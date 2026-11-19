// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// One bounded true public JS -> native setter invocation. No direct BodyInterface write.
(function(){
    var scene=__kinObservedScene();scene.retain();
    var d=jsb.physics3DRigidBodyDes();d.mass=0;d.shape=new jsb.Physics3DShape(jsb.Physics3DShape.ShapeType.BOX,cc.math.vec3(2,2,2));
    var body=new jsb.Physics3DRigidBody(d),node=new cc.Node();node.addComponent(body);body.setKinematic(true);
    node.setPosition3D(cc.math.vec3(2,3,4));scene.addChild(node);scene.onEnter();
    var before=__kinSnapshot(body),rotation={x:0,y:0,z:Math.sin(Math.PI/4),w:Math.cos(Math.PI/4)};
    __kinRecord(JSON.stringify({kind:'public-setter-before-call',vmGeneration:__kinGeneration,serial:0,
        method:'setRotation',methodType:typeof body.setRotation,before:before,argument:rotation,teleport:true,
        attribution:'setRotation is genuinely exported; previous unexported method was setRotationEnabled. Immediate durable JS marker; no captured stack claimed.'}));
    body.setRotation(rotation,true);
    var after=__kinSnapshot(body),afterRotation=body.getRotation(),checks=2,failures=[];
    if(Math.abs(after.nativePosition.x-before.nativePosition.x)+Math.abs(after.nativePosition.y-before.nativePosition.y)+Math.abs(after.nativePosition.z-before.nativePosition.z)>.001)failures.push('true public rotation setter preserves native position');
    if(Math.abs(afterRotation.x-rotation.x)+Math.abs(afterRotation.y-rotation.y)+Math.abs(afterRotation.z-rotation.z)+Math.abs(afterRotation.w-rotation.w)>.001)failures.push('true public quaternion changes');
    __kinRecord(JSON.stringify({kind:'public-setter-after-call',vmGeneration:__kinGeneration,serial:1,method:'setRotation',after:after,rotation:afterRotation}));
    scene.onExit();scene.cleanup();scene.release();
    kinematicStaticResult={checks:checks,failures:failures,vmGeneration:__kinGeneration};
    __kinRecord(JSON.stringify({kind:'result',vmGeneration:__kinGeneration,serial:2,checks:checks,failures:failures}));
})();
