// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Real exported JS setters; fixture only deactivates the actual native body.
(function(){
    var checks=0,failures=[],serial=0;
    function check(v,m){++checks;if(!v)failures.push(m);}
    function near(a,b){return Math.abs(a.x-b.x)+Math.abs(a.y-b.y)+Math.abs(a.z-b.z)<.001;}
    function quatNear(a,b){return near(a,b)&&Math.abs(a.w-b.w)<.001;}
    function record(kind,value){value.kind=kind;value.serial=serial++;value.vmGeneration=__kinGeneration;__kinRecord(JSON.stringify(value));}
    ['kinematic','dynamic','static'].forEach(function(type,index){
        var scene=__kinObservedScene();scene.retain();
        var d=jsb.physics3DRigidBodyDes();d.mass=type==='dynamic'?1:0;
        d.shape=new jsb.Physics3DShape(jsb.Physics3DShape.ShapeType.BOX,cc.math.vec3(2,2,2));
        var body=new jsb.Physics3DRigidBody(d),node=new cc.Node();node.addComponent(body);
        if(type==='kinematic')body.setKinematic(true);
        node.setPosition3D(cc.math.vec3(2,3,4));node.setRotationQuat({x:0,y:0,z:Math.sin(Math.PI/8),w:Math.cos(Math.PI/8)});
        scene.addChild(node);scene.onEnter();
        [false,true].forEach(function(teleport){
            __kinNativeDeactivate(body);var before=__kinSnapshot(body),rotationBefore=body.getRotation(),position={x:6+index,y:7,z:8};
            check(!before.nativeActive,type+' position starts truly inactive');
            body.setPosition(position,teleport);var after=__kinSnapshot(body),rotationAfter=body.getRotation();
            check(near(after.nativePosition,position),type+' actual public position coordinates teleport '+teleport);
            check(quatNear(rotationBefore,rotationAfter),type+' public position preserves true quaternion teleport '+teleport);
            check(after.nativeActive===(teleport&&type!=='static'),type+' public position preserves actual activation flag '+teleport);
            check(after.nativeBodyId===before.nativeBodyId,type+' public position preserves actual native identity');
            record('public-position-activation',{type:type,teleport:teleport,before:before,after:after,beforeRotation:rotationBefore,afterRotation:rotationAfter});
            __kinNativeDeactivate(body);before=__kinSnapshot(body);
            var nextRotation={x:Math.sin(Math.PI/12),y:0,z:0,w:Math.cos(Math.PI/12)};
            check(!before.nativeActive,type+' rotation starts truly inactive');
            body.setRotation(nextRotation,teleport);after=__kinSnapshot(body);rotationAfter=body.getRotation();
            check(near(after.nativePosition,before.nativePosition),type+' public rotation preserves true position teleport '+teleport);
            check(quatNear(rotationAfter,nextRotation),type+' actual public quaternion teleport '+teleport);
            check(after.nativeActive===(teleport&&type!=='static'),type+' public rotation preserves actual activation flag '+teleport);
            check(after.nativeBodyId===before.nativeBodyId,type+' public rotation preserves actual native identity');
            record('public-rotation-activation',{type:type,teleport:teleport,before:before,after:after,rotation:rotationAfter});
        });
        scene.onExit();check(!body.isAttached(),type+' exited Scene actually detached');
        body.setPosition(cc.math.vec3(99,99,99),true);body.setRotation({x:0,y:0,z:0,w:1},true);
        check(!body.isAttached(),type+' detached real public entries safely remain detached');
        scene.cleanup();scene.release();
    });
    kinematicStaticResult={checks:checks,failures:failures,vmGeneration:__kinGeneration,
        scope:'Public JS setPosition/setRotation coordinates, other-component preservation, native activation and detached guards; no GPU claim.'};
    record('result',kinematicStaticResult);
})();
