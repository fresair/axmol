// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Actual native extension and lifetime checks; no graphics device is created.
(function () {
    function check(value,message) { if(!value)throw Error('Effect3D compatibility: '+message); }
    function rejects(fn,message) { var rejected=false;try{fn();}catch(error){rejected=true;}check(rejected,message); }
    check(typeof cc.EffectSprite3D==='function' && typeof cc.Effect3DOutline==='function','native extension classes');
    var outline=new cc.Effect3DOutline();
    outline.retain();
    check(outline.getOutlineWidth()===1,'original native default outline width');
    var color=outline.getOutlineColor();
    check(color.x===1 && color.y===1 && color.z===1,'original native default outline color');
    outline.setOutlineWidth(0.01);
    outline.setOutlineColor(cc.math.vec3(1,0,0));
    color=outline.getOutlineColor();
    check(Math.abs(outline.getOutlineWidth()-0.01)<0.000001 && color.x===1 && color.y===0 && color.z===0,
        'real native outline parameter storage');
    rejects(function(){cc.Effect3DOutline.prototype.setOutlineWidth.call({},0.02);},'plain native receiver rejected');
    var sprite=new cc.EffectSprite3D();
    sprite.retain();
    check(sprite instanceof cc.MeshRenderer,'actual MeshRenderer base prototype');
    sprite.setPosition(23,41);
    check(sprite.getPosition().x===23 && sprite.getPosition().y===41,'native inherited Node methods');
    rejects(function(){sprite.addEffect(outline,-1);},'outline cannot pretend an empty mesh was rendered');
    rejects(function(){sprite.addEffect(sprite,-1);},'wrong effect native type rejected');
    cc.sys.garbageCollect();
    check(Math.abs(outline.getOutlineWidth()-0.01)<0.000001 && sprite.getPosition().x===23,
        'explicitly retained native objects survive GC');
    var Derived=cc.EffectSprite3D.extend({ctor:function(){this._super();this.setPosition(7,9);}});
    var child=new Derived();
    check(child instanceof cc.EffectSprite3D && child.getPosition().x===7 && child.getPosition().y===9,
        'native allocation and subclass ctor run on the same receiver');
    child.retain();
    this.__effect3dReleased=outline;
    sprite.release();outline.release();child.release();
    cc.log('Effect3DCompatibilitySmoke: 11 native CPU checks passed; GPU rendering not asserted');
})();
