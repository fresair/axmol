// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const {PassThrough} = require('node:stream');
const {DebugSession, runStdio} = require('../src/adapter');

function targetSession() {
    const messages = [], requests = [];
    const session = new DebugSession(message => messages.push(message));
    session.config = {sourceRoot: path.resolve('/local/game'), remoteRoot: '/remote/game'};
    session.client = {request: async (actor, type, params) => {
        requests.push({actor, type, params});
        if (type === 'setBreakpoint') return {actor: 'breakpoint1', isPending: true, actualLocation: {line: params.location.line}};
        if (type === 'frames') return {totalFrames: 1, frames: [{actor: 'frame1', displayName: 'tick',
            where: {line: 3, column: 0, source: {actor: 'source1', url: '/remote/game/src/app.js'}}, this: {type: 'null'},
            environment: {actor: 'env1', type: 'declarative', parent: {actor: 'env2', type: 'object', object: {actor: 'global1', type: 'object'}}}}]};
        if (type === 'bindings') return {bindings: {variables: {count: {value: 1}, object: {value: {type: 'object', actor: 'object1', class: 'Object'}}}}};
        if (type === 'prototypeAndProperties') return {ownProperties: {n: {value: 7}, secret: {get: {type: 'object'}}}, prototype: {type: 'null'}};
        if (type === 'clientEvaluate') return {result: params.expression === 'object' ? {type: 'object', actor: 'object1', class: 'Object'} : 42};
        if (type === 'resume') { session.onPacket({type: 'resumed'}); return {type: 'resumed'}; }
        return {};
    }, close() {}};
    return {session, messages, requests};
}

test('DAP translates pending breakpoints, source mappings and resolution notifications', async () => {
    const {session, messages, requests} = targetSession();
    const result = await session.dispatch('setBreakpoints', {source: {path: '/local/game/src/app.js'},
        breakpoints: [{line: 3, condition: 'count > 0'}]});
    assert.equal(result.breakpoints[0].verified, false);
    assert.deepEqual(requests[0].params, {location: {url: '/remote/game/src/app.js', line: 3}, condition: 'count > 0'});
    session.onPacket({type: 'breakpointResolved', actor: 'breakpoint1', location: {line: 4}});
    assert.equal(messages[0].body.breakpoint.verified, true);
    assert.equal(messages[0].body.breakpoint.line, 4);
    session.config.sourceMap = {'/remote/extra': '/local/extra'};
    assert.equal(session.toRemote('/local/extra/module.js'), '/remote/extra/module.js');
    assert.equal(session.toLocal('/remote/extra/module.js'), path.resolve('/local/extra/module.js'));
});

test('DAP stopped frames/scopes/variables/evaluation expire on resume and do not invoke getters', async () => {
    const {session, messages, requests} = targetSession();
    session.onPacket({type: 'paused', why: {type: 'breakpoint', actors: ['breakpoint1']}});
    const stack = await session.dispatch('stackTrace', {threadId: 1});
    assert.equal(stack.stackFrames[0].source.path, path.resolve('/local/game/src/app.js'));
    assert.equal(stack.stackFrames[0].column, 1);
    const scopes = await session.dispatch('scopes', {frameId: stack.stackFrames[0].id});
    assert.equal(scopes.scopes[1].name, 'Local');
    const vars = await session.dispatch('variables', {variablesReference: scopes.scopes[1].variablesReference});
    assert.equal(vars.variables[0].value, '1');
    const properties = await session.dispatch('variables', {variablesReference: vars.variables[1].variablesReference});
    assert.equal(properties.variables[1].value, '<accessor>');
    assert.equal(properties.variables[1].variablesReference, 0);
    const evaluation = await session.dispatch('evaluate', {expression: 'object', frameId: stack.stackFrames[0].id});
    assert.equal(evaluation.result, 'Object');
    assert.ok(evaluation.variablesReference > 0);
    assert.equal(requests.find(request => request.type === 'clientEvaluate').params.frame, 'frame1');
    await session.dispatch('next', {threadId: 1});
    assert.equal(requests.findLast(request => request.type === 'resume').params.resumeLimit.type, 'next');
    await assert.rejects(session.dispatch('evaluate', {expression: 'count', frameId: stack.stackFrames[0].id}), /running/);
    session.onPacket({type: 'paused', why: {type: 'resumeLimit'}});
    await assert.rejects(session.dispatch('variables', {variablesReference: scopes.scopes[1].variablesReference}), /expired/);
    assert.equal(messages.filter(message => message.event === 'stopped').length, 2);
});

test('DAP exceptions configure caught detection and startup resume-limit handles stopOnEntry', async () => {
    const {session, messages, requests} = targetSession();
    session.config.stopOnEntry = true; session.configuring = true;
    await session.dispatch('setExceptionBreakpoints', {filters: ['uncaught']});
    assert.deepEqual(requests[0].params.options, {pauseOnExceptions: true, ignoreCaughtExceptions: true});
    await session.dispatch('configurationDone', {});
    assert.equal(requests.findLast(request => request.type === 'resume').params.resumeLimit.type, 'step');
    session.onPacket({type: 'paused', why: {type: 'exception', exception: {type: 'object', class: 'Error', actor: 'err1'}}});
    assert.equal(messages.findLast(message => message.event === 'stopped').body.reason, 'exception');
    assert.equal((await session.dispatch('exceptionInfo', {})).breakMode, 'unhandled');
    await session.dispatch('disconnect', {});
    assert.equal(requests.at(-1).type, 'detach');
});

test('stdio DAP framing accepts fragmented UTF-8 request and reports unsupported requests', async () => {
    const input = new PassThrough(), output = new PassThrough();
    const received = [];
    output.on('data', chunk => received.push(chunk));
    const session = runStdio(input, output);
    const message = Buffer.from(JSON.stringify({seq: 10, type: 'request', command: 'unsupported中文', arguments: {}}));
    const bytes = Buffer.concat([Buffer.from(`Content-Length: ${message.length}\r\n\r\n`), message]);
    for (const byte of bytes) input.write(Buffer.from([byte]));
    await new Promise(resolve => setImmediate(resolve));
    const result = Buffer.concat(received);
    const end = result.indexOf('\r\n\r\n');
    const response = JSON.parse(result.subarray(end + 4).toString());
    assert.equal(response.request_seq, 10);
    assert.equal(response.success, false);
    assert.match(response.message, /unsupported中文/);
    input.end();
});
