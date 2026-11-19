/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function (global) {
    function assertEq(actual, expected, label) {
        if (actual !== expected) {
            throw new Error('Es6PolyfillSmoke mismatch [' + label + ']: expected ' + expected + ', got ' + actual);
        }
    }

    // 1. Non-enumerability check (must not pollute for..in on Array or Object)
    var sampleArr = [10, 20];
    var enumArrKeys = [];
    for (var ak in sampleArr) enumArrKeys.push(ak);
    assertEq(enumArrKeys.join(','), '0,1', 'Array.prototype for..in cleanliness');

    var sampleObj = {alpha: 1};
    var enumObjKeys = [];
    for (var ok in sampleObj) enumObjKeys.push(ok);
    assertEq(enumObjKeys.join(','), 'alpha', 'Object for..in cleanliness');

    // 2. Symbol & iterator protocol
    assertEq(typeof Symbol, 'function', 'typeof Symbol');
    var s1 = Symbol('token');
    var s2 = Symbol('token');
    assertEq(s1 === s2, false, 'unique Symbol instances');
    var sharedA = Symbol.for('axmol.shared');
    var sharedB = Symbol.for('axmol.shared');
    assertEq(sharedA, sharedB, 'Symbol.for identity');
    assertEq(Symbol.keyFor(sharedA), 'axmol.shared', 'Symbol.keyFor lookup');
    assertEq(Boolean(Symbol.iterator && Symbol.toStringTag && Symbol.hasInstance), true, 'well-known Symbols');

    var iterOut = [];
    var arrIt = [7, 8, 9][Symbol.iterator]();
    var step;
    while (!(step = arrIt.next()).done) iterOut.push(step.value);
    assertEq(iterOut.join(','), '7,8,9', 'Array[Symbol.iterator]');

    var strChars = [];
    var strIt = 'A\uD83D\uDE00B'[Symbol.iterator]();
    while (!(step = strIt.next()).done) strChars.push(step.value);
    assertEq(strChars.length, 3, 'String[Symbol.iterator] astral codepoint count');
    assertEq(strChars[1], '\uD83D\uDE00', 'String[Symbol.iterator] surrogate pair');

    // 3. Object.assign, Object.values, Object.entries, Object.fromEntries, Object.is
    var merged = Object.assign({a: 1, b: 2}, null, undefined, {b: 20, c: 30}, {d: 40});
    assertEq(JSON.stringify(merged), '{"a":1,"b":20,"c":30,"d":40}', 'Object.assign');
    assertEq(Object.values({x: 3, y: 4}).join(','), '3,4', 'Object.values');
    assertEq(JSON.stringify(Object.entries({x: 3, y: 4})), '[["x",3],["y",4]]', 'Object.entries');
    var fromArr = Object.fromEntries([['k1', 11], ['k2', 22]]);
    assertEq(fromArr.k1 + ':' + fromArr.k2, '11:22', 'Object.fromEntries array');
    var mapIn = new Map();
    mapIn.set('m1', 101);
    mapIn.set('m2', 202);
    var fromMap = Object.fromEntries(mapIn);
    assertEq(fromMap.m1 + ':' + fromMap.m2, '101:202', 'Object.fromEntries Map');
    assertEq(Object.is(NaN, NaN), true, 'Object.is(NaN, NaN)');
    assertEq(Object.is(+0, -0), false, 'Object.is(+0, -0)');

    // 4. Array.prototype.includes, flat, flatMap
    assertEq([1, 2, NaN, 4].includes(NaN), true, 'Array.includes(NaN)');
    assertEq([1, 2, 3].includes(2, -2), true, 'Array.includes negative fromIndex hit');
    assertEq([1, 2, 3].includes(2, -1), false, 'Array.includes negative fromIndex miss');
    assertEq(JSON.stringify([1, [2, [3, [4]]]].flat()), '[1,2,[3,[4]]]', 'Array.flat(1)');
    assertEq(JSON.stringify([1, [2, [3, [4]]]].flat(2)), '[1,2,3,[4]]', 'Array.flat(2)');
    assertEq(
        JSON.stringify([1, 2, 3].flatMap(function (x) { return [x, x * 10]; })),
        '[1,10,2,20,3,30]',
        'Array.flatMap'
    );

    // 5. String.prototype.padStart, padEnd, trimStart, trimEnd
    assertEq('5'.padStart(3, '0'), '005', 'String.padStart');
    assertEq('abc'.padEnd(6, '12'), 'abc121', 'String.padEnd');
    assertEq('  hello  '.trimStart(), 'hello  ', 'String.trimStart');
    assertEq('  hello  '.trimEnd(), '  hello', 'String.trimEnd');

    // 6. Number constants & predicates
    assertEq(Number.isSafeInteger(Number.MAX_SAFE_INTEGER), true, 'Number.isSafeInteger max');
    assertEq(Number.isSafeInteger(Number.MAX_SAFE_INTEGER + 1), false, 'Number.isSafeInteger overflow');

    // 7. Promise & queueMicrotask asynchronous pipeline
    var state = {
        chainValue: 0,
        finallyCalled: 0,
        caughtReason: '',
        allValue: '',
        allSettledValue: '',
        raceValue: '',
        microtaskOrder: []
    };

    queueMicrotask(function () {
        state.microtaskOrder.push('qm1');
    });

    Promise.resolve(2)
        .then(function (v) {
            state.microtaskOrder.push('p1');
            return v * 3;
        })
        .then(function (v) {
            return new Promise(function (resolve) {
                resolve(v + 4);
            });
        })
        .finally(function () {
            state.finallyCalled++;
        })
        .then(function (v) {
            state.chainValue = v;
        });

    Promise.reject(new Error('expected_failure'))
        .catch(function (err) {
            return 'recovered:' + err.message;
        })
        .then(function (msg) {
            state.caughtReason = msg;
        });

    Promise.all([
        Promise.resolve('A'),
        'B',
        new Promise(function (resolve) {
            setTimeout(function () { resolve('C'); }, 10);
        })
    ]).then(function (arr) {
        state.allValue = arr.join('-');
    });

    Promise.allSettled([
        Promise.resolve(42),
        Promise.reject('bad_item')
    ]).then(function (items) {
        state.allSettledValue = items[0].status + ':' + items[0].value + '|' + items[1].status + ':' + items[1].reason;
    });

    Promise.race([
        new Promise(function (resolve) { setTimeout(function () { resolve('slow'); }, 50); }),
        Promise.resolve('fast')
    ]).then(function (winner) {
        state.raceValue = winner;
    });

    global.es6PolyfillSmokeVerify = function () {
        assertEq(state.chainValue, 10, 'Promise chain + nested Promise');
        assertEq(state.finallyCalled, 1, 'Promise.finally execution');
        assertEq(state.caughtReason, 'recovered:expected_failure', 'Promise.catch recovery');
        assertEq(state.allValue, 'A-B-C', 'Promise.all with setTimeout');
        assertEq(state.allSettledValue, 'fulfilled:42|rejected:bad_item', 'Promise.allSettled');
        assertEq(state.raceValue, 'fast', 'Promise.race');
        assertEq(state.microtaskOrder.join(','), 'qm1,p1', 'queueMicrotask + Promise order');
        return 0;
    };
})(this);
