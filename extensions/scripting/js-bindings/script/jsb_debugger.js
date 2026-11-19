/*
 * Copyright (c) 2013-2016 Chukong Technologies Inc.
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Axmol Engine contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// A SpiderMonkey 33 Debugger compartment. Uses Cocos v3's actor protocol and
// byte-counted JSON transport, without browser/XPCOM or Firefox dependencies.
(function (host) {
    "use strict";
    var dbg, debuggee, connected = false, attached = false, paused = false;
    var newest = null, nextID = 1, pauseEpoch = 0, evaluating = false;
    var sources = [], scripts = [], breakpoints = [], transient = {};
    var stepHooks = [], pauseNext = false, pauseExceptions = false, ignoreCaught = false;

    function send(packet) {
        if (!connected) return;
        var json = JSON.stringify(packet);
        var size = unescape(encodeURIComponent(json)).length;
        _bufferWrite(size + ":" + json);
    }
    function reply(request, packet) {
        packet.from = packet.from || request.to;
        // Optional correlation extension; requests from old clients work too.
        if (request._requestId !== undefined) packet._requestId = request._requestId;
        send(packet);
    }
    function error(request, name, message) {
        reply(request, {error: name, message: String(message || name)});
    }
    function actor(prefix, object) {
        var id = prefix + pauseEpoch + "_" + nextID++;
        transient[id] = object;
        return id;
    }
    function normalize(url) {
        return String(url || "").replace(/\\/g, "/").replace(/^file:\/\//, "");
    }
    function sourceForm(entry) {
        return {actor: entry.actor, url: entry.url, isBlackBoxed: false};
    }
    function addSource(source, url) {
        for (var i = 0; i < sources.length; ++i) {
            if (sources[i].source === source) return sources[i];
        }
        var entry = {actor: "source" + nextID++, source: source, url: normalize(url || source.url)};
        sources.push(entry);
        if (attached) send({from: "thread0", type: "newSource", source: sourceForm(entry)});
        return entry;
    }
    function registerScript(script) {
        if (evaluating) return;
        if (scripts.indexOf(script) !== -1) return;
        scripts.push(script);
        addSource(script.source, script.url);
        for (var i = 0; i < breakpoints.length; ++i) installBreakpoint(breakpoints[i], script);
        var children = script.getChildScripts();
        for (i = 0; i < children.length; ++i) registerScript(children[i]);
    }
    function location(frame) {
        if (!frame || !frame.script) return null;
        var entry = addSource(frame.script.source, frame.script.url);
        return {source: sourceForm(entry), line: frame.script.getOffsetLine(frame.offset), column: 0};
    }
    function grip(value) {
        if (value === undefined) return {type: "undefined"};
        if (value === null) return {type: "null"};
        if (typeof value === "number" && !isFinite(value))
            return {type: isNaN(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity"};
        if (typeof value === "number" && value === 0 && 1 / value < 0) return {type: "-0"};
        if (typeof value !== "object" && typeof value !== "function") return value;
        if (value.optimizedOut) return {type: "unavailable", optimizedOut: true};
        if (value.uninitialized) return {type: "unavailable", uninitialized: true};
        // These are Debugger.Object wrappers, never game objects passed into
        // this compartment. Enumeration never calls user property getters.
        return {type: "object", actor: actor("object", {kind: "object", value: value}),
                class: value.class || "Object", name: value.displayName || value.name || undefined};
    }
    function environmentForm(env, depth) {
        if (!env || depth > 32) return null;
        var result = {actor: actor("environment", {kind: "environment", value: env}), type: env.type};
        if (env.parent) result.parent = environmentForm(env.parent, depth + 1);
        if (env.type === "object" || env.type === "with") result.object = grip(env.object);
        return result;
    }
    function frameForm(frame, depth) {
        return {actor: actor("frame", {kind: "frame", value: frame}), type: frame.type,
                depth: depth, callee: frame.callee ? grip(frame.callee) : null,
                displayName: frame.callee ? frame.callee.displayName || frame.callee.name || "(anonymous)" : "(global)",
                where: location(frame), environment: environmentForm(frame.environment, 0),
                this: grip(frame.this), oldest: !frame.older};
    }
    function clearSteps() {
        for (var i = 0; i < stepHooks.length; ++i) {
            var frame = stepHooks[i];
            try { frame.onStep = undefined; frame.onPop = undefined; } catch (ignore) {}
        }
        stepHooks = [];
        if (dbg) dbg.onEnterFrame = undefined;
        pauseNext = false;
    }
    function pause(frame, why) {
        if (!attached || paused || evaluating) return;
        clearSteps();
        paused = true;
        newest = frame;
        transient = {};
        ++pauseEpoch;
        send({from: "thread0", type: "paused", actor: "pause" + pauseEpoch,
              frame: frame ? frameForm(frame, 0) : null, why: why});
        // Game execution remains on the VM thread inside this hook. Only
        // queued debugger requests execute until resume/detach/disconnect.
        _enterNestedEventLoop();
    }
    function setStep(frame, type) {
        if (!frame || !frame.script) {
            pauseNext = true;
            dbg.onEnterFrame = function (entered) { pause(entered, {type: "resumeLimit"}); };
            return;
        }
        var startLine = frame.script.getOffsetLine(frame.offset);
        var startScript = frame.script;
        function watchParent(parent) {
            if (!parent || !parent.script) {
                pauseNext = true;
                dbg.onEnterFrame = function (entered) { pause(entered, {type: "resumeLimit"}); };
                return;
            }
            stepHooks.push(parent);
            parent.onStep = function () { pause(this, {type: "resumeLimit"}); };
        }
        stepHooks.push(frame);
        frame.onPop = function () { watchParent(this.older); };
        if (type !== "finish") frame.onStep = function () {
            if (this.script !== startScript || this.script.getOffsetLine(this.offset) !== startLine)
                pause(this, {type: "resumeLimit"});
        };
        if (type === "step") dbg.onEnterFrame = function (entered) {
            if (entered.script) pause(entered, {type: "resumeLimit"});
        };
    }
    function resume(request) {
        if (!paused) return error(request, "wrongState", "Thread is running");
        if (request.pauseOnExceptions !== undefined) pauseExceptions = !!request.pauseOnExceptions;
        if (request.ignoreCaughtExceptions !== undefined) ignoreCaught = !!request.ignoreCaughtExceptions;
        var frame = newest;
        clearSteps();
        if (request.resumeLimit) setStep(frame, request.resumeLimit.type);
        paused = false;
        newest = null;
        transient = {};
        reply(request, {type: "resumed"});
        _debuggerStartupReady();
        if (_getEventLoopNestLevel()) _exitNestedEventLoop();
    }
    function removeBreakpoint(bp) {
        for (var i = 0; i < bp.scripts.length; ++i) {
            try { bp.scripts[i].clearBreakpoint(bp.handler); } catch (ignore) {}
        }
        var index = breakpoints.indexOf(bp);
        if (index !== -1) breakpoints.splice(index, 1);
    }
    function installBreakpoint(bp, script) {
        if (normalize(script.url || script.source.url) !== bp.url) return;
        // Choose one source line across the whole script tree. Sliding each
        // script independently would also stop at a parent's function call
        // when a child function already has an exact executable offset.
        var best = null, i, candidate, matching = [];
        for (i = 0; i < scripts.length; ++i) {
            var current = scripts[i];
            if (normalize(current.url || current.source.url) !== bp.url) continue;
            matching.push(current);
            if (current.getLineOffsets(bp.line).length) { best = bp.line; continue; }
            var all = current.getAllOffsets();
            for (candidate = bp.line + 1; candidate < all.length; ++candidate) {
                if (all[candidate] && all[candidate].length) {
                    if (best === null || candidate < best) best = candidate;
                    break;
                }
            }
        }
        if (best === null) return;
        var changed = bp.actualLine !== best;
        if (changed) {
            for (i = 0; i < bp.scripts.length; ++i) bp.scripts[i].clearBreakpoint(bp.handler);
            bp.scripts = [];
            bp.actualLine = best;
        }
        for (i = 0; i < matching.length; ++i) {
            current = matching[i];
            if (bp.scripts.indexOf(current) !== -1) continue;
            var offsets = current.getLineOffsets(best);
            if (!offsets.length) continue;
            for (candidate = 0; candidate < offsets.length; ++candidate) current.setBreakpoint(offsets[candidate], bp.handler);
            bp.scripts.push(current);
        }
        if (changed && attached) send({from: "thread0", type: "breakpointResolved", actor: bp.actor,
                                     location: {url: bp.url, line: bp.actualLine}});
    }
    function createBreakpoint(request, url, line) {
        if (!url || typeof line !== "number" || line < 1 || line % 1 !== 0)
            return error(request, "badParameterType", "Breakpoint requires URL and positive integer line");
        var bp = {actor: "breakpoint" + nextID++, url: normalize(url), line: line,
                  actualLine: null, scripts: [], condition: request.condition || null, handler: null};
        bp.handler = {hit: function (frame) {
            if (evaluating) return;
            if (bp.condition) {
                var completion;
                evaluating = true;
                try { completion = frame.eval(bp.condition); } finally { evaluating = false; }
                if (!completion || ("return" in completion && !completion.return)) return;
                if ("throw" in completion) return pause(frame, {type: "exception", exception: grip(completion.throw)});
            }
            pause(frame, {type: "breakpoint", actors: [bp.actor]});
        }};
        breakpoints.push(bp);
        for (var i = 0; i < scripts.length; ++i) installBreakpoint(bp, scripts[i]);
        reply(request, {actor: bp.actor, isPending: bp.actualLine === null,
                        actualLocation: {url: bp.url, line: bp.actualLine === null ? line : bp.actualLine}});
    }
    function detach(releaseStartup) {
        clearSteps();
        while (breakpoints.length) removeBreakpoint(breakpoints[0]);
        transient = {};
        newest = null;
        paused = false;
        attached = false;
        if (dbg) dbg.enabled = false;
        if (releaseStartup !== false) _debuggerStartupReady();
        while (_getEventLoopNestLevel()) _exitNestedEventLoop();
    }
    function inspectObject(object) {
        var properties = {}, names = object.getOwnPropertyNames();
        for (var i = 0; i < names.length; ++i) {
            var name = names[i], descriptor;
            try { descriptor = object.getOwnPropertyDescriptor(name); } catch (ignore) { continue; }
            if (!descriptor) continue;
            var form = {configurable: descriptor.configurable, enumerable: descriptor.enumerable};
            if ("value" in descriptor) { form.value = grip(descriptor.value); form.writable = descriptor.writable; }
            else { form.get = grip(descriptor.get); form.set = grip(descriptor.set); }
            // defineProperty is essential for a property actually named __proto__.
            Object.defineProperty(properties, name, {value: form, enumerable: true});
        }
        return {ownProperties: properties, prototype: grip(object.proto)};
    }
    function environmentBindings(env) {
        var variables = {}, names = env.names();
        for (var i = 0; i < names.length; ++i) {
            var value;
            try { value = env.getVariable(names[i]); }
            catch (ignore) { value = {optimizedOut: true}; }
            Object.defineProperty(variables, names[i], {value: {value: grip(value)}, enumerable: true});
        }
        return {arguments: [], variables: variables};
    }
    function evaluate(request, frame) {
        if (!paused) return error(request, "wrongState", "Evaluate requires a paused thread");
        evaluating = true;
        var completion;
        try {
            completion = frame ? frame.eval(String(request.expression)) : debuggee.executeInGlobal(String(request.expression));
        } finally { evaluating = false; }
        if (!completion) return reply(request, {result: {type: "unavailable"}});
        if ("throw" in completion) return reply(request, {exception: grip(completion.throw)});
        reply(request, {result: grip(completion.return)});
    }
    function dispatch(request) {
        var to = request.to, type = request.type, i, entry;
        if (to === "root") {
            if (type === "listTabs") return reply(request, {tabs: [{actor: "tab0", title: "Axmol JS",
                url: "axmol://game", consoleActor: "console0"}], selected: 0});
            if (type === "getRoot") return reply(request, {applicationType: "axmol", traits: {sources: true}});
        } else if (to === "tab0") {
            if (type === "attach") return reply(request, {type: "tabAttached", threadActor: "thread0"});
            if (type === "detach") { detach(); return reply(request, {type: "detached"}); }
        } else if (to === "thread0") {
            if (type === "attach") {
                if (attached) return error(request, "wrongState", "Already attached");
                attached = true;
                dbg.enabled = true;
                var found = dbg.findScripts();
                for (i = 0; i < found.length; ++i) registerScript(found[i]);
                paused = true;
                newest = dbg.getNewestFrame();
                ++pauseEpoch;
                // Attach is a configuration pause. Native startup wait holds
                // execution; an ordinary frame pump returns without nesting.
                reply(request, {type: "paused", why: {type: "attached"},
                    frame: newest ? frameForm(newest, 0) : null, actor: "pause" + pauseEpoch});
                if (newest) _enterNestedEventLoop();
                return;
            }
            if (type === "detach") { detach(); return reply(request, {type: "detached"}); }
            if (type === "resume") return resume(request);
            if (type === "interrupt") {
                if (paused) return reply(request, {type: "paused", why: {type: "alreadyPaused"}});
                reply(request, {type: "willInterrupt"});
                var frame = dbg.getNewestFrame();
                if (frame) return pause(frame, {type: "interrupted"});
                pauseNext = true;
                dbg.onEnterFrame = function (entered) { pause(entered, {type: "interrupted"}); };
                return;
            }
            if (type === "reconfigure") {
                if (request.options) {
                    pauseExceptions = !!request.options.pauseOnExceptions;
                    ignoreCaught = !!request.options.ignoreCaughtExceptions;
                }
                return reply(request, {});
            }
            if (type === "sources") return reply(request, {sources: sources.map(sourceForm)});
            if (type === "setBreakpoint") return createBreakpoint(request,
                request.location && request.location.url, request.location && request.location.line);
            if (type === "frames") {
                if (!paused) return error(request, "wrongState", "Stack requires a paused thread");
                var frames = [], depth = 0, current = newest;
                var first = request.start || 0, limit = request.count === undefined ? 1000 : request.count;
                while (current && depth < first + limit) {
                    if (depth >= first) frames.push(frameForm(current, depth));
                    current = current.older; ++depth;
                }
                return reply(request, {frames: frames, totalFrames: countFrames(newest)});
            }
            if (type === "clientEvaluate") {
                entry = request.frame ? transient[request.frame] : null;
                return evaluate(request, entry && entry.kind === "frame" ? entry.value : newest);
            }
        } else if (to === "console0" && type === "evaluateJS") {
            return evaluate(request, newest);
        }
        for (i = 0; i < sources.length; ++i) {
            entry = sources[i];
            if (entry.actor !== to) continue;
            if (type === "source") return reply(request, {source: entry.source.text, contentType: "text/javascript"});
            if (type === "setBreakpoint") return createBreakpoint(request, entry.url, request.location && request.location.line);
        }
        for (i = 0; i < breakpoints.length; ++i) {
            if (breakpoints[i].actor === to && type === "delete") {
                removeBreakpoint(breakpoints[i]); return reply(request, {});
            }
        }
        entry = transient[to];
        if (entry && paused) {
            if (entry.kind === "frame" && type === "evaluate") return evaluate(request, entry.value);
            if (entry.kind === "environment" && type === "bindings")
                return reply(request, {bindings: environmentBindings(entry.value)});
            if (entry.kind === "object" && type === "prototypeAndProperties")
                return reply(request, inspectObject(entry.value));
        }
        error(request, entry || to === "thread0" || to === "root" ? "unrecognizedPacketType" : "noSuchActor",
              "Unsupported request or expired pause actor: " + to + "/" + type);
    }
    function countFrames(frame) {
        var count = 0;
        while (frame) { ++count; frame = frame.older; }
        return count;
    }

    host._prepareDebugger = function (global) {
        dbg = new Debugger();
        debuggee = dbg.addDebuggee(global);
        dbg.enabled = false;
        dbg.onNewScript = registerScript;
        dbg.onDebuggerStatement = function (frame) { pause(frame, {type: "debuggerStatement"}); };
        dbg.onExceptionUnwind = function (frame, value) {
            if (!pauseExceptions || evaluating) return;
            if (ignoreCaught) {
                for (var current = frame; current; current = current.older) {
                    if (current.script && current.script.isInCatchScope(current.offset)) return;
                }
            }
            pause(frame, {type: "exception", exception: grip(value)});
        };
        dbg.uncaughtExceptionHook = function (exception) {
            send({from: "thread0", type: "debuggerError", message: String(exception), stack: exception.stack});
            detach();
        };
    };
    host.processInput = function (input) {
        if (input === "connected") {
            detach(false);
            connected = true;
            send({from: "root", applicationType: "axmol", traits: {sources: true,
                conditionalBreakpoints: true, debuggerSourceActors: true, axmolRequestIds: true}});
            return;
        }
        if (input === "disconnected") { detach(); connected = false; return; }
        var request;
        try {
            // JS_NewStringCopyN preserves the raw input octets as Latin-1;
            // convert complete UTF-8 JSON packets once, after native framing.
            request = JSON.parse(decodeURIComponent(escape(input)));
            dispatch(request);
        } catch (exception) {
            if (request && request.to) error(request, "debuggerError", exception);
            else send({from: "root", error: "malformedPacket", message: String(exception)});
        }
    };
    host._shutdownDebugger = function () {
        detach();
        connected = false;
        sources = [];
        scripts = [];
        if (dbg) { dbg.removeAllDebuggees(); dbg = null; debuggee = null; }
    };
})(this);
