// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
(function () {
    function assert(value, message) {
        if (!value) { throw new Error("JS binding smoke: " + message); }
    }
    assert(typeof cc === "object" && typeof jsb === "object", "binding namespaces");
    assert(typeof cc.Node === "function" && typeof cc.Scene === "function", "core constructors");

    var parent = new cc.Node();
    var child = new cc.Node();
    child.setName("identity-child");
    parent.addChild(child);
    assert(parent.getChildren()[0] === child, "stable native wrapper identity");
    assert(parent.getChildByName("identity-child") === child, "getter wrapper identity");
    child.setPosition(12, 34);
    var position = child.getPosition();
    assert(position.x === 12 && position.y === 34, "Vec2 input/output conversion");
    child.setScale(2);
    assert(child.getScale() === 2, "number conversion");
    child.retain();
    parent.removeChild(child, true);
    assert(child.getName() === "identity-child", "explicit retain survives detach");
    child.release();

    var Derived = cc.Node.extend({
        ctor: function () {
            this._super();
            this.setName("derived");
        },
        answer: function () { return 42; }
    });
    var derived = new Derived();
    assert(derived.getName() === "derived" && derived.answer() === 42, "extend and ctor super");

    assert(typeof ccui.Button === "function", "UI constructor");
    var widget = new ccui.Widget();
    widget.setName("widget-smoke");
    assert(widget.getName() === "widget-smoke", "UI native binding inheritance");
    assert(typeof cc.moveBy === "function" && cc.moveBy(1, cc.p(2, 3)), "action factory");
    assert(jsb.fileUtils.getStringFromFile("project.json").indexOf("jsList") !== -1, "file host interface");
    cc.log("JavaScript bindings smoke passed");
}());
function jsBindingSmokeResult() { return 0; }
