/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Real native .extend construction failure and ordinary JS class return semantics. */
var constructorSmokeReport = (function () {
    var nativeFailed = false, calls = 0;
    var NativeFailure = cc.Node.extend({ctor: function () { ++calls; return false; }});
    try { new NativeFailure(); } catch (error) { nativeFailed = true; }
    if (!nativeFailed || calls !== 1) throw new Error('Native ctor=false did not reject native construction exactly once');
    var ParentFailure = cc.Node.extend({ctor: function () { ++calls; return false; }});
    var ChildFailure = ParentFailure.extend({ctor: function () { return this._super(); }});
    nativeFailed = false;
    try { new ChildFailure(); } catch (error) { nativeFailed = true; }
    if (!nativeFailed || calls !== 2) throw new Error('Native inherited ctor=false did not propagate failure');
    var PlainFailure = cc.Class.extend({ctor: function () { this.marker = 37; return false; }});
    var plain = new PlainFailure();
    if (plain.marker !== 37 || !(plain instanceof PlainFailure))
        throw new Error('Ordinary JS ctor=false changed JS construction semantics');
    cc.log('CONSTRUCTOR_SMOKE_PASS 3 checks');
    return {phase: 'native-constructor-failure-regression', passed: true, checks: 3};
})();

