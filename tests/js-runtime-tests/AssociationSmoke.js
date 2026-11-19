/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
var associationSmokeReport=(function () {
    var checks=0;
    function expect(value,message){++checks;if(!value)throw new Error('Native association: '+message);}
    var Legacy=cc.Layer.extend({ctor:function () {
        this._super();this.setPosition(12,34);
        cc.associateWithNative(this,cc.Layer);
        this.marker='same receiver';
    }});
    var layer=new Legacy(),parent=new cc.Node();parent.addChild(layer,0,317);
    expect(jsb.isObjectValid(layer),'legacy subclass remains bound');
    expect(layer.getPositionX()===12&&layer.getPositionY()===34,'existing native state preserved');
    expect(parent.getChildByTag(317)===layer&&layer.marker==='same receiver','canonical receiver identity');
    var unbound=Object.create(cc.Layer.prototype);
    expect(!jsb.isObjectValid(unbound),'plain receiver starts unbound');
    cc.associateWithNative(unbound,cc.Layer);
    expect(jsb.isObjectValid(unbound)&&unbound.init(),'real Layer allocation and init');
    unbound.setPosition(56,78);parent.addChild(unbound,0,318);
    expect(parent.getChildByTag(318)===unbound&&unbound.getPositionY()===78,'explicit class native association');
    var donor=new cc.Node(),receiver=Object.create(cc.Node.prototype);donor.setPosition(19,23);
    cc.associateWithNative(receiver,donor);
    expect(jsb.isObjectValid(receiver)&&!jsb.isObjectValid(donor),'native instance transfers unique wrapper');
    parent.addChild(receiver,0,319);
    expect(parent.getChildByTag(319)===receiver&&receiver.getPositionX()===19,'transferred native state/identity');
    cc.associateWithNative(receiver,cc.Node);
    expect(parent.getChildByTag(319)===receiver,'repeated compatibility call preserves identity');
    parent.removeAllChildren();
    cc.log('ASSOCIATION_SMOKE_PASS '+checks+' checks');
    return {checks:checks,passed:true,graphicsDeviceCreated:false};
})();
