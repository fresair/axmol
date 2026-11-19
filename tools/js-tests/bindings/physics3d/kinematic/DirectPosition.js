// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// One bounded true public JS -> native setter invocation. No direct BodyInterface write.
(function(){
    var scene=__kinObservedScene();scene.retain();
    var d=jsb.physics3DRigidBodyDes();d.mass=0;d.shape=new jsb.Physics3DShape(jsb.Physics3DShape.ShapeType.BOX,cc.math.vec3(2,2,2));
    var body=new jsb.Physics3DRigidBody(d),node=new cc.Node();node.addComponent(body);body.setKinematic(true);
    node.setPosition3D(cc.math.vec3(2,3,4));node.setRotationQuat({x:0,y:0,z:Math.sin(Math.PI/8),w:Math.cos(Math.PI/8)});
    scene.addChild(node);scene.onEnter();
    var before=__kinSnapshot(body),beforeRotation=body.getRotation();
    __kinRecord(JSON.stringify({kind:'public-setter-before-call',vmGeneration:__kinGeneration,serial:0,
        method:'setPosition',methodType:typeof body.setPosition,before:before,beforeRotation:beforeRotation,
        argument:{x:6,y:7,z:8},teleport:false,attribution:'Immediate durable JS marker before actual exported native setter; no captured stack claimed.'}));
    body.setPosition(cc.math.vec3(6,7,8),false);
    var after=__kinSnapshot(body),rotation=body.getRotation(),checks=2,failures=[];
    if(Math.abs(after.nativePosition.x-6)+Math.abs(after.nativePosition.y-7)+Math.abs(after.nativePosition.z-8)>.001)failures.push('true public position changes');
    if(Math.abs(rotation.x-beforeRotation.x)+Math.abs(rotation.y-beforeRotation.y)+Math.abs(rotation.z-beforeRotation.z)+Math.abs(rotation.w-beforeRotation.w)>.001)failures.push('true position setter preserves native quaternion');
    __kinRecord(JSON.stringify({kind:'public-setter-after-call',vmGeneration:__kinGeneration,serial:1,method:'setPosition',after:after,rotation:rotation}));
    scene.onExit();scene.cleanup();scene.release();
    kinematicStaticResult={checks:checks,failures:failures,vmGeneration:__kinGeneration};
    __kinRecord(JSON.stringify({kind:'result',vmGeneration:__kinGeneration,serial:2,checks:checks,failures:failures}));
})();
