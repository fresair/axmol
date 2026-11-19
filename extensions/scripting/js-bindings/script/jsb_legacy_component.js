/*
 * Copyright (c) 2015-2016 Chukong Technologies Inc.
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Axmol contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 * SPDX-License-Identifier: MIT
 * Contract adapted from cocos2d-x/v3 jsb_cocos2d.js:2838-2847, e4b6a5ef8f.
 */
(function () {
    var Native = cc.ComponentJS;
    if (typeof Native !== 'function') return;
    cc._ComponentJS = Native;
    Native.__plainClass = true;
    Native.extend = cc.Class.extend;
    function ComponentJS(filename) {
        return Native.create(filename).getScriptObject();
    }
    ComponentJS.prototype = Native.prototype;
    // Component files return a plain JS class/prototype. Native allocation
    // happens once in the loader, preserving the old no-JS-ctor contract.
    ComponentJS.__plainClass = true;
    ComponentJS.extend = cc.Class.extend;
    ComponentJS.create = function (filename) { return Native.create(filename); };
    cc.ComponentJS = ComponentJS;
})();
