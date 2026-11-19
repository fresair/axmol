// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT

var HelloWorldScene = cc.Scene.extend({
    onEnter: function () {
        this._super();
        var size = cc.director.getWinSize();
        var logo = new cc.Sprite("res/HelloWorld.png");
        logo.setPosition(size.width / 2, size.height / 2);
        this.addChild(logo);
        var title = new cc.LabelTTF("Hello JavaScript · SpiderMonkey 33.1", "Arial", 30);
        title.setPosition(size.width / 2, size.height - 70);
        this.addChild(title);
    }
});
