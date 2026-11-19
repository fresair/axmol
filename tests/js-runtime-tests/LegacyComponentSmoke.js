/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function (global) {
    function check(value, message) { if (!value) throw Error('ComponentJS: ' + message); }
    var first = new cc.ComponentJS('LegacyComponentModule.js');
    var second = cc.ComponentJS.create('LegacyComponentModule.js');
    var owner = new cc.Node(), other = new cc.Node();
    first.setName('script'); second.setName('script');
    check(first.getScriptObject() === first && second.getScriptObject() === second, 'canonical script object');
    check(first instanceof cc.Component && second instanceof cc.ComponentJS, 'native and script prototype bases');
    check(LegacyComponentModuleLoads === 2 && LegacyComponentCtorCalls === 0, 'cached script reexecutes without calling JS ctor');
    check(owner.addComponent(first) && other.addComponent(second), 'real native component attach');
    check(owner.getComponent('script') === first && first.getOwner() === owner, 'lookup and owner identity');
    check(first.adds === 1 && first.enters === 0, 'attached before owner enters');
    global.__LegacyComponentScene.addChild(owner);
    global.__LegacyComponentScene.addChild(other);
    jsb.garbageCollect();
    global.LegacyComponentProbe = {scene: global.__LegacyComponentScene, owner: owner, other: other,
        first: first, second: second,
        verify: function (stage) {
            check(first.getScriptObject() === first && second.getScriptObject() === second, 'identity after GC');
            if (stage === 'updated') {
                check(first.enters === 1 && second.enters === 1 && first.total === 0.375 && second.total === 0.375 &&
                    first.updates === 2 && second.updates === 2, 'real Scheduler updates and instance fields');
            } else if (stage === 'paused') {
                check(first.total === 0.375 && second.total === 0.5 && first.updates === 2 && second.updates === 3,
                    'Node pause skips only its components');
            } else if (stage === 'resumed') {
                check(first.total === 0.5 && second.total === 0.625, 'Node resume restores updates');
            } else if (stage === 'exited') {
                check(first.exits === 1 && second.exits === 1 && first.total === 0.5 && second.total === 0.625,
                    'Scene exit callbacks and Scheduler pause');
            } else if (stage === 'detached') {
                check(first.removes === 1 && first.getOwner() === null && owner.getComponent('script') === null,
                    'real native detach clears owner');
                check(first.total === 0.5 && second.total === 0.875, 'removed component receives no owner update');
            } else if (stage === 'reattached') {
                check(first.adds === 2 && first.enters === 3 && first.getOwner() === owner && first.total === 0.75,
                    'reattach to running Node and existing instance fields');
                check(second.total === 1.125, 'other component continues');
            } else throw Error('unknown component stage ' + stage);
        }};
})(this);
