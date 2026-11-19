/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function (global) {
    var cc=global.cc,jsb=global.jsb;
    // Older SDKs have the generated PolygonInfo class but lack these legacy
    // entry points; no partial alias is installed until the native adapter exists.
    if(!cc||!jsb||!jsb.PolygonInfo||!cc.AutoPolygon||
       typeof cc.AutoPolygon.generatePolygon!=='function'||
       !cc.Sprite||typeof cc.Sprite.prototype.initWithPolygon!=='function')return;
    jsb.AutoPolygon=cc.AutoPolygon;
    cc.PolygonInfo=jsb.PolygonInfo;
    var prototype=cc.Sprite.prototype,previous=prototype.ctor;
    if(!previous.__legacyPolygonCtor) {
        var adapted=function (source) {
            if(source instanceof jsb.PolygonInfo) {
                if(arguments.length!==1)throw new TypeError('Sprite polygon constructor requires one PolygonInfo');
                return this.initWithPolygon(source);
            }
            return previous.apply(this,arguments);
        };
        adapted.__legacyPolygonCtor=true;prototype.ctor=adapted;
    }
})(this);
