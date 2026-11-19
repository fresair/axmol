// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    var checks=0;
    function expect(ok,message){++checks;if(!ok)throw Error('Legacy Node: '+message);}
    var parent=new cc.Node(), child=new cc.Node();parent.addChild(child);
    parent.reorderChild(child,4-(137-10)/81);
    expect(child.getLocalZOrder()===2,'original fractional TMX z-order truncates toward zero');
    parent.reorderChild(child,-3.75);expect(child.getLocalZOrder()===-3,'negative z-order');
    parent.reorderChild(child,'6.9');expect(child.getLocalZOrder()===6,'old number conversion');
    var failed=false;try{parent.reorderChild(child,NaN);}catch(error){failed=true;}
    expect(failed&&child.getLocalZOrder()===6,'NaN rejected without modifying child');
    var Layer=cc.Layer.extend({ctor:function(){this._super(cc.color(98,99,117),cc.color(0,0,0));}});
    var layer=new Layer();
    expect(jsb.isObjectValid(layer)&&layer instanceof cc.Layer,'original shared BaseTestLayer constructor');
    var anchor=layer.getAnchorPoint();
    expect(anchor.x===0.5&&anchor.y===0.5&&layer.isIgnoreAnchorPointForPosition(),'true native Layer initialization');
    var direct=new cc.Layer(cc.color(1,2,3),cc.color(4,5,6));
    expect(jsb.isObjectValid(direct)&&direct.getAnchorPoint().x===0.5,'direct legacy Layer ignores constructor arguments');
    parent.removeAllChildren();
    cc.log('NODE_COMPATIBILITY_PASS '+checks+' checks');
})();
