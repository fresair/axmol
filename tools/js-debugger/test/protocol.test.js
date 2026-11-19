// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const {once} = require('node:events');
const {encodePacket, PacketDecoder, RemoteClient} = require('../src/protocol');

async function listenOrSkip(server, port, t) {
    server.listen(port, '127.0.0.1');
    try { await once(server, 'listening'); return true; }
    catch (error) {
        if (error.code !== 'EPERM' && error.code !== 'EACCES') throw error;
        t.skip(`Environment forbids loopback socket listening: ${error.code}`);
        return false;
    }
}

test('remote framing counts UTF-8 octets and tolerates every split and joined packets', () => {
    const packets = [{to: 'source1', url: 'src/中文😀.js'}, {from: 'root', message: 'é\n🚀'}];
    const bytes = Buffer.concat(packets.map(encodePacket));
    const received = [];
    const decoder = new PacketDecoder(packet => received.push(packet));
    for (const byte of bytes) decoder.push(Buffer.from([byte]));
    assert.deepEqual(received, packets);
    const joined = [];
    new PacketDecoder(packet => joined.push(packet)).push(bytes);
    assert.deepEqual(joined, packets);
});

test('remote framing rejects invalid headers and oversize bodies before buffering payload', () => {
    for (const bytes of ['x:{}', ':{}', '0:', '99999999:', '123456789'])
        assert.throws(() => new PacketDecoder(() => {}).push(Buffer.from(bytes)));
});

test('RemoteClient correlates out-of-order responses, rejects errors and expires timeouts', async t => {
    const sockets = new Set();
    const server = net.createServer(socket => {
        sockets.add(socket); socket.on('close', () => sockets.delete(socket));
        socket.write(encodePacket({from: 'root', applicationType: 'axmol', traits: {axmolRequestIds: true}}));
        const saved = [];
        const decoder = new PacketDecoder(request => {
            if (request.type === 'timeout') return;
            if (request.type === 'failure') {
                socket.write(encodePacket({from: request.to, _requestId: request._requestId, error: 'noSuchActor', message: 'expired'}));
                return;
            }
            saved.push(request);
            if (saved.length === 2) for (const packet of saved.reverse())
                socket.write(encodePacket({from: packet.to, _requestId: packet._requestId, value: packet.type}));
        });
        socket.on('data', chunk => decoder.push(chunk));
    });
    if (!await listenOrSkip(server, 0, t)) return;
    const client = new RemoteClient({port: server.address().port, requestTimeout: 40});
    t.after(() => { client.close(); for (const socket of sockets) socket.destroy(); server.close(); });
    await client.connect();
    const responses = await Promise.all([client.request('thread0', 'first'), client.request('thread0', 'second')]);
    assert.deepEqual(responses.map(response => response.value), ['first', 'second']);
    await assert.rejects(client.request('expired', 'failure'), /noSuchActor: expired/);
    await assert.rejects(client.request('thread0', 'timeout'), /timed out/);
});

test('connection retry does not emit a terminal close event before a successful greeting', async t => {
    const reservation = net.createServer();
    if (!await listenOrSkip(reservation, 0, t)) return;
    const port = reservation.address().port; await new Promise(resolve => reservation.close(resolve));
    const client = new RemoteClient({port, timeout: 1500});
    let closes = 0;
    client.on('closed', () => ++closes);
    const connected = client.connect();
    await new Promise(resolve => setTimeout(resolve, 150));
    const sockets = new Set();
    const server = net.createServer(socket => {
        sockets.add(socket);
        socket.write(encodePacket({from: 'root', applicationType: 'axmol', traits: {axmolRequestIds: true}}));
    });
    t.after(() => { client.close(); for (const socket of sockets) socket.destroy(); server.close(); });
    if (!await listenOrSkip(server, port, t)) { client.close(); return; }
    await connected;
    assert.equal(closes, 0);
});

test('RemoteClient request correlation and timeout operate on a memory transport', async () => {
    const client = new RemoteClient({requestTimeout: 20});
    const sent = [];
    const decoder = new PacketDecoder(packet => sent.push(packet));
    client.socket = {destroyed: false, write: (bytes, callback) => { decoder.push(bytes); callback(); }, destroy() { this.destroyed = true; }};
    const first = client.request('thread0', 'first'), second = client.request('thread0', 'second');
    client._packet({_requestId: sent[1]._requestId, from: 'thread0', value: 2});
    client._packet({_requestId: sent[0]._requestId, from: 'thread0', value: 1});
    assert.deepEqual((await Promise.all([first, second])).map(response => response.value), [1, 2]);
    const failure = client.request('expired', 'bindings');
    client._packet({_requestId: sent[2]._requestId, from: 'expired', error: 'noSuchActor', message: 'expired'});
    await assert.rejects(failure, /noSuchActor/);
    await assert.rejects(client.request('thread0', 'timeout'), /timed out/);
    const interrupted = client.request('thread0', 'never');
    client.close();
    await assert.rejects(interrupted, /closed/);
    assert.equal(client.pending.size, 0);
});
