// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Isolated candidate, actual native Scene/Scheduler/ActionManager/Jolt.
// Minimal same-target RED reproduction plus a modern default-policy control.
(function () {
    var checks = 0, failures = [], serial = 0;
    function check(value, message) { ++checks; if (!value) failures.push(message); }
    function close(a, b) { return Math.abs(a - b) < .000001; }
    function record(kind, value) {
        value.kind = kind; value.serial = serial++; value.vmGeneration = __kinGeneration;
        __kinRecord(JSON.stringify(value));
    }
    var scene = __kinObservedScene(); scene.retain();
    var d = jsb.physics3DRigidBodyDes(); d.mass = 0;
    d.shape = new jsb.Physics3DShape(jsb.Physics3DShape.ShapeType.BOX, cc.math.vec3(2, 2, 2));
    var body = new jsb.Physics3DRigidBody(d), node = new cc.Node();
    node.addComponent(body); body.setKinematic(true);
    node.setPosition3D(cc.math.vec3(0, 0, 0)); scene.addChild(node);
    var detachedLegacy=__kinSnapshot(body);
    check(!detachedLegacy.attached&&detachedLegacy.zeroStepSyncEnabled,
          'legacy ctor enables real native policy before world attachment');
    __kinObserveBody(scene, body); scene.onEnter();
    var modernBody = cc.Rigidbody3D.create(cc.BoxCollider3D.create(cc.math.vec3(2,2,2)),0);
    var detachedModern=__kinSnapshot(modernBody);
    check(!detachedModern.attached&&!detachedModern.zeroStepSyncEnabled,
          'ordinary cc ctor has real native default false before attachment');
    var modernNode = new cc.Node();modernNode.addComponent(modernBody);
    modernBody.setKinematic();modernNode.setPosition3D(cc.math.vec3(0,0,100));scene.addChild(modernNode);
    check(__kinSnapshot(body).zeroStepSyncEnabled, 'legacy native policy survives attachment');
    check(!__kinSnapshot(modernBody).zeroStepSyncEnabled, 'ordinary cc body remains opted out despite setKinematic');
    var adoptedBody=cc.Rigidbody3D.create(cc.BoxCollider3D.create(cc.math.vec3(2,2,2)),0);
    var detachedAdoptedBefore=__kinSnapshot(adoptedBody);
    check(!detachedAdoptedBefore.zeroStepSyncEnabled,'modern body is opted out before explicit legacy adoption');
    var badOffset={},badOffsetReads=0,adoptionRejected=false;
    Object.defineProperty(badOffset,'x',{get:function(){++badOffsetReads;throw Error('invalid adoption offset');}});
    try { new jsb.Physics3DComponent(adoptedBody,badOffset); } catch(error) { adoptionRejected=true; }
    var detachedAdoptedAfterFailure=__kinSnapshot(adoptedBody);
    check(adoptionRejected&&badOffsetReads===1&&!detachedAdoptedAfterFailure.zeroStepSyncEnabled,
          'failed legacy adoption preserves original native policy');
    check(new jsb.Physics3DComponent(adoptedBody)===adoptedBody,'legacy Component returns its actual adopted native body');
    var detachedAdoptedAfter=__kinSnapshot(adoptedBody);
    check(detachedAdoptedAfter.zeroStepSyncEnabled,'legacy Component adoption enables actual native policy');
    var adoptedNode=new cc.Node();adoptedNode.addComponent(adoptedBody);adoptedBody.setKinematic();
    adoptedNode.setPosition3D(cc.math.vec3(0,0,200));scene.addChild(adoptedNode);
    var nativeBody=__kinNativeMasslessKinematic();
    var detachedNative=__kinSnapshot(nativeBody);
    check(!detachedNative.zeroStepSyncEnabled,'direct native C++ factory and setKinematic preserve default false');
    record('zero-step-policy-construction',{legacy:detachedLegacy,modern:detachedModern,
        adoptedBefore:detachedAdoptedBefore,adoptedAfterFailed:detachedAdoptedAfterFailure,
        adoptionRejected:adoptionRejected,badOffsetReads:badOffsetReads,
        adoptedAfter:detachedAdoptedAfter,nativeCpp:detachedNative});
    var nativeNode=new cc.Node();nativeNode.addComponent(nativeBody);
    nativeNode.setPosition3D(cc.math.vec3(0,0,300));scene.addChild(nativeNode);
    node.runAction(cc.moveBy(2, cc.math.vec3(30, 0, 0)));
    modernNode.runAction(cc.moveBy(2, cc.math.vec3(30,0,0)));
    adoptedNode.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));
    nativeNode.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));
    function frame(label, dt, expectedSteps, expectedTarget) {
        var modernBefore=__kinSnapshot(modernBody),nativeBefore=__kinSnapshot(nativeBody);
        __kinFrame(scene, dt);
        var f = JSON.parse(__kinLastStepFrame(scene));
        check(f.steps.length === expectedSteps, label + ' actual Scene fixed-step count');
        check(close(f.before.node.x, expectedTarget), label + ' actual Scheduler action target');
        check(f.before.attached && f.after.attached && f.after.lockSucceeded,
              label + ' attached real Jolt body');
        check(f.after.motionType === 1 && f.after.inverseMass === 0,
              label + ' actual mass-zero Kinematic');
        check(f.before.bodyId === f.after.bodyId && f.after.worldMatchesObservedScene,
              label + ' stable actual body/world identity');
        check(f.after.zeroStepSyncEnabled, label + ' actual native legacy policy');
        check(close(f.after.position.x,expectedTarget), label + ' actual legacy body follows target including N0');
        var modern=__kinSnapshot(modernBody);
        check(!modern.zeroStepSyncEnabled, label + ' modern policy remains default false in mixed world');
        var adopted=__kinSnapshot(adoptedBody),native=__kinSnapshot(nativeBody);
        check(adopted.zeroStepSyncEnabled&&close(adopted.nativePosition.x,expectedTarget),
              label+' adopted native body follows target including N0');
        check(!native.zeroStepSyncEnabled,label+' direct C++ body remains opted out');
        if(expectedSteps===0) {
            check(modern.nativePosition.x===modernBefore.nativePosition.x,
                  label+' ordinary cc body preserves actual prior solver pose');
            check(native.nativePosition.x===nativeBefore.nativePosition.x,
                  label+' direct C++ body preserves actual prior solver pose');
            check(f.after.velocity.x===f.before.velocity.x&&f.after.active===f.before.active,
                  label+' legacy N0 preserves real native velocity and activation');
        }
        if(label==='forward-one-step')check(Math.abs(f.after.velocity.x-18)<.00001,
            'real next-step velocity retains physical solver baseline across N0 publication');
        record('current-zero-step-frame', {label: label, requestedDelta: dt, observed: f});
        record('modern-zero-step-control', {label:label,native:modern,node:modernNode.getPosition3D()});
        record('adoption-zero-step-control',{label:label,native:adopted,node:adoptedNode.getPosition3D()});
        record('native-zero-step-control',{label:label,native:native,node:nativeNode.getPosition3D()});
    }
    // Current Scene starts with a seeded h; requested dt0 both starts the
    // actual action and consumes that seed. The old Bullet fixture primes its
    // solver with h. Both enter the measured .01/.01 pair at position/velocity0
    // and accumulator0. Preserve the distinct actual priming deltas in reports.
    frame('prime', 0, 1, 0);
    frame('forward-zero-step', .01, 0, .15);
    frame('forward-one-step', .01, 1, .30);
    frame('preserve-moving-velocity-zero-step',.001,0,.315);
    scene.onExit();
    check(!__kinSnapshot(body).attached, 'real Scene exit detaches body');
    scene.cleanup(); scene.release();
    function autoBoundary(manual) {
        var s=__kinObservedScene();s.retain();
        var descriptor=jsb.physics3DRigidBodyDes();descriptor.mass=0;descriptor.shape=d.shape;
        var b=new jsb.Physics3DRigidBody(descriptor),n=new cc.Node();
        n.addComponent(b);b.setKinematic();s.addChild(n);__kinObserveBody(s,b);s.onEnter();
        n.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));
        function observed(dt) { __kinFrame(s,dt);return JSON.parse(__kinLastStepFrame(s)); }
        var prime=observed(0),zero=observed(.01),world=s.getPhysics3DWorld();
        check(prime.steps.length===1&&zero.steps.length===0&&close(zero.after.position.x,.15),
              'auto boundary has actual prime then native N0 publication');
        world.setAutoStep(false);
        check(!world.isAutoStep(),'real generated JS setAutoStep(false) changes native world');
        var advanced,after;
        if(manual) {
            advanced=observed(.01);
            check(advanced.steps.length===1&&!advanced.steps[0].solverStepped&&close(advanced.before.node.x,.30),
                  'real Scheduler advances target .30 while automatic solver is disabled');
            world.stepSimulation(1/60);
            after=__kinSnapshot(b);
        } else {
            world.setAutoStep(true);
            check(world.isAutoStep(),'real generated JS setAutoStep(true) re-enables native world');
            advanced=observed(.01);
            check(advanced.steps.length===1&&advanced.steps[0].solverStepped,
                  'OFF then ON reaches one actual automatic solver step');
            after=__kinSnapshot(b);
        }
        check(close(after.nativePosition.x,.30)&&Math.abs(after.nativeVelocity.x-18)<.00001,
              'auto boundary retains full physical baseline for real solver velocity18');
        record('zero-step-auto-boundary',{manual:manual,zero:zero,advanced:advanced,
            actualAutoStep:world.isAutoStep(),nativeAfter:after,nodeAfter:n.getPosition3D()});
        s.onExit();check(!__kinSnapshot(b).attached,'auto boundary real Scene exit detaches native body');
        s.cleanup();s.release();
    }
    autoBoundary(true);
    autoBoundary(false);
    kinematicStaticResult = {checks: checks, failures: failures,
        scope: 'minimal N0 native pose/next-step velocity candidate plus actual mixed-world modern default control'};
    record('current-zero-step-collection-result', kinematicStaticResult);
})();
