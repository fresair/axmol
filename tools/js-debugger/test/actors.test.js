// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const {PacketDecoder} = require('../src/protocol');

const actorScript = fs.readFileSync(path.resolve(__dirname, '../../../extensions/scripting/js-bindings/script/jsb_debugger.js'), 'utf8');

function createHarness() {
    let debuggerInstance;
    const source = {url: 'src/中文😀.js', text: 'var n = 1;\n\nn += 2;'};
    const handlers = new Map();
    const script = {source, url: source.url, getChildScripts: () => [],
        getLineOffsets: line => line === 1 ? [0] : line === 3 ? [6] : [],
        getAllOffsets: () => [undefined, [0], undefined, [6]],
        getOffsetLine: offset => offset === 0 ? 1 : 3,
        setBreakpoint: (offset, handler) => handlers.set(offset, handler),
        clearBreakpoint: handler => { for (const [offset, item] of handlers) if (item === handler) handlers.delete(offset); },
        isInCatchScope: () => false};
    class FakeDebugger {
        constructor() { debuggerInstance = this; this.scripts = []; }
        addDebuggee() { return {executeInGlobal: () => ({return: 7})}; }
        findScripts() { return this.scripts; }
        getNewestFrame() { return this.frame || null; }
        removeAllDebuggees() { this.removed = true; }
    }
    const messages = [];
    let startup = 0, nested = 0, nestedHandler = null;
    const context = vm.createContext({Debugger: FakeDebugger,
        _bufferWrite: data => new PacketDecoder(packet => messages.push(packet)).push(Buffer.from(data, 'utf8')),
        _debuggerStartupReady: () => ++startup,
        _getEventLoopNestLevel: () => nested,
        _enterNestedEventLoop: () => { ++nested; if (nestedHandler) nestedHandler(); },
        _exitNestedEventLoop: () => --nested});
    vm.runInContext(actorScript, context);
    context._prepareDebugger({});
    function request(to, type, params = {}) {
        const packet = {...params, to, type, _requestId: messages.length + 1};
        context.processInput(Buffer.from(JSON.stringify(packet), 'utf8').toString('latin1'));
        return messages.findLast(message => message._requestId === packet._requestId);
    }
    return {context, messages, source, script, handlers, request,
        get dbg() { return debuggerInstance; }, get startup() { return startup; }, get nested() { return nested; },
        onNested(callback) { nestedHandler = callback; }};
}

test('connect keeps startup wait, configure-before-load breakpoint slides and resumes nested pause', () => {
    const h = createHarness();
    h.context.processInput('connected');
    assert.equal(h.startup, 0, 'TCP connect must not release the pre-main.js wait');
    assert.equal(h.messages[0].applicationType, 'axmol');
    h.request('thread0', 'attach');
    const bp = h.request('thread0', 'setBreakpoint', {location: {url: h.source.url, line: 2}});
    assert.equal(bp.isPending, true);
    h.request('thread0', 'resume');
    assert.equal(h.startup, 1);
    h.dbg.onNewScript(h.script);
    const resolved = h.messages.find(packet => packet.type === 'breakpointResolved');
    assert.equal(resolved.location.line, 3);
    assert.equal(resolved.location.url, h.source.url);
    let evaluated = false;
    const environment = {type: 'declarative', parent: null, names: () => ['n'], getVariable: () => 3};
    const frame = {type: 'call', script: h.script, offset: 6, environment, older: null, this: undefined,
        callee: {class: 'Function', name: 'tick'}, eval: expression => { evaluated = expression === 'n + 4'; return {return: 7}; }};
    h.onNested(() => {
        const frames = h.request('thread0', 'frames');
        assert.equal(frames.frames[0].where.line, 3);
        const bindings = h.request(frames.frames[0].environment.actor, 'bindings');
        assert.equal(bindings.bindings.variables.n.value, 3);
        const result = h.request('thread0', 'clientEvaluate', {expression: 'n + 4', frame: frames.frames[0].actor});
        assert.equal(result.result, 7);
        h.request('thread0', 'resume');
        assert.equal(h.request(frames.frames[0].actor, 'evaluate', {expression: 'n'}).error, 'noSuchActor');
    });
    h.handlers.get(6).hit(frame);
    assert.equal(evaluated, true);
    assert.equal(h.nested, 0);
});

test('disconnect removes breakpoints and stepping, releases startup and allows reconnect', () => {
    const h = createHarness(); h.context.processInput('connected');
    h.dbg.scripts = [h.script];
    h.request('thread0', 'attach');
    h.request('thread0', 'setBreakpoint', {location: {url: h.source.url, line: 1}});
    assert.equal(h.handlers.size, 1);
    h.request('thread0', 'resume', {resumeLimit: {type: 'step'}});
    assert.equal(typeof h.dbg.onEnterFrame, 'function');
    h.context.processInput('disconnected');
    assert.equal(h.handlers.size, 0);
    assert.equal(h.dbg.onEnterFrame, undefined);
    assert.equal(h.dbg.enabled, false);
    h.context.processInput('connected');
    assert.equal(h.request('thread0', 'attach').why.type, 'attached');
    h.context._shutdownDebugger();
    assert.equal(h.dbg.removed, true);
});

test('object inspection never executes getters and preserves a __proto__ property', () => {
    const h = createHarness(); h.context.processInput('connected'); h.request('thread0', 'attach');
    let getterCalls = 0;
    const object = {class: 'Object', proto: null, getOwnPropertyNames: () => ['__proto__', 'dangerous'],
        getOwnPropertyDescriptor: name => name === '__proto__' ? {value: 123, enumerable: true} :
            {get: {class: 'Function', name: 'get dangerous'}, set: undefined, enumerable: true}};
    h.dbg.frame = null;
    const saved = h.dbg.addDebuggee;
    // Create a new harness debuggee result via a normal paused frame evaluation.
    const env = {type: 'declarative', names: () => [], parent: null};
    const frame = {type: 'call', script: h.script, offset: 0, environment: env, older: null,
        this: object, callee: null, eval: () => { ++getterCalls; return {return: object}; }};
    h.request('thread0', 'resume');
    h.onNested(() => {
        const frames = h.request('thread0', 'frames');
        const response = h.request(frames.frames[0].this.actor, 'prototypeAndProperties');
        assert.equal(response.ownProperties.__proto__.value, 123);
        assert.equal(response.ownProperties.dangerous.get.class, 'Function');
        h.request('thread0', 'resume');
    });
    h.dbg.onDebuggerStatement(frame);
    assert.equal(getterCalls, 0);
});
