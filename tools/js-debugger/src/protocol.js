// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const {EventEmitter} = require('node:events');
const net = require('node:net');
const MAX_PACKET = 8 * 1024 * 1024;

function encodePacket(packet) {
    const body = Buffer.from(JSON.stringify(packet), 'utf8');
    return Buffer.concat([Buffer.from(`${body.length}:`, 'ascii'), body]);
}

class PacketDecoder {
    constructor(onPacket) { this.buffer = Buffer.alloc(0); this.onPacket = onPacket; }
    push(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        for (;;) {
            const colon = this.buffer.indexOf(58);
            if (colon < 0) {
                if (this.buffer.length > 8) throw new Error('Invalid remote packet header');
                return;
            }
            const header = this.buffer.subarray(0, colon).toString('ascii');
            if (!/^\d{1,8}$/.test(header)) throw new Error('Invalid remote packet length');
            const length = Number(header);
            if (length <= 0 || length > MAX_PACKET) throw new Error('Remote packet too large or empty');
            if (this.buffer.length < colon + 1 + length) return;
            const body = this.buffer.subarray(colon + 1, colon + 1 + length).toString('utf8');
            this.buffer = this.buffer.subarray(colon + 1 + length);
            this.onPacket(JSON.parse(body));
        }
    }
}

class RemoteClient extends EventEmitter {
    constructor(options = {}) {
        super();
        this.options = options;
        this.nextId = 1;
        this.pending = new Map();
        this.socket = null;
        this.greeting = null;
        this.closing = false;
    }
    async connect() {
        const deadline = Date.now() + (this.options.timeout || 15000);
        let lastError;
        do {
            try { await this._connectOnce(Math.min(1000, Math.max(1, deadline - Date.now()))); return; }
            catch (error) { lastError = error; }
            if (this.closing) throw new Error('Debugger connection cancelled');
            if (Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 100));
        } while (Date.now() < deadline);
        throw new Error(`Cannot connect to Axmol debugger at ${this.options.host || '127.0.0.1'}:${this.options.port || 5086}: ${lastError.message}`);
    }
    _connectOnce(timeout) {
        return new Promise((resolve, reject) => {
            let settled = false, greeted = false;
            const socket = net.createConnection({host: this.options.host || '127.0.0.1', port: this.options.port || 5086});
            this.socket = socket;
            socket.setNoDelay(true);
            const timer = setTimeout(() => fail(new Error('No Axmol debugger greeting')), timeout);
            const fail = error => {
                if (!settled) { settled = true; clearTimeout(timer); socket.destroy(); reject(error); }
                else if (greeted) { this._rejectPending(error); this.emit('transportError', error); }
            };
            const decoder = new PacketDecoder(packet => {
                if (!settled) {
                    if (packet.from !== 'root' || !packet.applicationType) throw new Error('Invalid Axmol debugger greeting');
                    if (!packet.traits || !packet.traits.axmolRequestIds)
                        throw new Error('This adapter requires the Axmol SpiderMonkey actor server');
                    settled = true; greeted = true; clearTimeout(timer); this.greeting = packet; resolve();
                } else this._packet(packet);
            });
            socket.on('data', chunk => { try { decoder.push(chunk); } catch (error) { fail(error); socket.destroy(); } });
            socket.on('error', fail);
            socket.on('close', () => {
                if (!settled) fail(new Error('Server closed before greeting'));
                else if (greeted) {
                    this._rejectPending(new Error('Axmol debugger disconnected'));
                    if (this.socket === socket) this.socket = null;
                    this.emit('closed');
                }
            });
        });
    }
    _packet(packet) {
        const pending = this.pending.get(packet._requestId);
        if (pending) {
            this.pending.delete(packet._requestId); clearTimeout(pending.timer);
            if (packet.error) pending.reject(new Error(`${packet.error}: ${packet.message || ''}`));
            else pending.resolve(packet);
        }
        this.emit('packet', packet);
    }
    request(to, type, params = {}) {
        if (!this.socket || this.socket.destroyed) return Promise.reject(new Error('Debugger is disconnected'));
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`Debugger request timed out: ${to}/${type}`));
            }, this.options.requestTimeout || 30000);
            this.pending.set(id, {resolve, reject, timer});
            this.socket.write(encodePacket({...params, to, type, _requestId: id}), error => {
                if (!error) return;
                const pending = this.pending.get(id);
                if (pending) { this.pending.delete(id); clearTimeout(timer); reject(error); }
            });
        });
    }
    _rejectPending(error) {
        for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
        this.pending.clear();
    }
    close() {
        this.closing = true;
        if (this.socket) this.socket.destroy();
        this._rejectPending(new Error('Debugger client closed'));
    }
}

module.exports = {encodePacket, PacketDecoder, RemoteClient};
