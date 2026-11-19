// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const {EventEmitter} = require('node:events');
const {spawn} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const {RemoteClient} = require('./protocol');

const slash = value => String(value || '').replace(/\\/g, '/').replace(/^file:\/\//, '');

class DebugSession extends EventEmitter {
    constructor(write) {
        super();
        this.write = write;
        this.sequence = 1;
        this.config = {};
        this.client = null;
        this.child = null;
        this.thread = 'thread0';
        this.frames = new Map();
        this.references = new Map();
        this.referenceIds = new Map();
        this.sourceReferences = new Map();
        this.sourceIds = new Map();
        this.breakpoints = new Map();
        this.breakpointIds = new Map();
        this.nextReference = 1;
        this.nextFrame = 1;
        this.nextSource = 1;
        this.nextBreakpoint = 1;
        this.paused = false;
        this.configuring = false;
        this.ended = false;
        this.exceptionFilters = [];
        this.exception = null;
    }
    send(message) { this.write({...message, seq: this.sequence++}); }
    event(event, body) { this.send({type: 'event', event, body}); }
    response(request, body = {}) { this.send({type: 'response', request_seq: request.seq, command: request.command, success: true, body}); }
    failure(request, error) {
        this.send({type: 'response', request_seq: request.seq, command: request.command, success: false,
            message: error.message, body: {error: {id: 1, format: error.message, showUser: true}}});
    }
    async handle(request) {
        try {
            const body = await this.dispatch(request.command, request.arguments || {});
            this.response(request, body || {});
            if (request.command === 'initialize') this.initialized = true;
            if (request.command === 'attach' || request.command === 'launch') this.event('initialized');
        } catch (error) { this.failure(request, error); }
    }
    async dispatch(command, args) {
        switch (command) {
        case 'initialize': return {
            supportsConfigurationDoneRequest: true, supportsConditionalBreakpoints: true,
            supportsEvaluateForHovers: true, supportsLoadedSourcesRequest: true,
            supportsExceptionInfoRequest: true, supportsTerminateRequest: true,
            exceptionBreakpointFilters: [{filter: 'all', label: 'All thrown exceptions', default: false},
                {filter: 'uncaught', label: 'Uncaught exceptions', default: false}]
        };
        case 'launch':
            if (!args.program) throw new Error('Launch requires a native executable in program');
            this.child = spawn(args.program, args.args || [], {
                cwd: args.cwd || args.sourceRoot || process.cwd(),
                env: {...process.env, AXMOL_JS_DEBUG: '1', AXMOL_JS_DEBUG_WAIT: '1', ...args.env},
                stdio: ['ignore', 'pipe', 'pipe']
            });
            this.child.stdout.on('data', data => this.event('output', {category: 'stdout', output: data.toString()}));
            this.child.stderr.on('data', data => this.event('output', {category: 'stderr', output: data.toString()}));
            this.child.on('error', error => {
                this.event('output', {category: 'stderr', output: `${error.message}\n`});
                if (this.client) this.client.close();
                this.terminateEvent();
            });
            this.child.on('exit', (code, signal) => {
                this.event('exited', {exitCode: code === null ? -1 : code});
                if (this.client) this.client.close();
                this.terminateEvent();
            });
            return this.attach(args);
        case 'attach': return this.attach(args);
        case 'configurationDone':
            await this.configureExceptions();
            await this.resume(this.config.stopOnEntry ? 'step' : null);
            this.configuring = false;
            return {};
        case 'setBreakpoints': return this.setBreakpoints(args);
        case 'setExceptionBreakpoints':
            this.exceptionFilters = args.filters || [];
            if (this.client) await this.configureExceptions();
            return {breakpoints: this.exceptionFilters.map(() => ({verified: true}))};
        case 'threads': return {threads: [{id: 1, name: 'Axmol JavaScript (Director thread)'}]};
        case 'continue': await this.resume(null); return {allThreadsContinued: true};
        case 'next': await this.resume('next'); return {};
        case 'stepIn': await this.resume('step'); return {};
        case 'stepOut': await this.resume('finish'); return {};
        case 'pause': await this.remote(this.thread, 'interrupt'); return {};
        case 'stackTrace': return this.stackTrace(args);
        case 'scopes': return this.scopes(args);
        case 'variables': return this.variables(args);
        case 'evaluate': return this.evaluate(args);
        case 'source': {
            const actor = this.sourceReferences.get(args.sourceReference);
            if (!actor) throw new Error('Unknown source reference');
            const result = await this.remote(actor, 'source');
            return {content: result.source, mimeType: 'text/javascript'};
        }
        case 'loadedSources': {
            const result = await this.remote(this.thread, 'sources');
            return {sources: result.sources.map(source => this.source(source))};
        }
        case 'exceptionInfo': return {exceptionId: this.display(this.exception), description: this.display(this.exception),
            breakMode: this.exceptionFilters.includes('all') ? 'always' : 'unhandled'};
        case 'disconnect': await this.disconnect(args.terminateDebuggee === true); return {};
        case 'terminate': await this.disconnect(true); return {};
        default: throw new Error(`Unsupported DAP request: ${command}`);
        }
    }
    async attach(args) {
        if (this.client) throw new Error('A target is already attached');
        this.config = {...args, sourceRoot: path.resolve(args.sourceRoot || args.cwd || process.cwd())};
        this.configuring = true;
        this.client = new RemoteClient(this.config);
        this.client.on('packet', packet => this.onPacket(packet));
        this.client.on('transportError', error => this.event('output', {category: 'stderr', output: `${error.message}\n`}));
        this.client.on('closed', () => this.terminateEvent());
        try {
            await this.client.connect();
            const list = await this.remote('root', 'listTabs');
            const tab = list.tabs[list.selected || 0];
            if (!tab) throw new Error('Axmol debugger has no game global');
            const attached = await this.remote(tab.actor, 'attach');
            this.thread = attached.threadActor;
            await this.remote(this.thread, 'attach');
        } catch (error) {
            this.client.close(); this.client = null;
            if (this.child) this.child.kill();
            throw error;
        }
        return {};
    }
    remote(actor, type, params = {}) {
        if (!this.client) return Promise.reject(new Error('No attached target'));
        return this.client.request(actor, type, params);
    }
    onPacket(packet) {
        if (packet.type === 'paused') {
            this.paused = true;
            this.invalidatePause();
            const why = packet.why || {};
            this.exception = why.exception || null;
            if (why.type !== 'attached' && why.type !== 'alreadyPaused') {
                const reasons = {breakpoint: 'breakpoint', resumeLimit: 'step', exception: 'exception',
                    debuggerStatement: 'breakpoint', interrupted: 'pause'};
                this.event('stopped', {reason: reasons[why.type] || 'pause', threadId: 1, allThreadsStopped: true,
                    description: why.type === 'exception' ? this.display(why.exception) : undefined,
                    hitBreakpointIds: (why.actors || []).map(actor => this.breakpointIds.get(actor)).filter(Boolean)});
            }
        } else if (packet.type === 'resumed') {
            this.paused = false; this.invalidatePause();
            if (!this.configuring) this.event('continued', {threadId: 1, allThreadsContinued: true});
        } else if (packet.type === 'breakpointResolved') {
            const id = this.breakpointIds.get(packet.actor);
            if (id) this.event('breakpoint', {reason: 'changed', breakpoint: {id, verified: true, line: packet.location.line}});
        } else if (packet.type === 'newSource') {
            this.event('loadedSource', {reason: 'new', source: this.source(packet.source)});
        } else if (packet.type === 'debuggerError') {
            this.event('output', {category: 'stderr', output: `Debugger: ${packet.message}\n${packet.stack || ''}\n`});
        }
    }
    invalidatePause() { this.frames.clear(); this.references.clear(); this.referenceIds.clear(); }
    async configureExceptions() {
        await this.remote(this.thread, 'reconfigure', {options: {
            pauseOnExceptions: this.exceptionFilters.length > 0,
            ignoreCaughtExceptions: !this.exceptionFilters.includes('all') && this.exceptionFilters.includes('uncaught')
        }});
    }
    async resume(limit) {
        await this.remote(this.thread, 'resume', limit ? {resumeLimit: {type: limit}} : {});
    }
    toRemote(local) {
        const normalized = slash(path.resolve(local));
        const mappings = Object.entries(this.config.sourceMap || {}).sort((a, b) => b[1].length - a[1].length);
        for (const [remote, root] of mappings) {
            const prefix = slash(path.resolve(root)).replace(/\/$/, '');
            if (normalized === prefix || normalized.startsWith(`${prefix}/`)) return slash(remote).replace(/\/$/, '') + normalized.slice(prefix.length);
        }
        const relative = slash(path.relative(this.config.sourceRoot, normalized));
        return this.config.remoteRoot ? slash(this.config.remoteRoot).replace(/\/$/, '') + '/' + relative : relative;
    }
    toLocal(remote) {
        const normalized = slash(remote);
        const mappings = Object.entries(this.config.sourceMap || {}).sort((a, b) => b[0].length - a[0].length);
        if (this.config.remoteRoot) mappings.push([slash(this.config.remoteRoot), this.config.sourceRoot]);
        for (const [root, local] of mappings) {
            const prefix = slash(root).replace(/\/$/, '');
            if (normalized === prefix || normalized.startsWith(`${prefix}/`))
                return path.resolve(local, normalized.slice(prefix.length).replace(/^\//, ''));
        }
        return path.isAbsolute(normalized) ? normalized : path.resolve(this.config.sourceRoot, normalized);
    }
    source(source) {
        if (!source) return undefined;
        let reference = this.sourceIds.get(source.actor);
        if (!reference) { reference = this.nextSource++; this.sourceIds.set(source.actor, reference); this.sourceReferences.set(reference, source.actor); }
        const local = this.toLocal(source.url);
        return {name: path.basename(slash(source.url)) || '(eval)', path: local,
            sourceReference: fs.existsSync(local) ? 0 : reference};
    }
    async setBreakpoints(args) {
        if (!args.source || !args.source.path) throw new Error('Breakpoint requires a source path');
        const remote = this.toRemote(args.source.path);
        for (const bp of this.breakpoints.get(remote) || []) {
            await this.remote(bp.actor, 'delete'); this.breakpointIds.delete(bp.actor);
        }
        const entries = [];
        const results = [];
        for (const requested of args.breakpoints || []) {
            if (requested.hitCondition || requested.logMessage) {
                results.push({verified: false, line: requested.line, message: 'Hit conditions and logpoints are not supported'});
                continue;
            }
            const response = await this.remote(this.thread, 'setBreakpoint', {
                location: {url: remote, line: requested.line}, condition: requested.condition
            });
            const id = this.nextBreakpoint++;
            entries.push({actor: response.actor, id}); this.breakpointIds.set(response.actor, id);
            results.push({id, verified: !response.isPending, line: response.actualLocation.line,
                message: response.isPending ? 'Pending until the script is loaded' : undefined});
        }
        this.breakpoints.set(remote, entries);
        return {breakpoints: results};
    }
    async stackTrace(args) {
        this.requirePaused();
        const response = await this.remote(this.thread, 'frames', {start: args.startFrame || 0, count: args.levels || 1000});
        const stackFrames = response.frames.map(frame => {
            const id = this.nextFrame++; this.frames.set(id, frame);
            return {id, name: frame.displayName || '(anonymous)', line: frame.where ? frame.where.line : 1,
                column: frame.where ? frame.where.column + 1 : 1, source: frame.where ? this.source(frame.where.source) : undefined};
        });
        return {stackFrames, totalFrames: response.totalFrames};
    }
    ref(kind, actor, extra) {
        if (!actor) return 0;
        const key = `${kind}:${actor}`;
        let id = this.referenceIds.get(key);
        if (!id) { id = this.nextReference++; this.referenceIds.set(key, id); this.references.set(id, {kind, actor, ...extra}); }
        return id;
    }
    scopes(args) {
        this.requirePaused();
        const frame = this.frames.get(args.frameId);
        if (!frame) throw new Error('Frame expired; request a new stack trace');
        const scopes = [{name: 'this', variablesReference: this.ref('this', frame.actor, {value: frame.this}), expensive: false}];
        for (let env = frame.environment, depth = 0; env; env = env.parent, ++depth) {
            scopes.push({name: env.type === 'declarative' ? depth ? 'Closure' : 'Local' : env.type === 'object' ? 'Global' : env.type,
                variablesReference: this.ref(env.type === 'object' || env.type === 'with' ? 'object' : 'environment',
                    env.object ? env.object.actor : env.actor), expensive: env.type === 'object'});
        }
        return {scopes};
    }
    display(value) {
        if (value === null || value === undefined) return String(value);
        if (typeof value === 'string') return JSON.stringify(value);
        if (typeof value !== 'object') return String(value);
        if (value.type === 'object') return value.name ? `${value.class} ${value.name}` : value.class || 'Object';
        if (value.type === 'unavailable') return value.optimizedOut ? '<optimized out>' : '<unavailable>';
        return value.type || 'Object';
    }
    variable(name, value) {
        return {name, value: this.display(value), type: value && value.type === 'object' ? value.class : typeof value,
            variablesReference: value && value.actor && value.type === 'object' ? this.ref('object', value.actor) : 0};
    }
    async variables(args) {
        this.requirePaused();
        const reference = this.references.get(args.variablesReference);
        if (!reference) throw new Error('Variable reference expired');
        let variables;
        if (reference.kind === 'this') variables = [this.variable('this', reference.value)];
        else if (reference.kind === 'environment') {
            const response = await this.remote(reference.actor, 'bindings');
            const bindings = response.bindings;
            const entries = {...bindings.variables};
            for (const arg of bindings.arguments || []) Object.assign(entries, arg);
            variables = Object.keys(entries).map(name => this.variable(name, entries[name].value));
        } else {
            const response = await this.remote(reference.actor, 'prototypeAndProperties');
            variables = Object.entries(response.ownProperties).map(([name, descriptor]) => {
                if ('value' in descriptor) return this.variable(name, descriptor.value);
                return {name, value: '<accessor>', variablesReference: 0,
                    presentationHint: {kind: 'property', attributes: ['readOnly'], lazy: false}};
            });
            if (response.prototype && response.prototype.type !== 'null') variables.push(this.variable('[[Prototype]]', response.prototype));
        }
        if (args.filter === 'indexed') variables = variables.filter(variable => /^\d+$/.test(variable.name));
        else if (args.filter === 'named') variables = variables.filter(variable => !/^\d+$/.test(variable.name));
        const start = args.start || 0;
        return {variables: variables.slice(start, args.count ? start + args.count : undefined)};
    }
    async evaluate(args) {
        this.requirePaused();
        const frame = args.frameId === undefined ? null : this.frames.get(args.frameId);
        if (args.frameId !== undefined && !frame) throw new Error('Evaluation frame expired');
        const response = await this.remote(this.thread, 'clientEvaluate', {expression: args.expression, frame: frame && frame.actor});
        if (response.exception) throw new Error(`Evaluation threw ${this.display(response.exception)}`);
        const result = this.variable('', response.result);
        return {result: result.value, type: result.type, variablesReference: result.variablesReference};
    }
    requirePaused() { if (!this.paused) throw new Error('JavaScript thread is running'); }
    async disconnect(terminate) {
        if (this.client) {
            try { await this.remote(this.thread, 'detach'); } catch (_) {}
            this.client.close(); this.client = null;
        }
        if (terminate && this.child) this.child.kill();
        this.terminateEvent();
    }
    terminateEvent() { if (!this.ended) { this.ended = true; this.event('terminated'); } }
}

function runStdio(input = process.stdin, output = process.stdout) {
    const session = new DebugSession(message => {
        const json = Buffer.from(JSON.stringify(message), 'utf8');
        output.write(`Content-Length: ${json.length}\r\n\r\n`); output.write(json);
    });
    let bytes = Buffer.alloc(0), chain = Promise.resolve();
    input.on('data', chunk => {
        bytes = Buffer.concat([bytes, chunk]);
        try {
            for (;;) {
                const end = bytes.indexOf('\r\n\r\n');
                if (end < 0) { if (bytes.length > 8192) throw new Error('DAP header too large'); break; }
                const header = bytes.subarray(0, end).toString('ascii');
                const match = /^Content-Length:\s*(\d+)\s*$/im.exec(header);
                if (!match) throw new Error('Missing DAP Content-Length');
                const length = Number(match[1]);
                if (!length || length > 8 * 1024 * 1024) throw new Error('Invalid DAP message length');
                if (bytes.length < end + 4 + length) break;
                const message = JSON.parse(bytes.subarray(end + 4, end + 4 + length).toString('utf8'));
                bytes = bytes.subarray(end + 4 + length);
                if (message.type === 'request') chain = chain.then(() => session.handle(message));
            }
        } catch (error) {
            session.event('output', {category: 'stderr', output: `DAP framing error: ${error.message}\n`});
            session.disconnect(false).finally(() => { input.destroy(); });
        }
    });
    input.on('end', () => session.disconnect(false));
    return session;
}

if (require.main === module) runStdio();
module.exports = {DebugSession, runStdio};
