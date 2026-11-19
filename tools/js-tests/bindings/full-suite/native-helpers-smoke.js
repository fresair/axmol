// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Run with the real axjs bootstrap/bindings, without a rendering device.
// This verifies compatibility helpers only; it is not a full-scene result.
(function () {
    var checks=0;
    function expect(value,message) { checks++;if(!value)throw new Error('Native helper: '+message); }
    function near(a,b,message) { expect(Math.abs(a-b)<0.0001,message+': '+a+' versus '+b); }
    var translation=cc.math.mat4CreateTranslation(cc.math.vec3(2,3,4));
    var point=cc.math.mat4TransformPoint(translation,cc.math.vec3(5,6,7));
    near(point.x,7,'translated x');near(point.y,9,'translated y');near(point.z,11,'translated z');
    point=cc.math.mat4TransformPoint(cc.math.mat4GetInversed(translation),point);
    near(point.x,5,'inverse x');near(point.y,6,'inverse y');near(point.z,7,'inverse z');
    var quaternion=cc.math.quaternion(cc.math.vec3(0,0,1),Math.PI/2);
    var rotation=cc.math.mat4CreateRotation(quaternion);
    point=cc.math.mat4MultiplyVec3(rotation,cc.math.vec3(1,0,0));
    near(point.x,0,'rotated x');near(point.y,1,'rotated y');
    var product=cc.math.mat4Multiply(translation,rotation);
    point=cc.math.mat4TransformPoint(product,cc.math.vec3(1,0,0));
    near(point.x,2,'composition x');near(point.y,4,'composition y');near(point.z,4,'composition z');
    var q2=cc.math.quatMultiply(quaternion,quaternion);
    point=cc.math.mat4MultiplyVec3(cc.math.mat4CreateRotation(q2),cc.math.vec3(1,0,0));
    near(point.x,-1,'quaternion composition x');near(point.y,0,'quaternion composition y');
    var bounds=cc.math.obb(cc.math.aabb(cc.math.vec3(-1,-1,-1),cc.math.vec3(1,1,1)));
    var corners=cc.math.obbGetCorners(bounds);
    expect(corners.length===8,'eight native OBB corners');
    for(var i=0;i<corners.length;i++) {
        near(Math.abs(corners[i].x),1,'corner x');near(Math.abs(corners[i].y),1,'corner y');near(Math.abs(corners[i].z),1,'corner z');
    }
    var other=cc.math.obb(cc.math.aabb(cc.math.vec3(3,3,3),cc.math.vec3(4,4,4)));
    expect(cc.math.obbIntersectsObb(bounds,bounds),'overlapping OBBs');
    expect(!cc.math.obbIntersectsObb(bounds,other),'separated OBBs');
    expect(cc.math.rayIntersectsObb(cc.math.ray(cc.math.vec3(-2,0,0),cc.math.vec3(1,0,0)),bounds),'ray hit');
    expect(!cc.math.rayIntersectsObb(cc.math.ray(cc.math.vec3(-2,2,0),cc.math.vec3(1,0,0)),bounds),'ray miss');
    var node=new cc.Node();
    node.width=31;node.height=17;node.anchorX=0.25;node.anchorY=0.75;
    expect(node.getContentSize().width===31&&node.getContentSize().height===17,'native size accessors');
    near(node.getAnchorPoint().x,0.25,'native anchor x');near(node.getAnchorPoint().y,0.75,'native anchor y');
    node.rotationX=37.2;node.rotationY=-37.2;
    near(node.getRotationSkewX(),37.2,'legacy rotationX');near(node.getRotationSkewY(),-37.2,'legacy rotationY');
    node.setNormalizedPosition(0.25,0.75);
    near(node.getNormalizedPosition().x,0.25,'normalized two-number x');
    near(node.getNormalizedPosition().y,0.75,'normalized two-number y');
    node.setOpacity(83);node.setColor(cc.color(17,29,41,255));
    expect(JSON.stringify(node.getColor())==='{"r":17,"g":29,"b":41}','legacy RGB result');
    expect(node.getOpacity()===83,'setColor preserves independent opacity');
    expect(Object.keys(node.getDisplayedColor()).length===3,'displayed color RGB fields');
    var uninitializedSprite=cc.Sprite.__allocate();
    uninitializedSprite.setOpacity(97);uninitializedSprite.color=cc.color(31,47,59);
    expect(JSON.stringify(uninitializedSprite.color)==='{"r":31,"g":47,"b":59}',
        'native non-enumerable Sprite receives RGB adapter');
    expect(uninitializedSprite.getOpacity()===97,'Sprite color property preserves opacity');
    var data={answer:42};node.setUserData(data);expect(node.getUserData()===data,'user data identity');
    expect(cc.EventListener.FOCUS===6&&cc.EventListener.CUSTOM===8,'v3 event IDs');
    var dispatcher=cc.eventManager,wasEnabled=dispatcher.isEnabled(),eventCalls=0;
    dispatcher.setEnabled(true);
    var custom=new cc.EventCustom('full-suite-owned-event'),payload={answer:42};
    custom.setUserData(payload);
    var customListener=dispatcher.addCustomListener('full-suite-owned-event',function(event){
        expect(event===custom,'created custom event keeps wrapper identity');
        expect(event.getUserData()===payload,'custom event carries JS object identity');
        eventCalls++;
    });
    try {
        cc.sys.garbageCollect();
        dispatcher.dispatchEvent(custom);
        expect(eventCalls===1,'actual owned custom event dispatch');
    } finally {
        dispatcher.removeListener(customListener);dispatcher.setEnabled(wasEnabled);
    }
    var callbacks=0,progress=[];
    cc.loader.register(['axjstest'],{load:function (real,url,item,done) { done(null,url); }});
    cc.loader.load(['first.axjstest','second.axjstest'],function (value,total,completed) {
        expect(total===2,'loader total');progress.push(completed);
    },function (error,results) {
        expect(!error&&results[0]==='first.axjstest'&&results[1]==='second.axjstest','plugin resource order');callbacks++;
    });
    expect(callbacks===1&&progress.join(',')==='0,1','legacy completion and progress');
    cc.loader.releaseAll();
    cc.log('FULL_SUITE_NATIVE_HELPERS_PASS '+checks+' checks');
})();
