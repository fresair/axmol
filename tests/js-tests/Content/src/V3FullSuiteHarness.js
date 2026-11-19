// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Recording sidecar for the complete cocos2d-x/v3 native js-tests.
// Original imports stay byte-identical. Specialized device observers and
// independently corrected suite variants are not part of this sample.
(function (global) {
    var state = {
        filter: "", entryFilter: "", prepared: false, index: 0, category: null, caseRecord: null, caseLayer: null,
        categories: [], records: [], counts: {asserted: 0, rendered: 0, failed: 0, needsInteraction: 0, notApplicable: 0},
        menuRecorded: false, frame: 0, scene: null, sceneFrames: 0,
        categoryStarted: 0, lastProgress: 0, nextCategory: false, finished: false, errorCount: 0,
        categoriesNeedingInteraction: 0, nested: null, recoveryPending: null,
        captureEnabled: false, captureSequence: 0, registryEnabled: false, registry: null,
        sidecar: "", sidecarStarted: false, sidecarDone: false, sidecarStartedAt: 0,
        sidecarDescription: null, sidecarDuration: 180, serviceConfig: "", serviceConfigTransport: "plain",
        runNonce: "", processId: null, previousCaseType: null,
        layoutAuditEnabled: false, layoutAuditStarted: false,
        validationVariant: "original", validationRevision: "", validationManifest: null,
        validationSources: {}, validationLoadedSources: {}
    };
    var originalRequire = global.require;
    var hooksInstalled = false;

    function now() { return performance.now(); }
    global.jsV3StartTileMapInteractionProbe = function (options) {
        return global.axmolStartTileMapInteractionProbe(options.caseIndex);
    };
    // Autorun owns only the finite requests issued by these original categories.
    // A category scope exists before native onEnter (XHR starts there), and is
    // adopted by its first case. Native completion is drained by the monitor,
    // never from inside a callback while NativeXHR still owns its active root.
    function finiteAsyncGate() {
        var scope = null, pending = [], deferred = [], activeCallback = null, reportedOwner = null, stopped = false;
        var restorers = [], sequence = 0;
        function enabled() { return scope && state.category && scope.category === state.category.title && !state.finished && !stopped; }
        function scopedCategory(title) {
            return title === "TextureCache Test" || title === "XMLHttpRequest" ||
                title === "XMLHttpRequest send ArrayBuffer";
        }
        function beginCategory(category) {
            scope = scopedCategory(category.title) && !state.sidecar ?
                {category: category.title, caseRecord: null, deadline: null, error: ""} : null;
            install();
        }
        function bindCase(value) {
            if (!scope) return;
            if (scope.caseRecord && scope.caseRecord !== value) {
                if (pending.length || deferred.length) throw Error("Async case advanced before finite completion.");
                scope = {category: scope.category, caseRecord: null, deadline: null, error: ""};
            }
            scope.caseRecord = value;
            if (scope.error) value.async_callback_error = scope.error;
        }
        function reportError(error, owner) {
            owner = owner || activeCallback || reportedOwner || scope;
            if (!owner) return false;
            // Native error reporters run after the JS callback has unwound,
            // including its terminal finally. Keep its origin until the next
            // monitor tick so a loadend throw cannot also schedule recovery.
            reportedOwner = owner;
            var message = typeof error === "string" ? error : exceptionText(error);
            if (owner.error !== message) {
                owner.error = message;
                if (owner.caseRecord) owner.caseRecord.async_callback_error = message;
                record({kind: "async-callback-error", category: owner.category,
                    number: owner.caseRecord ? owner.caseRecord.number : null,
                    type: owner.caseRecord ? owner.caseRecord.type : null,
                    status: "failed", error: message});
            }
            return true;
        }
        function start(kind, url) {
            var operation = {owner: scope, kind: kind, url: String(url), started_at_ms: now(),
                id: ++sequence, done: false};
            if (scope.deadline === null) scope.deadline = operation.started_at_ms + 30000;
            pending.push(operation);
            operationRecord(operation, "started");
            return operation;
        }
        function finish(operation) {
            if (operation.done) return;
            operation.done = true;
            var index = pending.indexOf(operation);
            if (index >= 0) pending.splice(index, 1);
            operationRecord(operation, "completed");
        }
        function operationRecord(operation, phase) {
            var owner = operation.owner;
            record({kind: "async-operation", category: owner.category,
                number: owner.caseRecord ? owner.caseRecord.number : null,
                type: owner.caseRecord ? owner.caseRecord.type : null,
                operation: operation.id, operation_kind: operation.kind, url: operation.url,
                phase: phase, pending_count: pending.length, frame: state.frame,
                elapsed_ms: now() - operation.started_at_ms, deadline_ms: owner.deadline});
        }
        function invoke(operation, callback, receiver, args, terminal) {
            var previous = activeCallback;
            activeCallback = operation.owner;
            try { return typeof callback === "function" ? callback.apply(receiver, args) : undefined; }
            catch (error) { reportError(error, operation.owner); throw error; }
            finally {
                activeCallback = previous;
                if (terminal) finish(operation);
            }
        }
        function defer(key, callback) {
            if (stopped) return false;
            if (!pending.length && !activeCallback) return true;
            for (var i = 0; i < deferred.length; ++i) if (deferred[i].key === key) return false;
            deferred.push({key: key, callback: callback});
            return false;
        }
        function tick() {
            reportedOwner = null;
            if (stopped) return false;
            if (pending.length && now() >= pending[0].owner.deadline) {
                stopped = true;
                var owner = pending[0].owner;
                var operations = pending.map(function (item) {
                    return {kind: item.kind, url: item.url, elapsed_ms: now() - item.started_at_ms};
                });
                var message = "Finite async completion exceeded its absolute 30-second deadline.";
                reportError(message, owner);
                record({kind: "async-timeout", category: owner.category, status: "failed",
                    pending_count: operations.length, operations: operations,
                    navigation_stopped: true, deadline_ms: owner.deadline});
                deferred = [];
                if (owner.caseRecord && !owner.caseRecord.complete) completeCase("failed", message);
                state.recoveryPending = null;
                state.finished = true;
                restore();
                return false;
            }
            if (pending.length || activeCallback) return false;
            var ready = deferred;
            deferred = [];
            for (var i = 0; i < ready.length; ++i) {
                if (stopped) break;
                // A completion may start another finite operation; preserve
                // ordering and wait for it before executing navigation.
                if (pending.length) { deferred = ready.slice(i).concat(deferred); break; }
                ready[i].callback();
            }
            return !pending.length;
        }
        function restore() {
            for (var i = restorers.length - 1; i >= 0; --i) restorers[i]();
            restorers = [];
        }
        function replace(owner, key, wrapper) {
            var original = owner[key];
            owner[key] = wrapper(original);
            var installed = owner[key];
            restorers.push(function () { if (owner[key] === installed) owner[key] = original; });
        }
        function install() {
            if (restorers.length || !scope) return;
            replace(cc.loader, "loadImg", function (original) {
                return function (url) {
                    if (!enabled() || scope.category !== "TextureCache Test") return original.apply(this, arguments);
                    var args = Array.prototype.slice.call(arguments), index = args.length === 2 ? 1 : 2;
                    if (typeof args[index] !== "function") return original.apply(this, arguments);
                    var operation = start("loadImg", url), callback = args[index];
                    args[index] = function () { return invoke(operation, callback, this, arguments, true); };
                    try { return original.apply(this, args); }
                    catch (error) { reportError(error, operation.owner); finish(operation); throw error; }
                };
            });
            if (typeof global.XMLHttpRequest !== "function") return;
            var prototype = global.XMLHttpRequest.prototype;
            replace(prototype, "open", function (original) {
                return function (method, url) {
                    if (enabled() && scope.category !== "TextureCache Test") this.__v3FiniteURL = String(url);
                    return original.apply(this, arguments);
                };
            });
            replace(prototype, "send", function (original) {
                return function () {
                    if (!enabled() || scope.category === "TextureCache Test") return original.apply(this, arguments);
                    var operation = start("xhr", this.__v3FiniteURL || "<URL unavailable>");
                    var xhr = this, saved = {}, installed = {};
                    ["onreadystatechange", "onload", "onerror", "onabort", "ontimeout", "onloadend"].forEach(function (name) {
                        saved[name] = xhr[name];
                        if (typeof saved[name] !== "function" && name !== "onloadend") return;
                        installed[name] = xhr[name] = function () {
                            try { return invoke(operation, saved[name], this, arguments, name === "onloadend"); }
                            finally {
                                if (name === "onloadend") restoreHandlers();
                            }
                        };
                    });
                    function restoreHandlers() {
                        Object.keys(installed).forEach(function (name) {
                            if (xhr[name] === installed[name]) xhr[name] = saved[name];
                        });
                    }
                    try { return original.apply(this, arguments); }
                    catch (error) {
                        reportError(error, operation.owner); finish(operation); restoreHandlers(); throw error;
                    }
                };
            });
        }
        return {beginCategory: beginCategory, bindCase: bindCase, defer: defer, tick: tick,
            endCategory: function () { scope = null; },
            reportError: reportError, restore: restore,
            ownsErrors: function () { return !!(activeCallback || reportedOwner || scope && pending.length); },
            isStopped: function () { return stopped; },
            pendingCount: function () { return pending.length; }};
    }
    var asyncGate = state.asyncGate = finiteAsyncGate();
    function sidecarDescription(name) {
        // Specialized device observers are external to this sample application.
        return null;
    }
    function record(value) {
        value.validation_variant = state.validationVariant;
        if (state.validationVariant === "corrected") {
            value.validation_revision = state.validationRevision;
            if (value.source_revision) {
                value.upstream_source_revision = value.source_revision;
                value.source_revision = state.validationRevision;
            }
            if (value.source && state.validationSources[value.source])
                value.executed_source = state.validationSources[value.source].resolved_path;
        }
        state.records.push(value);
        if ((value.kind === "case" || value.status === "notApplicable") &&
            value.status && state.counts.hasOwnProperty(value.status)) state.counts[value.status]++;
        if (value.status === "failed" && value.kind !== "case") state.errorCount++;
        if (value.status === "needsInteraction" && value.kind === "category") state.categoriesNeedingInteraction++;
        v3Record(JSON.stringify(value));
    }
    function exceptionText(error) {
        var message = String(error);
        return error && error.stack ? message + "\n" + String(error.stack) : message;
    }
    function titleOf(layer) {
        try { return String(layer.getTitle ? layer.getTitle() : layer._title || "Untitled"); }
        catch (error) { return "title failed: " + exceptionText(error); }
    }
    function numberOf(layer) {
        try { return layer.getTestNumber ? layer.getTestNumber() : null; }
        catch (error) { return null; }
    }
    function typeOf(layer) {
        var prototype = Object.getPrototypeOf(layer);
        var names = Object.getOwnPropertyNames(global);
        for (var i = 0; i < names.length; ++i) {
            var value;
            try { value = global[names[i]]; } catch (error) { continue; }
            if (typeof value === "function" && value.prototype === prototype) return names[i];
        }
        return "OriginalTestLayer";
    }
    function afterDraw(callback) {
        var listener = cc.eventManager.addCustomListener("director_after_draw", function () {
            cc.eventManager.removeListener(listener);
            callback();
        });
    }
    function captureCase(value) {
        if (!state.captureEnabled || value.capture) return;
        var name = [value.category, value.path || "", value.number === null ? "manual" : value.number,
            value.type, state.captureSequence++].join("-").toLowerCase().replace(/[^a-z0-9_.-]/g, "-").replace(/\.{2,}/g, "-").slice(0, 200) + ".png";
        if (state.validationVariant === "corrected")
            name = "corrected-" + (state.runNonce || "interactive") + "-" + state.captureSequence + ".png";
        if (v3Capture(name)) {
            value.capture = {file: "v3-captures/" + name, phase: "director_after_draw",
                elapsed_ms: now() - value.started_at_ms, frame: state.frame};
            if (state.layoutAuditStarted && value.category === "UI Test" && value.type === "OriginalUIFactory")
                global.originalUILayoutAuditCapture(value, name);
        } else record({kind: "capture-error", category: value.category, number: value.number,
            type: value.type, status: "failed", error: "Native rendered-frame capture failed."});
    }
    var cardinalTimingCleanup = null;
    function clearCardinalTimingObserver() {
        if (cardinalTimingCleanup) cardinalTimingCleanup();
    }
    function installCardinalTimingObserver(layer, owner) {
        if (owner.category !== "Actions Test" || owner.number !== 11 || owner.type !== "ActionCardinalSpline") return;
        var restores = [], active = true, outerAction = null, innerAction = null, capturedTarget = null;
        var factoryParameters = null, submittedEqualsReturned = null;
        function diagnosticError(stage, error) {
            try { record({kind: "cardinal-spline-timing-diagnostic-error", category: owner.category,
                number: owner.number, type: owner.type, stage: stage, error: exceptionText(error)}); } catch (_) {}
        }
        function restoreAll(list) {
            while (list.length) {
                try { list.pop()(); } catch (error) { diagnosticError("restore", error); }
            }
        }
        function replace(object, name, makeWrapper, list) {
            var descriptor = Object.getOwnPropertyDescriptor(object, name), original = object[name];
            if (typeof original !== "function") throw new Error("Missing callable " + name);
            var wrapper = makeWrapper(original);
            object[name] = wrapper;
            if (object[name] !== wrapper) throw new Error("Cannot observe " + name);
            list.push(function () {
                // Another owner may have replaced the method in the meantime.
                if (object[name] !== wrapper) return;
                if (descriptor) Object.defineProperty(object, name, descriptor);
                else delete object[name];
            });
        }
        function cleanup() {
            if (!active) return;
            active = false;
            restoreAll(restores);
            // JS references only: no native retain/release or action updates.
            outerAction = innerAction = capturedTarget = null;
            if (cardinalTimingCleanup === cleanup) cardinalTimingCleanup = null;
        }
        cardinalTimingCleanup = cleanup;
        function actionFacts(action) {
            if (!action) return null;
            if (typeof jsb.isObjectValid === "function" && !jsb.isObjectValid(action))
                return {native_valid: false};
            return {native_valid: true, elapsed_seconds: action.getElapsed(), duration_seconds: action.getDuration(),
                is_sequence: typeof cc.Sequence === "function" ? action instanceof cc.Sequence : null,
                is_cardinal_spline_by: typeof cc.CardinalSplineBy === "function" ? action instanceof cc.CardinalSplineBy : null,
                target_is_actual_tamara: action.getTarget() === layer._tamara};
        }
        function observe(stage, receiver, args, field, nominalDelay) {
            if (!active || state.caseRecord !== owner) return;
            try {
                var outer = actionFacts(outerAction), inner = actionFacts(innerAction);
                var saved = field && receiver[field], position = receiver._tamara ? receiver._tamara.getPosition() : null;
                var normalized = outer && inner && outer.native_valid && inner.native_valid &&
                    Math.abs(outer.duration_seconds - 4.5) < 0.0001 && inner.duration_seconds === 2 &&
                    outer.elapsed_seconds >= 0 && outer.elapsed_seconds <= 2 ? outer.elapsed_seconds / 2 : null;
                record({kind: "cardinal-spline-callback-observation", category: owner.category,
                    number: owner.number, type: owner.type, stage: stage, nominal_delay_seconds: nominalDelay,
                    original_callback_dt: args.length ? args[0] : null,
                    callback_dt_scope: "Unchanged original callback argument; Timer may pass the nominal delay, not its accumulated elapsed.",
                    director_total_frames: cc.director.getTotalFrames(), director_delta_seconds: cc.director.getDeltaTime(),
                    animation_interval: cc.director.getAnimationInterval(), harness_frame: state.frame, time_ms: now(),
                    stored_control_point: saved ? {x: saved.x, y: saved.y} : null,
                    tamara_position: position ? {x: position.x, y: position.y} : null,
                    run_action_receiver_is_actual_tamara: capturedTarget === receiver._tamara && !!capturedTarget,
                    submitted_action_equals_run_action_return: submittedEqualsReturned,
                    outer_sequence: outer, inner_cardinal_spline_by: inner, factory_parameters: factoryParameters,
                    derived_first_forward_segment_normalized_time: normalized,
                    elapsed_scope: "Outer runAction-returned Sequence is the real elapsed accumulator. Its original segments are CardinalSplineBy(2), Delay(.25), Reverse(2), Delay(.25). Inner getElapsed is recorded literally; Sequence calls inner.update directly, so inner elapsed is NOT its update parameter.",
                    derived_time_scope: "Outer elapsed/2 only while the original first forward segment is active; derived value, not intercepted native update argument.",
                    native_timer_accumulated_elapsed: null,
                    phase: "Immediately after unchanged original callback in the same synchronous invocation"});
            } catch (error) { diagnosticError(stage, error); }
        }
        try {
            ["checkControl1", "checkControl2", "checkControl3"].forEach(function (name, index) {
                replace(layer, name, function (original) {
                    return function () {
                        var result = original.apply(this, arguments);
                        observe(name, this, arguments, "control" + (index + 1), (index + 1) * 0.5);
                        return result;
                    };
                }, restores);
            });
            replace(layer, "setupAutomation", function (original) {
                return function () {
                    observe("setupAutomation-before", this, arguments, null, null);
                    try {
                        var result = original.apply(this, arguments);
                        observe("setupAutomation-after", this, arguments, null, null);
                        return result;
                    } catch (error) { cleanup(); throw error; }
                };
            }, restores);
            replace(layer, "onEnter", function (original) {
                return function () {
                    var temporary = [];
                    try {
                        try {
                            // The original ActionsDemo ctor has now created Tamara.
                            // Observe only this receiver, never Node.prototype globally.
                            replace(layer._tamara, "runAction", function (runAction) {
                                return function () {
                                    var result = runAction.apply(this, arguments);
                                    if (this === layer._tamara) {
                                        capturedTarget = this;
                                        outerAction = result;
                                        submittedEqualsReturned = result === arguments[0];
                                    }
                                    return result;
                                };
                            }, temporary);
                            // Sequence exposes no child getter. Capture the actual
                            // original factory result, not a clone or replacement.
                            replace(cc, "cardinalSplineBy", function (factory) {
                                return function () {
                                    var result = factory.apply(this, arguments);
                                    if (arguments[0] === 2 && arguments[2] === 0) {
                                        innerAction = result;
                                        try {
                                            factoryParameters = {duration_seconds: arguments[0], tension: arguments[2],
                                                control_point_count: arguments[1] ? arguments[1].length : null,
                                                control_points: Array.prototype.map.call(arguments[1], function (point) {
                                                    return {x: point.x, y: point.y};
                                                })};
                                        } catch (error) { diagnosticError("factory-parameters", error); }
                                    }
                                    return result;
                                };
                            }, temporary);
                        } catch (error) {
                            restoreAll(temporary);
                            diagnosticError("install-onEnter-capture", error);
                        }
                        var result = original.apply(this, arguments);
                        observe("onEnter-after", this, [], null, null);
                        return result;
                    } catch (error) { cleanup(); throw error; }
                    finally { restoreAll(temporary); }
                };
            }, restores);
            replace(layer, "onExit", function (original) {
                return function () {
                    try { return original.apply(this, arguments); }
                    finally { cleanup(); }
                };
            }, restores);
        } catch (error) { diagnosticError("install-instance", error); cleanup(); }
    }
    function startCase(layer) {
        if (!state.category || state.sidecarStarted) return;
        clearCardinalTimingObserver();
        state.caseLayer = layer;
        if (state.caseRecord && !state.caseRecord.complete) {
            completeCase("needsInteraction", "Original case advanced without a teardown result.");
        }
        state.caseRecord = {
            kind: "case", category: state.category.title, source: state.category.linksrc || "",
            number: numberOf(layer), type: typeOf(layer), title: titleOf(layer),
            started_at_ms: now(), started_frame: state.frame, complete: false, from_base: true
        };
        asyncGate.bindCase(state.caseRecord);
        if (state.nested) state.caseRecord.path = state.nested.path();
        record({kind: "case-start", category: state.caseRecord.category, source: state.caseRecord.source,
            number: state.caseRecord.number, type: state.caseRecord.type, title: state.caseRecord.title});
        installCardinalTimingObserver(layer, state.caseRecord);
        state.lastProgress = now();
    }
    function completeCase(status, detail) {
        if (!state.caseRecord || state.caseRecord.complete) return true;
        var value = state.caseRecord;
        // Failure survives later rendered/asserted completion attempts while
        // requests are draining, even when the native reporter clears errors.
        if (status === "failed") value.async_pending_failure = detail || "Original case failed.";
        if (!asyncGate.isStopped() && !asyncGate.defer(value, function () { completeCase(status, detail); })) {
            value.comparison_phase = "awaiting-finite-async-completion";
            return false;
        }
        if (value.async_callback_error || value.async_pending_failure) {
            status = "failed";
            detail = value.async_callback_error || value.async_pending_failure;
        }
        if (state.captureEnabled && !value.terminal_without_after_draw &&
            (status === "rendered" || status === "asserted") &&
            value.comparison_phase !== "director_after_draw") {
            if (!value.captureQueued) {
                value.captureQueued = true;
                afterDraw(function () {
                    if (state.caseRecord !== value || value.complete) return;
                    value.comparison_phase = "director_after_draw";
                    completeCase(status, detail);
                });
            }
            return false;
        }
        if (value.reported_native_callback_error || value.reported_native_update_error) {
            status = "failed";
            detail = value.reported_native_update_error || value.reported_native_callback_error;
            value.failure_scope = value.reported_native_update_error ?
                "Reported original Retro update dereferenced its null space glyph; other original GL observations completed through native navigation, while this failure remains preserved." :
                (value.reported_native_callback_scope ||
                 "Reported original constructor callback exception; native factory returned and its real frames/state were observed before navigation.");
        }
        value.complete = true;
        clearCardinalTimingObserver();
        value.status = status;
        value.frames = state.frame - value.started_frame;
        value.elapsed_ms = now() - value.started_at_ms;
        if (detail) value.detail = detail;
        if (value.type === "ActionManual") {
            value.binding_diagnostic = {
                node_rgb_adapter: !!cc.Node.prototype.getColor.__legacyRGBResult,
                sprite_rgb_adapter: !!cc.Sprite.prototype.getColor.__legacyRGBResult,
                sprite_inherits_node: cc.Node.prototype.isPrototypeOf(cc.Sprite.prototype),
                enumerable_node: Object.keys(cc).indexOf("Node") >= 0,
                enumerable_sprite: Object.keys(cc).indexOf("Sprite") >= 0
            };
        }
        if (value.type === "ActionRepeatForever" && state.caseLayer && state.caseLayer._grossini)
            value.observed_rotation = state.caseLayer._grossini.rotation;
        if (value.type === "ActionCardinalSpline" && state.caseLayer) {
            value.control_points = {
                control1: state.caseLayer.control1 || null,
                control2: state.caseLayer.control2 || null,
                control3: state.caseLayer.control3 || null,
                win_size: {width: winSize.width, height: winSize.height}
            };
        }
        if (value.type === "OriginalNativeCategoryScene")
            value.category_elapsed_ms = now() - state.categoryStarted;
        if (value.comparison_phase === "director_after_draw") captureCase(value);
        record(value);
        state.lastProgress = now();
        return true;
    }
    function returnToMenu() {
        if (!asyncGate.defer("returnToMenu", returnToMenu)) return;
        clearCardinalTimingObserver();
        if (cc.director.isPaused()) cc.director.resume();
        var scene = new cc.Scene();
        scene.addChild(new TestController());
        cc.director.runScene(scene);
        state.nextCategory = true;
    }
    function installBaseHooks() {
        if (hooksInstalled || typeof BaseTestLayer !== "function") return;
        hooksInstalled = true;
        var originalCtor = BaseTestLayer.prototype.ctor;
        var originalSharedCtor = BaseTestLayerProps.ctor;
        var originalReadPixels = BaseTestLayer.prototype.readPixels;
        function observeShaderUniforms(layer, coordinates) {
            if (!state.category || !state.caseRecord || state.category.title !== "OpenGL Test" ||
                [6, 7].indexOf(state.caseRecord.number) < 0) return;
            var owner = state.caseRecord;
            function diagnosticError(path, uniform, error) {
                try { record({kind: "shader-readpixels-uniform-diagnostic-error", category: owner.category,
                    number: owner.number, type: owner.type, path: path, uniform: uniform,
                    error: exceptionText(error)}); } catch (_) {}
            }
            function inspect(node, path, depth) {
                if (depth > 3) return;
                if (typeof ShaderNode === "function" && node instanceof ShaderNode) {
                    try {
                        var program = node.shader.getProgram(), uniforms = {};
                        ["CC_Time", "center", "resolution"].forEach(function (name) {
                            try {
                                var value = gl.getUniform(program, gl.getUniformLocation(program, name));
                                uniforms[name] = value && typeof value.length === "number" ?
                                    Array.prototype.slice.call(value) : value;
                            } catch (error) { diagnosticError(path, name, error); }
                        });
                        record({kind: "shader-readpixels-uniform-observation", category: owner.category,
                            number: owner.number, type: owner.type, path: path, coordinates: coordinates,
                            director_total_frames: cc.director.getTotalFrames(),
                            director_delta_seconds: cc.director.getDeltaTime(), harness_frame: state.frame, time_ms: now(),
                            uniforms: uniforms, node_position: node.getPosition(), node_content_size: node.getContentSize(),
                            node_width: node.width, node_height: node.height, node_world_matrix: node.getNodeToWorldTransform3D(),
                            window_size: typeof winSize === "undefined" ? null : {width: winSize.width, height: winSize.height},
                            frame_size: cc.view.getFrameSize(), visible_size: cc.view.getVisibleSize(), view: v3ViewState(),
                            device_pixel_ratio: typeof global.devicePixelRatio === "undefined" ? null : global.devicePixelRatio,
                            window_device_pixel_ratio: global.window && typeof global.window.devicePixelRatio !== "undefined" ?
                                global.window.devicePixelRatio : null,
                            js_node_time: node._time,
                            js_node_time_scope: "Read-only ShaderNode._time; original draw does not upload this field as CC_Time.",
                            uniform_scope: "gl.getUniform reads the source GLProgram ProgramState. Native drawArrays clones that state at command submission; these getters are not a direct read of the draw clone or GPU uniform buffer.",
                            phase: "During the unchanged original readPixels call, immediately before the native read"});
                    } catch (error) { diagnosticError(path, null, error); }
                }
                node.getChildren().forEach(function (child, index) { inspect(child, path + "/" + index, depth + 1); });
            }
            try { inspect(layer, "layer", 0); }
            catch (error) { diagnosticError("traverse", null, error); }
        }
        function observeReadPixels() {
            observeShaderUniforms(this, Array.prototype.slice.call(arguments));
            var pixels = originalReadPixels.apply(this, arguments);
            if (state.category && state.caseRecord) record({kind: "pixel-observation",
                category: state.category.title, number: state.caseRecord.number,
                type: state.caseRecord.type, coordinates: Array.prototype.slice.call(arguments),
                byte_count: pixels.length, rgba: Array.prototype.slice.call(pixels, 0, 100),
                director_total_frames: cc.director.getTotalFrames(),
                animation_interval: cc.director.getAnimationInterval(), harness_frame: state.frame,
                phase: "original readPixels call", contract: "physical framebuffer coordinates"});
            return pixels;
        }
        function recordPixelDiagnostics(layer, dt) {
            if (!state.caseRecord || !state.category) return;
            var category = state.category.title, number = state.caseRecord.number;
            if (["Sprite Test", "Label Test", "Layer Test", "TileMap Test", "OpenGL Test"].indexOf(category) < 0) return;
            try {
                record({kind: "pixel-environment", category: category, number: number,
                    type: state.caseRecord.type, view: v3ViewState(),
                    director_total_frames: cc.director.getTotalFrames(), animation_interval: cc.director.getAnimationInterval(),
                    original_comparison_dt: dt, phase: "before unchanged original teardown, director_after_draw"});
                if (category === "Sprite Test" && [9, 11, 12, 13, 14, 18, 36, 49].indexOf(number) >= 0) {
                    function inspect(node, path, depth) {
                        if (depth > 2) return;
                        if (node instanceof cc.Sprite || node instanceof cc.SpriteBatchNode) {
                            record({kind: "sprite-native-transform", category: category, number: number, path: path,
                                tag: node.getTag(), position: node.getPosition(), rotation: node.getRotation(),
                                scale_x: node.getScaleX(), scale_y: node.getScaleY(), opacity: node.getOpacity(),
                                displayed_opacity: node.getDisplayedOpacity(), color: node.getColor(),
                                world_matrix: node.getNodeToWorldTransform3D()});
                            if (node instanceof cc.Sprite) {
                                var texture = node.getTexture(), frame = node.getSpriteFrame();
                                record({kind: "sprite-native-frame", category: category, number: number, path: path,
                                    director_total_frames: cc.director.getTotalFrames(), harness_frame: state.frame,
                                    local_z_order: node.getLocalZOrder(), anchor: node.getAnchorPoint(),
                                    content_size: node.getContentSize(), texture_rect: node.getTextureRect(),
                                    texture_rect_rotated: node.isTextureRectRotated(), offset_position: node.getOffsetPosition(),
                                    texture_path: texture ? texture.getPath() : null,
                                    texture_size: texture ? {width: texture.getWidth(), height: texture.getHeight()} : null,
                                    frame_rect: frame ? frame.getRect() : null,
                                    frame_original_size: frame ? frame.getOriginalSize() : null,
                                    frame_offset: frame ? frame.getOffset() : null,
                                    phase: "before unchanged original teardown, director_after_draw"});
                            }
                        }
                        node.getChildren().forEach(function (child, index) { inspect(child, path + "/" + index, depth + 1); });
                    }
                    layer.getChildren().forEach(function (node, index) { inspect(node, String(index), 0); });
                }
                if (category === "Label Test" && (number === 2 || number === 18)) {
                    var files = number === 2 ? ["fonts/larabie-16.plist", "fonts/larabie-16.png",
                        "fonts/larabie-16-hd.plist", "fonts/larabie-16-hd.png"] :
                        ["fonts/bitmapFontChinese.fnt", "fonts/bitmapFontChinese.png"];
                    files.forEach(function (file) { record({kind: "label-resource-path", category: category,
                        number: number, resource: file, full_path: jsb.fileUtils.fullPathForFilename(s_resprefix + file)}); });
                    layer.getChildren().forEach(function (node, index) {
                        if (node instanceof cc.LabelAtlas || node instanceof cc.LabelBMFont || node instanceof cc.LabelTTF)
                            record({kind: "label-native-color", category: category, number: number, child: index,
                                string: node.getString(), color: node.getColor(), opacity: node.getOpacity(),
                                displayed_opacity: node.getDisplayedOpacity(), position: node.getPosition()});
                    });
                }
                if (category === "Layer Test" && number === 2 && typeof v3GradientQuad === "function") {
                    layer.getChildren().forEach(function (node, index) {
                        if (node instanceof cc.LayerGradient) {
                            var vector = node.getVector();
                            record({kind: "gradient-native-quad", category: category, number: number, child: index,
                                compressed: node.isCompressedInterpolation(), vector: {x: vector.x, y: vector.y},
                                start_color: node.getStartColor(), end_color: node.getEndColor(),
                                start_opacity: node.getStartOpacity(), end_opacity: node.getEndOpacity(),
                                opacity: node.getOpacity(), displayed_opacity: node.getDisplayedOpacity(),
                                colors_bl_br_tl_tr: v3GradientQuad(node),
                                director_total_frames: cc.director.getTotalFrames(),
                                phase: "before unchanged original teardown, director_after_draw"});
                        }
                    });
                }
            } catch (error) {
                record({kind: "pixel-diagnostic-error", category: category, number: number, error: exceptionText(error)});
            }
        }
        BaseTestLayer.prototype.readPixels = observeReadPixels;
        BaseTestLayerProps.readPixels = observeReadPixels;
        BaseTestLayer.prototype.ctor = function () {
            if (!state.registry) startCase(this);
            try { return originalCtor.apply(this, arguments); }
            catch (error) { clearCardinalTimingObserver(); throw error; }
        };
        function compareOriginal(layer, dt) {
            var status = "asserted", detail = "";
            if (state.caseRecord) state.caseRecord.original_comparison_dt = dt;
            if (state.caseRecord && state.caseRecord.type === "ActionCardinalSpline" &&
                state.caseRecord.number === 11) {
                try {
                    function snapshot(point) { return point ? {x: point.x, y: point.y} : null; }
                    record({kind: "cardinal-spline-comparison-observation", category: state.caseRecord.category,
                        number: 11, type: "ActionCardinalSpline", control1: snapshot(layer.control1),
                        control2: snapshot(layer.control2), control3: snapshot(layer.control3),
                        tamara_position: snapshot(layer._tamara.getPosition()), original_comparison_dt: dt,
                        frame: state.frame, time_ms: now(), director_total_frames: cc.director.getTotalFrames(),
                        phase: "before unchanged original teardown; original scheduled snapshots read only"});
                } catch (diagnosticError) {
                    record({kind: "cardinal-spline-diagnostic-error", category: state.caseRecord.category,
                        number: 11, error: exceptionText(diagnosticError)});
                }
            }
            recordPixelDiagnostics(layer, dt);
            try {
                layer.errorDescription = "";
                if (!layer.tearDown(dt)) {
                    status = "failed";
                    detail = layer.errorDescription || "Original teardown comparison returned false.";
                }
            } catch (error) {
                var pendingReference = state.validationVariant === "corrected" &&
                    state.caseRecord && state.caseRecord.category === "OpenGL Test" &&
                    (state.caseRecord.number === 2 || state.caseRecord.number === 3) &&
                    error && error.code === "VALIDATION_REFERENCE_UNSTABLE" &&
                    error.reference_only === true && error.gpu_read_performed === false;
                if (pendingReference) {
                    var owner = state.caseRecord;
                    if (!owner.reference_conditioning) owner.reference_conditioning = {
                        started_ms: now(), deadline_ms: now() + 5000, attempts: 0,
                        started_frame: cc.director.getTotalFrames(), maximum_attempts: 180};
                    var wait = owner.reference_conditioning;
                    ++wait.attempts;
                    wait.last_reason = String(error.message || error);
                    wait.last_frame = cc.director.getTotalFrames();
                    if (now() < wait.deadline_ms && wait.attempts < wait.maximum_attempts) {
                        if (wait.attempts === 1 || wait.attempts % 30 === 0)
                            record({kind: "validation-reference-wait", category: owner.category,
                                number: owner.number, attempts: wait.attempts, reason: wait.last_reason,
                                director_total_frames: wait.last_frame, gpu_pixels_read: false,
                                scope: "Reference conditioning only; no actual framebuffer mismatch is retried."});
                        state.lastProgress = now();
                        layer.scheduleOnce(function () {
                            afterDraw(function () {
                                if (state.caseRecord === owner && !owner.complete) compareOriginal(layer, dt);
                            });
                        }, 0);
                        return;
                    }
                    error = new Error("Defined shader reference coverage did not become available within 5s/180 attempts: " +
                        wait.last_reason);
                }
                detail = exceptionText(error);
                // The original base throws this for visual tests without an
                // expected/current value. A real rendered frame is recorded,
                // but is never counted as a functional assertion.
                status = String(error) === "Not Implemented" ? "rendered" : "failed";
            }
            if (state.validationVariant === "corrected") {
                ["__validationEvidence", "__validationCardinalSamples", "__validationActionEvidence",
                    "__validationEaseEvidence"].forEach(function (field) {
                    if (layer[field] !== undefined) record({kind: "corrected-validation-evidence",
                        category: state.caseRecord.category, number: state.caseRecord.number,
                        type: state.caseRecord.type, field: field, evidence: layer[field],
                        director_total_frames: cc.director.getTotalFrames(),
                        phase: "after corrected copy teardown in the same synchronous comparison"});
                });
            }
            function finishComparison() {
                var comparisonCase = state.caseRecord;
                if (!asyncGate.defer("finishComparison", finishComparison)) {
                    if (comparisonCase) comparisonCase.comparison_phase = "awaiting-finite-async-completion";
                    return;
                }
                if (state.captureEnabled && comparisonCase &&
                    comparisonCase.comparison_phase !== "director_after_draw") {
                    afterDraw(function () {
                        if (state.caseRecord !== comparisonCase || comparisonCase.complete) return;
                        comparisonCase.comparison_phase = "director_after_draw";
                        finishComparison();
                    });
                    return;
                }
                completeCase(status, detail);
                try {
                    if (state.caseRecord && state.caseRecord.type === "ActionRepeatForever") {
                        // Preserve the original 3.5-second assertion, then collect
                        // an additional real native observation without changing
                        // its expected value or marking the assertion successful.
                        var rotation = layer._grossini.rotation;
                        layer.scheduleOnce(function () {
                            record({kind: "native-observation", category: "Actions Test",
                                type: "ActionRepeatForever", original_rotation: rotation,
                                later_rotation: layer._grossini.rotation, follow_up_seconds: 1.0});
                            layer.runNextTest();
                        }, 1.0);
                    } else {
                        // Comparison owns the after-draw image. Create the
                        // next original scene during the next real Scheduler
                        // phase so Director enters it before autorelease-pool
                        // cleanup, as the original endTest control flow does.
                        function advanceOriginal() {
                            if (!asyncGate.defer("base-navigation", advanceOriginal)) return;
                            layer.runNextTest();
                        }
                        layer.scheduleOnce(advanceOriginal, 0);
                    }
                }
                catch (error) {
                    record({kind: "advance-error", category: state.category ? state.category.title : "",
                        status: "failed", error: exceptionText(error)});
                    returnToMenu();
                }
            }
            if (state.captureEnabled && status === "rendered") {
                var originalCase = state.caseRecord;
                originalCase.original_comparison_elapsed_ms = now() - originalCase.started_at_ms;
                originalCase.visual_dwell_seconds = 2.0;
                // Preserve the original timed comparison. Visual-only cases
                // then run another two seconds for their exported screenshot.
                layer.scheduleOnce(function () {
                    afterDraw(function () {
                        if (state.caseRecord !== originalCase || originalCase.complete) return;
                        finishComparison();
                    });
                }, 2.0);
                return;
            }
            finishComparison();
        }
        BaseTestLayer.prototype.endTest = function (dt) {
            var layer = this;
            if (state.category) {
                // Label, OpenGL and other original comparisons read pixels.
                // This Vulkan/Metal frame owns its swapchain image until
                // presentation. Compare after real drawing, before endFrame.
                if (layer.__v3ComparisonQueued) return;
                layer.__v3ComparisonQueued = true;
                var caseRecord = state.caseRecord;
                var listener = cc.eventManager.addCustomListener("director_after_draw", function () {
                    cc.eventManager.removeListener(listener);
                    if (state.caseRecord !== caseRecord || !caseRecord || caseRecord.complete) return;
                    if (state.caseRecord) state.caseRecord.comparison_phase = "director_after_draw";
                    compareOriginal(layer, dt);
                });
                return;
            }
            compareOriginal(layer, dt);
        };
        // These original TileMap layers copy the shared props onto cc.Layer.
        // Capture their actual shared ctor inside this same lexical scope and
        // keep cc.Layer as their native parent.
        BaseTestLayerProps.ctor = function () {
            if (!state.registry) startCase(this);
            try { return originalSharedCtor.apply(this, arguments); }
            catch (error) { clearCardinalTimingObserver(); throw error; }
        };
        BaseTestLayerProps.endTest = BaseTestLayer.prototype.endTest;
    }
    function installControllerHook() {
        if (typeof TestController !== "function" || TestController.__v3RecordingInstalled) return;
        TestController.__v3RecordingInstalled = true;
        var originalEnter = TestController.prototype.onEnter;
        TestController.prototype.onEnter = function () {
            originalEnter.apply(this, arguments);
            if (state.validationVariant === "corrected" && !this.getChildByName("__v3ValidationVariant")) {
                var badge = new cc.LabelTTF("CORRECTED VALIDATION: " + state.validationRevision, "Arial", 12);
                badge.setName("__v3ValidationVariant");
                badge.setAnchorPoint(0, 1);
                badge.setPosition(4, cc.director.getWinSize().height - 3);
                badge.setColor(cc.color(255, 180, 40));
                this.addChild(badge, 100000);
            }
            if (state.category) state.nextCategory = true;
        };
    }
    function installAdvanceHooks() {
        if (typeof BaseTestLayer !== "function") return;
        var names = Object.getOwnPropertyNames(global);
        for (var i = 0; i < names.length; ++i) {
            var type;
            try { type = global[names[i]]; } catch (error) { continue; }
            if (typeof type !== "function" || !type.prototype ||
                !BaseTestLayer.prototype.isPrototypeOf(type.prototype) ||
                !Object.prototype.hasOwnProperty.call(type.prototype, "onNextCallback")) continue;
            var callback = type.prototype.onNextCallback;
            if (typeof callback !== "function" || callback.__v3ExceptionRecording) continue;
            type.prototype.onNextCallback = (function (original) {
                var wrapped = function () {
                    var previous = state.caseRecord;
                    try {
                        var result = original.apply(this, arguments);
                        if (state.category && previous && previous.complete && state.caseRecord === previous &&
                            state.caseLayer === this && this.isRunning()) {
                            // FontTest's original next callback replaces its
                            // labels in the same layer instead of creating a
                            // new BaseTestLayer. Continue its declared timer
                            // for the new original entry without editing its
                            // callback, title, expected value or comparison.
                            startCase(this);
                            this.__v3ComparisonQueued = false;
                            this.scheduleOnce(this.endTest, this.testDuration);
                            record({kind: "automation-reschedule", category: state.category.title,
                                type: typeOf(this), number: numberOf(this),
                                declared_duration_seconds: this.testDuration});
                        }
                        return result;
                    }
                    catch (error) {
                        clearCardinalTimingObserver();
                        if (state.caseRecord && !state.caseRecord.complete) completeCase("failed", exceptionText(error));
                        else if (state.category) record({kind: "case", category: state.category.title,
                            source: state.category.linksrc || "", number: numberOf(this),
                            type: "ConstructionFailure", title: "Original next-case construction failed",
                            status: "failed", frames: 0, error: exceptionText(error)});
                        // Original runNextTest catches construction failures and
                        // advances recursively. Record the failed attempt before
                        // preserving that original control flow.
                        throw error;
                    }
                };
                wrapped.__v3ExceptionRecording = true;
                return wrapped;
            })(callback);
        }
    }
    function platformMask() {
        var mask = PLATFORM_JSB;
        if (cc.sys.os === cc.sys.OS_ANDROID) mask |= PLATFROM_ANDROID;
        else if (cc.sys.os === cc.sys.OS_IOS) mask |= PLATFROM_IOS;
        else if (cc.sys.os === cc.sys.OS_OSX) mask |= PLATFORM_MAC;
        return mask;
    }
    function currentController() {
        var scene = cc.director.getRunningScene();
        if (!scene || !scene.isRunning()) return null;
        var children = scene.getChildren();
        for (var i = 0; i < children.length; ++i)
            if (children[i] instanceof TestController) return children[i];
        return null;
    }
    function finishCategory(detail) {
        clearCardinalTimingObserver();
        if (state.caseRecord && !state.caseRecord.complete)
            completeCase("needsInteraction", detail || "Original case returned to its main menu without a recorded assertion.");
        if (state.category) record({kind: "category-end", category: state.category.title});
        asyncGate.endCategory();
        state.category = null;
        state.caseRecord = null;
        state.caseLayer = null;
        state.nextCategory = false;
        if (state.nested && state.nested.restore) state.nested.restore();
        state.nested = null;
        if (state.registry && state.registry.restore) state.registry.restore();
        state.registry = null;
        if (state.layoutAuditStarted) {
            global.originalUILayoutAuditFinish();
            state.layoutAuditStarted = false;
        }
    }
    function startManualCase(description) {
        if (!state.category) return;
        state.previousCaseType = state.caseRecord ? state.caseRecord.type : null;
        state.caseLayer = null;
        if (state.caseRecord && !state.caseRecord.complete)
            completeCase("needsInteraction", "Original scene advanced before a comparison was available.");
        state.caseRecord = {kind: "case", category: state.category.title,
            source: description.source || state.category.linksrc || "", number: description.number,
            type: description.type, title: description.title, path: description.path || "",
            started_at_ms: now(), started_frame: state.frame, complete: false};
        asyncGate.bindCase(state.caseRecord);
        record({kind: "case-start", category: state.caseRecord.category, source: state.caseRecord.source,
            number: state.caseRecord.number, type: state.caseRecord.type, title: state.caseRecord.title,
            path: state.caseRecord.path});
        state.lastProgress = now();
    }
    function launchCategory() {
        var controller = currentController();
        if (!controller) throw new Error("Original TestController is not the running native menu.");
        var items = controller._itemMenu.getChildren();
        for (var i = 0; i < testNames.length; ++i) {
            if (testNames[i] === state.category) {
                items[i].activate();
                return;
            }
        }
        throw new Error("Original category is absent from its native menu.");
    }
    function startCategory() {
        if (state.index >= state.categories.length) {
            state.finished = true;
            asyncGate.restore();
            record({kind: "summary", counts: state.counts, error_count: state.errorCount,
                categories_needing_interaction: state.categoriesNeedingInteraction,
                attempted_categories: state.categories.length,
                source_revision: "e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f"});
            return;
        }
        state.category = state.categories[state.index++];
        state.categoryStarted = state.lastProgress = now();
        state.previousCaseType = null;
        state.caseRecord = null;
        state.caseLayer = null;
        state.nextCategory = false;
        asyncGate.beginCategory(state.category);
        autoTestCurrentTestName = state.category.title;
        record({kind: "category-start", category: state.category.title, source: state.category.linksrc || ""});
        try {
            if (state.sidecar) {
                var observer = state.sidecarDescription = sidecarDescription(state.sidecar);
                if (!observer || state.category.title !== observer.category)
                    throw Error("Observer requires its corresponding original category.");
                require(observer.script);
                if (observer.afterLoadScripts)
                    observer.afterLoadScripts.forEach(function (script) { require(script); });
                autoTestEnabled = false;
                var options = observer.options;
                if (observer.traceOnly) {
                    var entry = state.entryFilter ? Number(state.entryFilter) : 0;
                    var declaredEntry = observer.entryIndices ? observer.entryIndices.indexOf(entry) : entry;
                    if (!isFinite(entry) || entry % 1 || entry < 0 || declaredEntry < 0 ||
                        declaredEntry >= observer.entryTypes.length)
                        throw Error("Observer entry is outside its unchanged original registry.");
                    observer.number = entry;
                    observer.type = observer.entryTypes[declaredEntry];
                    options = {caseIndex: entry, timeout: state.sidecarDuration + 30, samplePeriod: observer.samplePeriod || 500};
                }
                if (observer.part) options.part = observer.part;
                if (observer.runIdentity) {
                    options.run_nonce = state.runNonce;
                    options.process_id = state.processId;
                }
                if (state.serviceConfig === "ui-scrollbar-lifecycle-r1") {
                    if (state.serviceConfigTransport !== "plain" || state.filter !== "UI Test" ||
                        state.sidecar !== "uiScrollOriginal" || state.entryFilter !== "0")
                        throw Error("UI scrollbar lifecycle options require their exact original observer selection.");
                    options.scrollBarLifecycle = true;
                }
                if (state.sidecar === "chipmunkCallbackGC") options.retiredGC = true;
                if (observer.serviceRequired) {
                    if (!state.serviceConfig) throw Error("Original network observer requires an explicit service configuration.");
                    options = {mode: observer.options && observer.options.mode, serviceConfig: state.serviceConfig};
                    if (observer.assetsSceneFromConfig) {
                        var assetsConfig = JSON.parse(state.serviceConfig);
                        var assetsIndex = assetsConfig.scene_index === undefined ? 0 : assetsConfig.scene_index;
                        if (typeof assetsIndex !== "number" || assetsIndex % 1 || assetsIndex < 0 || assetsIndex > 3)
                            throw Error("Assets observer requires original scene_index 0..3.");
                        observer.number = assetsIndex;
                        observer.type = "AMTestScene" + (assetsIndex + 1);
                    }
                    if (observer.modeFromEntry) {
                        if (state.entryFilter !== "WebSocketTest" && state.entryFilter !== "SocketIOTest")
                            throw Error("Original network extension entry must be WebSocketTest or SocketIOTest.");
                        options.mode = state.entryFilter;
                        observer.type = state.entryFilter;
                    }
                    if (state.sidecar === "originalSysScript") {
                        var scriptIndex = arrayOfSysTest.indexOf(ScriptTestLayer);
                        if (scriptIndex < 0) throw Error("Original ScriptTestLayer is absent from the native Sys registry.");
                        observer.number = scriptIndex;
                    }
                }
                startManualCase(observer);
                state.sidecarStarted = true;
                state.sidecarStartedAt = now();
                global[observer.start](options);
                return;
            }
            if (state.registryEnabled) {
                state.registry = V3RegistryNavigation(state.category, {
                    begin: startManualCase, complete: completeCase,
                    ready: function (key, callback) { return asyncGate.defer(key, callback); },
                    attach: function (layer) { state.caseLayer = layer; },
                    rename: function (title) { if (state.caseRecord) state.caseRecord.title = title; },
                    active: function () { return state.caseRecord && !state.caseRecord.complete; },
                    record: function (value) { value.category = state.category.title; record(value); },
                    fail: function (error) {
                        if (state.caseRecord && !state.caseRecord.complete) completeCase("failed", exceptionText(error));
                        else record({kind: "registry-error", category: state.category.title,
                            status: "failed", error: exceptionText(error)});
                    }, menu: returnToMenu
                });
            }
            state.nested = V3NestedNavigation(state.category, {
                begin: startManualCase, complete: completeCase, record: function (value) {
                    value.category = state.category.title;
                    record(value);
                }, rename: function (title) { if (state.caseRecord) state.caseRecord.title = title; },
                fail: function (error) {
                    if (state.caseRecord && state.caseRecord.complete && state.caseRecord.status === "failed") {
                        record({kind: "diagnostic", category: state.category.title,
                            number: state.caseRecord.number, type: state.caseRecord.type,
                            error: exceptionText(error)});
                        return;
                    }
                    if (!state.caseRecord || state.caseRecord.complete)
                        startManualCase({number: null, type: "OriginalNativeFactoryFailure", title: state.nested.path(),
                            path: state.nested.path()});
                    completeCase("failed", exceptionText(error));
                }, active: function () { return state.caseRecord && !state.caseRecord.complete; },
                baseFlow: function () {
                    return state.caseRecord && state.caseRecord.from_base &&
                        (!state.nested || state.caseRecord.path === state.nested.path());
                },
                captureEnabled: function () { return state.captureEnabled; },
                entryFilter: function () { return state.entryFilter; },
                launch: launchCategory, menu: returnToMenu
            });
            launchCategory();
        } catch (error) {
            record({kind: "category-error", category: state.category.title, status: "failed", error: exceptionText(error)});
            if (state.sidecarStarted) {
                // A throwing start must not leave the monitor polling a
                // sidecar that never initialized. Preserve the real failure
                // and let the existing menu/summary path terminate the run.
                state.sidecarDone = true;
                try {
                    if (observer && typeof global[observer.finish] === "function") global[observer.finish]();
                } catch (cleanupError) {
                    record({kind: "diagnostic", category: state.category.title,
                        phase: "failed observer startup cleanup", error: exceptionText(cleanupError)});
                }
                completeCase("failed", exceptionText(error));
            }
            if (state.registry) state.registry.onException();
            else returnToMenu();
        }
    }
    function configureValidationVariant(option) {
        var variant = String(option("AXMOL_JS_V3_VALIDATION") || "original");
        if (variant !== "original")
            throw Error("This application ships the original v3 suite; unsupported validation variant: " + variant);
        state.validationVariant = "original";
        state.validationRevision = "";
        state.validationSources = {};
        state.validationLoadedSources = {};
    }
    global.jsV3Configure = function (filter, continuation) {
        state.filter = filter || "";
        var option = typeof v3Option === "function" ? v3Option : function () { return ""; };
        configureValidationVariant(option);
        state.entryFilter = String(option("AXMOL_JS_V3_ENTRY") || "");
        state.registryEnabled = String(option("AXMOL_JS_V3_REGISTRY") || "").charAt(0) === "1";
        state.sidecar = String(option("AXMOL_JS_V3_SIDECAR") || "");
        state.serviceConfig = String(option("AXMOL_JS_V3_SERVICE_CONFIG") || "");
        state.runNonce = String(option("AXMOL_JS_V3_RUN_NONCE") || "");
        if (state.runNonce && !/^[A-Za-z0-9_-]{1,96}$/.test(state.runNonce))
            throw Error("Original-suite run nonce contains unsupported characters.");
        if (state.serviceConfig.indexOf("uri:") === 0) {
            state.serviceConfig = decodeURIComponent(state.serviceConfig.substring(4));
            state.serviceConfigTransport = "uri";
        }
        var duration = String(option("AXMOL_JS_V3_OBSERVER_SECONDS") || "");
        if (duration) {
            state.sidecarDuration = Number(duration);
            if (!isFinite(state.sidecarDuration) || state.sidecarDuration < 30 || state.sidecarDuration > 600)
                throw Error("Observer duration must be between 30 and 600 seconds.");
        }
        if (cc.sys.os === cc.sys.OS_ANDROID)
        {
            state.entryFilter = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeOption",
                "(Ljava/lang/String;)Ljava/lang/String;", "AXMOL_JS_V3_ENTRY");
            state.registryEnabled = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeRequested",
                "(Ljava/lang/String;)Z", "AXMOL_JS_V3_REGISTRY");
            state.sidecar = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeOption",
                "(Ljava/lang/String;)Ljava/lang/String;", "AXMOL_JS_V3_SIDECAR");
            state.serviceConfig = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeOption",
                "(Ljava/lang/String;)Ljava/lang/String;", "AXMOL_JS_V3_SERVICE_CONFIG");
            state.runNonce = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeOption",
                "(Ljava/lang/String;)Ljava/lang/String;", "AXMOL_JS_V3_RUN_NONCE");
            state.processId = jsb.reflection.callStaticMethod("android/os/Process", "myPid", "()I");
            if (state.runNonce && !/^[A-Za-z0-9_-]{1,96}$/.test(state.runNonce))
                throw Error("Original-suite run nonce contains unsupported characters.");
            if (state.serviceConfig.indexOf("uri:") === 0) {
                state.serviceConfig = decodeURIComponent(state.serviceConfig.substring(4));
                state.serviceConfigTransport = "uri";
            }
            var duration = jsb.reflection.callStaticMethod("dev/axmol/app/AppActivity", "jsSmokeOption",
                "(Ljava/lang/String;)Ljava/lang/String;", "AXMOL_JS_V3_OBSERVER_SECONDS");
            if (duration) {
                state.sidecarDuration = Number(duration);
                if (!isFinite(state.sidecarDuration) || state.sidecarDuration < 30 || state.sidecarDuration > 600)
                    throw Error("Observer duration must be between 30 and 600 seconds.");
            }
        }
        if (state.sidecar)
            throw Error("Specialized original-suite observers are not bundled: " + state.sidecar);
        if (state.serviceConfig)
            throw Error("Service configurations require an external observer and are not supported by this sample.");
        state.layoutAuditEnabled = false;
        record({kind: continuation ? "configuration-resumed" : "configuration", category_filter: state.filter,
            entry_filter: state.entryFilter, registry_navigation: state.registryEnabled,
            sidecar: state.sidecar, observer_duration_seconds: state.sidecarDuration,
            run_nonce: state.runNonce, process_id: state.processId,
            service_config_transport: state.serviceConfigTransport,
            service_config_supplied: !!state.serviceConfig,
            service_config_received: state.serviceConfig,
            validation_manifest: state.validationManifest,
            source_revision: "e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f",
            original_project_scripts: 125});
    };
    global.jsV3ApplyProfile = function (requested) {
        if (requested && requested !== "physical" && requested !== "legacy-ui-800x450")
            throw new Error("Unknown original-suite diagnostic profile: " + requested);
        var before = cc.director.getWinSize();
        if (requested === "physical") {
            cc.view.setDesignResolutionSize(AXMOL_V3_PHYSICAL_SIZE.width, AXMOL_V3_PHYSICAL_SIZE.height, 0);
            global.winSize = cc.director.getWinSize();
        } else if (requested === "legacy-ui-800x450") {
            // Original WebView controls use x=670 and position the view around 800/2.
            // Record this separate layout configuration; retain default-profile failures.
            cc.view.setDesignResolutionSize(800, 450, 0);
            global.winSize = cc.director.getWinSize();
        }
        var actualView = v3ViewState();
        record({kind: "render-profile", name: requested || "original",
            original_onStart_canvas: before, canvas: cc.director.getWinSize(),
            physical_framebuffer: AXMOL_V3_PHYSICAL_SIZE,
            resolution_policy: actualView.resolution_policy,
            viewport: actualView.viewport, native_scale: actualView.scale,
            detail: requested === "physical" ?
                "Additional no-design-scaling diagnostic after original onStart. Original assertions and raw GL coordinates remain unchanged; original-profile failures are retained separately." :
                requested === "legacy-ui-800x450" ?
                "Explicit 800x450 legacy UI layout diagnostic with unchanged original controls and callbacks. Default 600x450 and physical-profile results are retained separately." :
                "Unchanged original project onStart resolution; GL reads actual physical framebuffer coordinates."});
    };
    global.jsV3InstallLoaderHooks = function () {
        global.require = function (path) {
            var selected = state.validationVariant === "corrected" && state.validationSources[path];
            var actualArguments = arguments;
            if (selected) {
                actualArguments = Array.prototype.slice.call(arguments);
                actualArguments[0] = selected.resolved_path;
            }
            var value = originalRequire.apply(this, actualArguments);
            if (selected && !state.validationLoadedSources[path]) {
                state.validationLoadedSources[path] = true;
                record({kind: "validation-source-loaded", logical_source: path,
                    executed_source: selected.resolved_path,
                    declared_original_sha256: selected.manifest_entry.original_sha256,
                    declared_corrected_sha256: selected.manifest_entry.corrected_sha256,
                    sha_scope: "Declared manifest identities, independently verified against APK bytes by the host audit."});
            }
            if (/BaseTestLayer\/BaseTestLayer\.js$/.test(path)) installBaseHooks();
            if (/src\/tests-main\.js$/.test(path)) installControllerHook();
            installAdvanceHooks();
            return value;
        };
        return 0;
    };
    global.jsV3HandleException = function (message) {
        if (asyncGate.ownsErrors()) {
            asyncGate.reportError(message);
            return;
        }
        // A native Next callback can construct the next Layer during an old
        // Scene's Scheduler tick. That old Scene may still finish its update
        // before Director enters the pending Scene. Keep that diagnostic with
        // its phase instead of falsely failing the newly constructed entry.
        var constructionStack = /(?:\.ctor@|_ctor@|\.init@|\.runThisTest@)/.test(message);
        var previousType = state.previousCaseType;
        var previousUpdate = previousType &&
            (message.indexOf(previousType + "<.update@") >= 0 || message.indexOf(previousType + ".update@") >= 0);
        if (state.sidecar === "openglOriginal" && state.sidecarStarted && !state.sidecarDone && !constructionStack &&
            message.indexOf("src/OpenGLTest/OpenGLTest.js:995") >= 0 &&
            message.indexOf("TypeError: sprite is null") >= 0 && typeof ShaderRetroEffect === "function") {
            var running = cc.director.getRunningScene();
            var retro = running && running.getChildren().filter(function (node) {
                return node instanceof ShaderRetroEffect;
            })[0];
            if (retro && jsb.isObjectValid(retro) && retro.label && retro.label.getString() === "RETRO EFFECT") {
                // Preserve the unchanged original failure and its final failed
                // category result. Allow the observer's native Next to collect
                // the remaining GL entries; do not replace the original update.
                state.caseRecord.reported_native_update_error = message;
                record({kind: "native-callback-error", category: state.category.title,
                    number: 1, type: "ShaderRetroEffect", status: "failed",
                    phase: "original Retro update null space glyph; native observation and navigation continue",
                    classification_basis: "Exact original source line and live original Retro layer/text; no constructor stack or other runtime error is exempted.",
                    error: message});
                return;
            }
        }
        if ((state.sidecar === "polygon" || state.sidecar === "polygonNavigation") &&
            state.sidecarStarted && !state.sidecarDone && constructionStack &&
            message.indexOf("this._epsilonLabel is null") >= 0 &&
            message.indexOf("SpritePolygonTestSlider<.changeEpsilon@src/SpritePolygonTest/") >= 0 &&
            message.indexOf("ccui.Slider.prototype.setPercent@script/jsb_legacy_ui.js") >= 0 &&
            message.indexOf("withTarget/<@script/jsb_legacy.js") >= 0) {
            // The original Slider setter reports this callback exception, clears
            // it, and continues its constructor. Keep the failure and the actual
            // observer trace; automated recovery must not change this scene.
            state.caseRecord.reported_native_callback_error = message;
            record({kind: "native-callback-error", category: state.category.title,
                number: state.caseRecord.number, type: state.caseRecord.type, status: "failed",
                phase: "original Polygon constructor callback; genuine-input trace continues", error: message});
            return;
        }
        if (state.sidecar === "uiVideoOriginal" && state.sidecarStarted && !state.sidecarDone &&
            state.sidecarDescription.traceOnly && !constructionStack &&
            typeof global.uiVideoPlayerOriginalErrorWitness === "function") {
            var originalVideoFailure = global.uiVideoPlayerOriginalErrorWitness(message);
            if (originalVideoFailure) {
                // Fixed v3 Text.setString also requires exactly one argument. Preserve
                // the original SeekTo callback's error; it never reaches native seek.
                // Continue this trace without harness-driven replacement of the scene.
                state.caseRecord.reported_native_callback_error = message;
                record({kind:"native-callback-error", category:state.category.title,
                    number:state.caseRecord.number, type:state.caseRecord.type, status:"failed",
                    phase:"original Video SeekTo two-argument Text.setString; trace continues",
                    witness:originalVideoFailure, native_seek_executed:false, error:message});
                return;
            }
        }
        if (state.sidecar === "billboardOriginal" && state.sidecarStarted && !state.sidecarDone &&
            state.sidecarDescription.traceOnly && !constructionStack &&
            message.indexOf("Error: Native object is no longer valid") >= 0 &&
            (/BillBoardTest<\.menuCallback_oriented(?:Plane|Point)@src\/BillBoardTest\/BillBoardTest\.js:/.test(message) ||
             /^src\/BillBoardTest\/BillBoardTest\.js:(?:302|308): Error: Native object is no longer valid$/.test(message)) &&
            typeof BillBoardTest === "function") {
            var billboardScene = cc.director.getRunningScene();
            var billboardLayer = billboardScene && billboardScene.getChildren().filter(function (node) {
                return node instanceof BillBoardTest;
            })[0];
            var originalPeers = billboardLayer && billboardLayer._billboards;
            var expiredPeers = 0;
            if (originalPeers instanceof Array) {
                for (var billboardIndex = 0; billboardIndex < originalPeers.length; ++billboardIndex)
                    if (!jsb.isObjectValid(originalPeers[billboardIndex])) expiredPeers++;
            }
            if (billboardLayer && originalPeers === BillBoardTest.prototype._billboards &&
                jsb.isObjectValid(billboardLayer) && expiredPeers > 0) {
                // SM33's direct reporter supplies filename:line and no stack.
                // The two exact locations above are the unchanged original
                // Point/Plane setMode calls. The shared prototype array retains
                // expired peers; require that exact array, the real live layer
                // and expired peers.
                // Keep the actual failed callback and continue only this trace;
                // completeCase preserves failed status and native48 at the end.
                state.caseRecord.reported_native_callback_error = message;
                state.caseRecord.reported_native_callback_scope =
                    "Reported original BillBoard Point/Plane callback reached an expired shared-array peer; original controls and natural frames were observed without altering the array, and the callback failure remains preserved.";
                record({kind: "native-callback-error", category: state.category.title,
                    number: 1, type: "BillBoardTest", status: "failed",
                    phase: "original BillBoard mode callback reached an expired shared-array peer; trace continues",
                    original_shared_array_length: originalPeers.length, expired_native_peers: expiredPeers,
                    classification_basis: "Exact invalid-native error and unchanged original Point/Plane method stack or direct reporter location (302/308), live original layer, original prototype array identity and actual expired native peers; no handler or array is changed.",
                    error: message});
                return;
            }
        }
        if (constructionStack && message.indexOf("withTarget/<@script/jsb_legacy.js") >= 0 &&
            state.registry && state.caseRecord && !state.caseRecord.complete && state.caseLayer &&
            jsb.isObjectValid(state.caseLayer)) {
            // The controlled native callback reports and clears its exception,
            // then the original constructor continues. The application delivers
            // this report after the factory returned a live layer. Preserve a
            // failed result, but collect that layer's real state/rendered frames
            // before normal Scheduler navigation; do not cancel its updates.
            state.caseRecord.reported_native_callback_error = message;
            record({kind: "native-callback-error", category: state.category.title,
                number: state.caseRecord.number, type: state.caseRecord.type, status: "failed",
                phase: "original constructor callback reported; native factory returned live layer", error: message});
            return;
        }
        if (!constructionStack && previousUpdate && state.registry && state.caseLayer && jsb.isObjectValid(state.caseLayer)) {
            var caseScene = state.caseLayer instanceof cc.Scene ? state.caseLayer : state.caseLayer.getParent();
            if (caseScene && caseScene !== cc.director.getRunningScene()) {
                record({kind: "diagnostic", category: state.category.title,
                    number: state.caseRecord ? state.caseRecord.number : null,
                    phase: "previous Scene update before pending Scene entry", previous_case_type: previousType,
                    classification_basis: "Stack identifies previous entry update, contains no constructor, and new case belongs to pending Scene.",
                    error: message});
                return;
            }
        }
        completeCase("failed", message);
        record({kind: "runtime-error", category: state.category ? state.category.title : "bootstrap",
            number: state.caseRecord ? state.caseRecord.number : null,
            type: state.caseRecord ? state.caseRecord.type : null,
            status: "failed", error: message});
        if (state.prepared && state.category && !state.recoveryPending) {
            // A native callback can produce several diagnostics in one
            // frame. Drain them against this one original case, then advance
            // once on the next monitor tick. Advancing inside each diagnostic
            // would wrongly attribute the remaining errors to later cases.
            state.recoveryPending = {category: state.category, layer: state.caseLayer,
                nested: state.nested, registry: state.registry};
            if (state.caseLayer) {
                try { state.caseLayer.unschedule(state.caseLayer.endTest); } catch (expired) {}
                try { state.caseLayer.unscheduleUpdate(); } catch (expired) {}
                try { state.caseLayer.unscheduleAllCallbacks(); } catch (expired) {}
            }
        }
    };
    global.jsV3MenuSmokeResult = function () {
        var controller = currentController();
        if (!controller) return 1;
        var items = controller._itemMenu.getChildren();
        if (items.length !== testNames.length) {
            record({kind: "menu", status: "failed", expected: testNames.length, actual: items.length});
            return 45;
        }
        if (!state.menuRecorded) {
            var mask = platformMask(), enabled = 0;
            for (var i = 0; i < testNames.length; ++i)
                if (testNames[i].platforms & mask) enabled++;
            record({kind: "menu", native_scene_running: true, item_count: items.length,
                upstream_native_enabled: enabled, upstream_platform_disabled: items.length - enabled});
            state.menuRecorded = true;
        }
        return 0;
    };
    global.jsV3PrepareAutorun = function () {
        if (typeof testNames === "undefined" || !Array.isArray(testNames)) return 46;
        // This is an application-window flag. It expires with this test app
        // and lets a long native suite finish without changing device policy.
        cc.Device.setKeepScreenOn(true);
        var mask = platformMask();
        for (var i = 0; i < testNames.length; ++i) {
            var category = testNames[i];
            if (state.filter && state.filter !== category.title) continue;
            if (!(category.platforms & mask)) {
                record({kind: "category", category: category.title, status: "notApplicable",
                    detail: "Disabled by the unchanged upstream native platform mask."});
            } else state.categories.push(category);
        }
        if (!state.categories.length) return 47;
        autoTestEnabled = true;
        state.prepared = true;
        return 0;
    };
    global.jsV3RestartSmokeTick = function () {
        state.frame++;
        if (state.errorCount) return 50;
        if (state.restartCompleted) {
            var menuResult = jsV3MenuSmokeResult();
            if (menuResult !== 0) return menuResult;
            record({kind: "case", category: "Sys Tests", type: "RestartGameLayerTest",
                source: "src/SysTest/SysTest.js", status: "asserted",
                detail: "Original restartGame menu activation rebuilt the native VM and original running menu."});
            return 0;
        }
        if (state.restartRequested) return 1;
        var controller = currentController();
        if (controller && !state.restartEntered) {
            for (var i = 0; i < testNames.length; ++i) {
                if (testNames[i].title === "Sys Tests") {
                    state.restartEntered = true;
                    controller._itemMenu.getChildren()[i].activate();
                    return 1;
                }
            }
            return 50;
        }
        var scene = cc.director.getRunningScene();
        if (scene !== state.scene) { state.scene = scene; state.sceneFrames = 0; }
        else state.sceneFrames++;
        if (!scene || !scene.isRunning() || state.sceneFrames < 6) return 1;
        var children = scene.getChildren();
        for (var n = 0; n < children.length; ++n) {
            var layer = children[n];
            if (!(layer instanceof SysTestBase)) continue;
            if (layer instanceof RestartGameLayerTest) {
                var layerChildren = layer.getChildren();
                for (var m = 0; m < layerChildren.length; ++m) {
                    if (!(layerChildren[m] instanceof cc.Menu)) continue;
                    var items = layerChildren[m].getChildren();
                    for (var k = 0; k < items.length; ++k) {
                        var label = items[k].getLabel ? items[k].getLabel() : null;
                        if (label && label.getString && label.getString() === "restartGame") {
                            record({kind: "interaction-start", category: "Sys Tests", type: "RestartGameLayerTest",
                                source: "src/SysTest/SysTest.js", action: "original native restartGame menu activation"});
                            state.restartRequested = true;
                            items[k].activate();
                            return 1;
                        }
                    }
                }
                return 50;
            }
            layer.onNextCallback(layer);
            return 1;
        }
        return 1;
    };
    global.jsV3AutorunTick = function () {
        state.frame++;
        asyncGate.tick();
        if (asyncGate.isStopped()) return 48;
        if (state.recoveryPending) {
            if (!asyncGate.defer("error-recovery", function () {})) return 1;
            var recovery = state.recoveryPending;
            state.recoveryPending = null;
            if (recovery.category !== state.category) return 1;
            try {
                var recoveryScene = cc.director.getRunningScene();
                if (!recoveryScene) throw new Error("Recovery has no running native Scene.");
                // Error recovery owns the same next-scene construction as
                // normal advancement. Queue it in the running Scene's actual
                // Scheduler rather than creating draw commands after a frame
                // has rendered and before its autorelease pool is cleared.
                recoveryScene.scheduleOnce(function () {
                    if (recovery.category !== state.category) return;
                    try {
                        if (recovery.registry) recovery.registry.onException();
                        else if (recovery.nested) recovery.nested.onException();
                        else if (recovery.layer && typeof recovery.layer.runNextTest === "function")
                            recovery.layer.runNextTest();
                        else returnToMenu();
                    } catch (error) {
                        record({kind: "advance-error", category: state.category.title,
                            status: "failed", error: exceptionText(error)});
                        returnToMenu();
                    }
                }, 0);
            } catch (error) {
                record({kind: "advance-error", category: state.category.title,
                    status: "failed", error: exceptionText(error)});
                returnToMenu();
            }
            return 1;
        }
        if (state.finished) {
            if (state.counts.failed || state.errorCount) return 48;
            if (state.counts.rendered || state.counts.needsInteraction || state.categoriesNeedingInteraction) return 49;
            return 0;
        }
        var scene = cc.director.getRunningScene();
        if (scene !== state.scene) {
            state.scene = scene;
            state.sceneFrames = 0;
        } else state.sceneFrames++;
        if (!state.category) {
            if (jsV3MenuSmokeResult() === 0 && state.sceneFrames >= 2) startCategory();
            return 1;
        }
        if (state.sidecarStarted && !state.sidecarDone) {
            var observer = state.sidecarDescription;
            var observed = global[observer.tick]();
            if (!observed && observer.traceOnly && now() - state.sidecarStartedAt >= state.sidecarDuration * 1000)
                observed = global[observer.finish]();
            if (!observed) return 1;
            var observedPassed = observed.status === "PASS" || observed.status === "FILES_REQUESTED_PIXELS_PENDING" ||
                observed.status === "FILES_REQUESTED_PLATFORM_EVIDENCE_PENDING" ||
                observed.status === "FUNCTIONAL_PASS_IMAGES_PENDING" && observed.passed === true;
            var renderedObservation = observer.renderOnly && (observed.status === "FILES_REQUESTED_PIXELS_PENDING" ||
                observed.status === "FILES_REQUESTED_SYSTEM_IME_CHECK_PENDING" ||
                state.sidecar === "inputEditBoxDisabled" && observed.mode === "disabled-editbox" &&
                observed.caseIndex === 3 && observed.native_functional_checks === true && !observed.error &&
                observed.status === "FILES_REQUESTED_DISABLED_EDITBOX_SCREEN_CHECK_PENDING");
            if (renderedObservation) observedPassed = false;
            var traceRecorded = observer.traceOnly && observed.status === "TRACE_RECORDED_FUNCTIONAL_ANALYSIS_PENDING";
            var sourceContractRecorded = observer.sourceContract &&
                observed.status === "SOURCE_CONTRACT_FILES_REQUESTED_PIXELS_PENDING" && observed.native_source_contract === true;
            record({kind: "interaction-result", category: state.category.title, number: state.caseRecord.number,
                result: observed, native_functional_checks: observedPassed,
                native_source_contract_checks: sourceContractRecorded,
                platform_hardware_checks_pending: observed.status === "FILES_REQUESTED_PLATFORM_EVIDENCE_PENDING",
                external_functional_analysis_pending: traceRecorded,
                visual_pixel_checks_pending: observedPassed || traceRecorded || sourceContractRecorded || renderedObservation});
            state.caseRecord.functional_scope = observer.scope;
            state.caseRecord.platform_hardware_checks_pending = observed.status === "FILES_REQUESTED_PLATFORM_EVIDENCE_PENDING";
            state.caseRecord.external_functional_analysis_pending = traceRecorded;
            state.caseRecord.visual_pixel_checks_pending = observedPassed || traceRecorded || sourceContractRecorded || renderedObservation;
            if (renderedObservation) state.caseRecord.original_comparison = observed.original_comparison;
            if (sourceContractRecorded) {
                state.caseRecord.native_source_contract_checks = true;
                state.caseRecord.original_comparison = observed.original_comparison;
            }
            if (observed.hard_terminal_single_case === true) {
                if (state.sidecar !== "schedulerOriginal" || state.category.title !== "Scheduler Test" ||
                    state.caseRecord.number !== 4 || state.caseRecord.type !== "SchedulerUnscheduleAllHard" ||
                    !traceRecorded || observed.index !== 4 || observed.type !== "SchedulerUnscheduleAllHard" ||
                    observed.upstream_navigation_limitation !== true || observed.original_navigation_verified !== false)
                    throw Error("Terminal Scheduler observation requires the original Hard entry and explicit navigation limitation.");
                // Its original MainMenu already started a Transition after
                // global cancellation removed ActionManager. EventDispatcher
                // stays disabled, so no generic after_draw capture or second
                // navigation is possible. Preserve the four earlier natural
                // observer images and finish this single recorded scope.
                state.caseRecord.terminal_without_after_draw = true;
                state.caseRecord.upstream_navigation_limitation = true;
                state.caseRecord.original_navigation_verified = false;
                state.caseRecord.comparison_phase = "script-update-frame-terminal-observation";
                completeCase("rendered", JSON.stringify(observed));
                state.sidecarDone = true;
                record({kind: "terminal-observation", category: state.category.title, number: 4,
                    type: state.caseRecord.type, native_monitor_entry: "script-update-frame",
                    original_transition_preserved: true, generic_after_draw_capture_requested: false,
                    automatic_return_to_menu_requested: false, original_navigation_verified: false});
                finishCategory();
                startCategory();
                return 1;
            }
            completeCase(observedPassed ? "asserted" : traceRecorded || sourceContractRecorded || renderedObservation ? "rendered" : "failed", JSON.stringify(observed));
            state.sidecarDone = true;
            returnToMenu();
            return 1;
        }
        if (state.nextCategory && currentController()) {
            if (!asyncGate.defer("category-navigation", function () {})) return 1;
            state.nextCategory = false;
            if (state.nested && state.nested.onMainMenu()) return 1;
            finishCategory();
            startCategory();
            return 1;
        }
        if (state.nested && !state.nested.done) {
            state.nested.tick();
            return 1;
        }
        if (state.registry && !state.registry.done) {
            state.registry.tick();
            return 1;
        }
        // Menu-only/interactive categories do not derive from BaseTestLayer.
        // Preserve that distinction instead of pretending their visible menu
        // validated all nested tests. Subsequent native menu interaction can
        // run them through their original callback paths.
        if (!state.caseRecord && now() - state.categoryStarted > (state.captureEnabled ? 2500 : 1500)) {
            if (state.captureEnabled || state.category.title === "XMLHttpRequest" ||
                state.category.title === "XMLHttpRequest send ArrayBuffer") {
                startManualCase({number: 0, type: "OriginalNativeCategoryScene", title: state.category.title});
                completeCase("rendered", "Original first native scene ran frames; remaining cases and interaction are not verified.");
                return 1;
            }
            record({kind: "category", category: state.category.title, status: "needsInteraction",
                detail: "Rendered native category menu; original nested interaction is still required."});
            returnToMenu();
        } else if (state.caseRecord && state.caseRecord.complete &&
            state.caseRecord.type === "OriginalNativeCategoryScene") {
            record({kind: "category", category: state.category.title, status: "needsInteraction",
                detail: (state.caseRecord.capture ? "Captured" : "Observed") +
                    " the first original native scene; remaining cases and interaction are still required."});
            returnToMenu();
        } else if (!asyncGate.pendingCount() && now() - state.lastProgress > 30000) {
            completeCase("needsInteraction", "Original case did not finish within 30 seconds.");
            record({kind: "category-timeout", category: state.category.title, status: "failed",
                error: "No case progress within 30 seconds."});
            returnToMenu();
        }
        return 1;
    };
    // This passive observer publishes its result from its real after-draw
    // listener. Its public source contract intentionally has no Tick method.
    global.jsV3TickOriginalBillBoardProbe = function () {
        return global.OriginalBillBoardProbeResult || null;
    };
    global.V3SuiteState = state;
})(this);

