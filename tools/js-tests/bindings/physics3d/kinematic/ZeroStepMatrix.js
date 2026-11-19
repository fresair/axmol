// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Candidate r8 copy of canonical MultiSteps; original canonical files are unchanged.
// Every completed scenario ends its normal autorelease lifetime. Native retained
// shutdown scenes remain alive until the actual restart/stop probes complete.
// Legacy N0 publication uses a separately tracked last-solver baseline oracle.
(function () {
    var checks=0,failures=[],serial=0,counts={},seenN={},pendingSolverBaselines={};
    var types=jsb.Physics3DShape.ShapeType,sync=jsb.Physics3DComponent.PhysicsSyncFlag;
    function record(kind,value) {
        value.kind=kind;value.serial=serial++;value.vmGeneration=__kinGeneration;
        __kinRecord(JSON.stringify(value));
    }
    function check(value,message) { ++checks;if(!value)failures.push(message); }
    function close(a,b,t) { return Math.abs(a-b)<(t===undefined ? .002 : t); }
    function make(scene,mass,shape,position,kinematic) {
        var d=jsb.physics3DRigidBodyDes();d.mass=mass;d.shape=shape;
        var body=new jsb.Physics3DRigidBody(d),node=new cc.Node();node.addComponent(body);
        if(kinematic)body.setKinematic(true);
        node.setPosition3D(position||cc.math.vec3(0,0,0));scene.addChild(node);
        return {body:body,node:node};
    }
    function isolated(mass,kinematic) {
        var scene=__kinObservedScene();scene.retain();
        var item=make(scene,mass===undefined?0:mass,new jsb.Physics3DShape(types.BOX,cc.math.vec3(2,2,2)),null,kinematic!==false);
        item.scene=scene;item.world=scene.getPhysics3DWorld();__kinObserveBody(scene,item.body);scene.onEnter();return item;
    }
    function finish(item) {
        var last=__kinLastStepFrame(item.scene);
        if(last){var prefix=JSON.parse(last).nativeSceneSerial+'/';Object.keys(pendingSolverBaselines).forEach(function(key){if(key.indexOf(prefix)===0)delete pendingSolverBaselines[key];});}
        item.scene.onExit();item.scene.cleanup();item.scene.release();__kinDrainAutoreleases();
    }
    function frame(item,dt) { __kinFrame(item.scene,dt);return JSON.parse(__kinLastStepFrame(item.scene)); }
    function preview(f) {
        var accumulator=f.accumulatorBefore+f.appliedDelta,n=0;
        while(accumulator>=f.fixedDeltaBefore&&n<f.maxStepsBefore){accumulator-=f.fixedDeltaBefore;++n;}
        if(n===f.maxStepsBefore)accumulator=0;
        return {n:n,remainder:accumulator};
    }
    function stable(f,label) {
        var p=preview(f),n=p.n;seenN[n]=true;counts[label]=(counts[label]||0)+1;
        check(f.steps.length===n,label+' actual repeated-comparison N');
        check(close(f.accumulatorAfter,p.remainder,1e-12),label+' exact native remainder/drop');
        var key=f.nativeSceneSerial+'/'+f.before.ownerNativeAddress+'/'+f.before.bodyId;
        var hadPending=Object.prototype.hasOwnProperty.call(pendingSolverBaselines,key);
        var start=hadPending?pendingSolverBaselines[key]:f.before.position.x,target=f.before.node.x;
        f.expectedLegacySolverBaseline=start;f.hadPendingLegacyBaseline=hadPending;
        if(n===0){
            if(!hadPending)pendingSolverBaselines[key]=f.before.position.x;
            check(f.after.zeroStepSyncEnabled&&close(f.after.position.x,target,1e-6),label+' legacy N0 publishes actual native target');
            check(f.after.velocity.x===f.before.velocity.x&&f.after.active===f.before.active,label+' N0 preserves actual native velocity/activation');
            return;
        }
        delete pendingSolverBaselines[key];
        var velocity=(target-start)/(n*f.fixedDeltaBefore);
        f.steps.forEach(function(s,index){
            check(s.solverStepped,label+' step '+index+' actual automatic solver entry');
            check(s.native.motionType===1&&s.native.inverseMass===0&&s.native.hasMotionProperties,
                label+' step '+index+' real mass-zero Kinematic');
            check(close(s.native.position.x,start+(target-start)*(index+1)/n),label+' step '+index+' actual position fraction');
            check(close(s.native.velocity.x,velocity,.06),label+' step '+index+' actual remaining-time velocity');
            check(close(s.native.node.x,s.native.position.x,1e-5),label+' step '+index+' actual post-step Node pose');
        });
    }
    // Prime with a real zero-delta frame to consume Scene's seeded h. The .05
    // float boundary from zero is N2; .05000001 is N3. No division/floor oracle.
    [0,.005,1/60,.05,.05000001,.08333334,.1].forEach(function(dt,index){
        var item=isolated();item.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));
        var primed=frame(item,0);stable(primed,'boundary-prime-'+index);
        check(primed.steps.length===1&&primed.accumulatorAfter===0,'startup seed is consumed by one real h step');
        var f=frame(item,dt);stable(f,'boundary-'+index);
        if(dt===.05)check(f.steps.length===2,'exact float .05 from zero executes N2');
        if(dt===.05000001)check(f.steps.length===3,'next float boundary executes N3');
        if(dt===.1)check(f.steps.length===5&&f.accumulatorAfter===0,'max5 drops native remaining accumulator');
        if(dt===1/60)check(close(f.after.velocity.x,15,.025),'single-step equivalence real velocity15');
        record('delta-boundary',{requestedDelta:dt,observed:f,expected:preview(f)});finish(item);
    });
    var partitioned=isolated();partitioned.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(partitioned,0);
    [0,.005,.01,.002,.05,.05000001,1/60,0,.1,.003,.013667,.002,.08333334,.02].forEach(function(dt,index){
        var f=frame(partitioned,dt);stable(f,'partition-'+index);
        record('delta-partition',{index:index,requestedDelta:dt,observed:f,expected:preview(f)});
    });finish(partitioned);
    [.5,.25,2,0].forEach(function(scale,index){
        var item=isolated();item.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(item,0);
        item.scene.setTimeScale(scale);var f=frame(item,.05);stable(f,'scene-time-scale-'+index);
        check(close(f.before.node.x,.75),'Scene timeScale leaves actual Scheduler action target .75');
        record('scene-time-scale',{scale:scale,observed:f});
        item.scene.setTimeScale(1);stable(frame(item,.05),'scene-time-scale-resume-'+index);finish(item);
    });
    var clamped=isolated();clamped.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(clamped,0);
    clamped.scene.setMaxDeltaTime(.02);var cf=frame(clamped,.2);stable(cf,'clamp');
    check(close(cf.appliedDelta,.02,1e-8)&&cf.steps.length===1,'actual Scene max-delta clamp retains one step and remainder');
    check(close(cf.before.node.x,3),'Scheduler target records full .2 business delta');
    record('scene-delta-clamp',{observed:cf});finish(clamped);
    var paused=isolated();paused.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(paused,.05);
    var p1=frame(paused,.05);stable(p1,'before-node-pause');paused.node.pause();
    var p2=frame(paused,.05);stable(p2,'actual-node-pause');
    check(close(p2.before.node.x,p1.after.node.x)&&close(p2.after.velocity.x,0),'real Node pause freezes action target and native motion');
    paused.node.resume();var p3=frame(paused,.05);stable(p3,'actual-node-resume');
    check(p3.before.node.x>p2.after.node.x+.7,'real Node resume advances original action');
    record('node-pause-resume',{before:p1,paused:p2,resumed:p3});finish(paused);
    var sched=cc.director.getScheduler(),scaled=isolated();scaled.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(scaled,.05);
    sched.setTimeScale(.5);var sf=frame(scaled,.05);sched.setTimeScale(1);stable(sf,'scheduler-time-scale');
    check(close(sf.before.node.x,.375),'actual Scheduler scale .5 produces Node target .375');
    record('scheduler-time-scale',{observed:sf});finish(scaled);
    var variable=isolated();variable.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(variable,0);
    variable.scene.setFixedUpdateEnabled(false);var vf=frame(variable,.05);
    check(vf.steps.length===0&&!vf.fixedEnabledBefore,'fixed-disabled Scene uses actual variable physics entry');
    check(close(vf.after.position.x,.75)&&close(vf.after.velocity.x,15,.025),'real variable-step MoveKinematic arrives with velocity15');
    record('fixed-disabled-scene',{observed:vf});finish(variable);
    var manual=isolated();manual.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));frame(manual,0);
    manual.world.setAutoStep(false);var mf=frame(manual,.05);
    check(mf.steps.length===2&&mf.steps.every(function(s){return !s.solverStepped;}),'actual generated JS autoStep disables both solver entries');
    check(close(mf.after.position.x,0)&&close(mf.before.node.x,.75),'disabled world retains body with unsimulated Node target');
    manual.world.stepSimulation(1/60);var mn=__kinSnapshot(manual.body);
    check(close(mn.nativePosition.x,.75)&&close(mn.nativeVelocity.x,45,.05),'direct manual World step has one actual h budget');
    record('disabled-auto-step-and-manual-world',{observed:mf,manualNative:mn,
        attribution:'The generated JS setter runs outside the Scene frame; this control alone does not prove mid-frame inline setter invalidation.'});finish(manual);
    // Modern positive-mass path retains real native generic preSimulate behavior.
    var modern=isolated(1,false);__kinModernSetKinematic(modern.body);modern.node.setPosition3D(cc.math.vec3(.75,0,0));
    var modernFrame=frame(modern,.05);
    check(modernFrame.steps.length===3,'modern positive-mass control executes real N3');
    modernFrame.steps.forEach(function(s,i){
        check(s.native.inverseMass===1&&s.native.motionType===1&&close(s.native.position.x,.75)&&close(s.native.velocity.x,0),
            'modern positive-mass step '+i+' unchanged native default');
    });record('modern-positive-mass-default',{observed:modernFrame});finish(modern);
    // Captured quaternion target also survives actual intermediate Node writes.
    var rotated=isolated();rotated.node.setRotationQuat({x:0,y:0,z:Math.sin(Math.PI/4),w:Math.cos(Math.PI/4)});
    var rf=frame(rotated,.05),targetAngle=2*Math.atan2(rf.before.nodeRotation.z,rf.before.nodeRotation.w);
    check(rf.steps.length===3,'rotation control executes actual N3');
    rf.steps.forEach(function(s,i){
        check(close(2*Math.atan2(s.native.rotation.z,s.native.rotation.w),targetAngle*(i+1)/3,.002),
            'native quaternion target fraction '+i);
        check(close(s.native.angularVelocity.z,targetAngle/(3*rf.fixedDeltaBefore),.05),'actual angular solver velocity '+i);
    });record('native-quaternion-target',{observed:rf});finish(rotated);
    // All original 2/3/4-second MoveBy/reverse actions run via ActionManager.
    for(var originalIndex=0;originalIndex<3;++originalIndex){
        var actionItem=isolated(),duration=2+originalIndex,speed=30/duration;
        actionItem.node.setPosition3D(cc.math.vec3(-15,0,15-15*originalIndex));
        var move=cc.moveBy(duration,cc.math.vec3(30,0,0));actionItem.node.runAction(cc.sequence(move,move.reverse()).repeatForever());
        frame(actionItem,.05);var positive=0,negative=0,minimum=Infinity,maximum=-Infinity,actionFrames=Math.ceil((2*duration+.5)/.05);
        for(var ai=0;ai<actionFrames;++ai){
            var af=frame(actionItem,.05);stable(af,'original-action-'+originalIndex);
            af.steps.forEach(function(s){
                minimum=Math.min(minimum,s.native.position.x);maximum=Math.max(maximum,s.native.position.x);
                if(close(s.native.velocity.x,speed,.025))++positive;
                if(close(s.native.velocity.x,-speed,.025))++negative;
            });
        }
        check(minimum< -14.9&&maximum>14.9&&maximum-minimum>29.5,'original '+duration+'s full unchanged native action path');
        check(positive>50&&negative>50,'original '+duration+'s actual per-step velocities in both directions');
        record('native-original-action-summary',{index:originalIndex,duration:duration,expectedAbsoluteSpeed:speed,
            positiveSolverSteps:positive,negativeSolverSteps:negative,minNativeX:minimum,maxNativeX:maximum,sceneFrames:actionFrames});finish(actionItem);
    }
    // Genuine moving contact on original index2 platform during N3 frames.
    [false,true].forEach(function(withPendingZero){
    var contactScene=__kinObservedScene();contactScene.retain();
    var platform=make(contactScene,0,new jsb.Physics3DShape(types.BOX,cc.math.vec3(2,2,2)),cc.math.vec3(-15,0,-15),true);
    platform.node.setScale(2);var originalMove=cc.moveBy(4,cc.math.vec3(30,0,0));
    platform.node.runAction(cc.sequence(originalMove,originalMove.reverse()).repeatForever());
    var sphere=make(contactScene,1,new jsb.Physics3DShape(types.SPHERE,.5),cc.math.vec3(-15,1.55,-15),false);
    sphere.body.setSyncFlag(sync.PHYSICS_TO_NODE);__kinObserveBody(contactScene,platform.body);
    var contacts=[],productionContacts=0,contactFrames=[],maxSphereVelocityX=0,contactFrameIndex=-1;
    var contactWorld=contactScene.getPhysics3DWorld(),hitBefore=contactWorld.isGlobalEventEnabled(2),passive=cc.ContactEventListener3D.create();
    passive.onCollisionHit=function(event){
        var v=__physics3DNativeContactSnapshot(event);
        if(!((v.actorA===platform.body&&v.actorB===sphere.body)||(v.actorB===platform.body&&v.actorA===sphere.body)))return;
        var onA=v.actorA===platform.body,item={frameIndex:contactFrameIndex,stepIndex:__kinActiveStep(contactScene),
            eventCode:v.eventCode,supportOnA:onA,normal:v.normal,points:v.points,
            supportVelocity:v.points.map(function(p){return onA?p.velocityOnA:p.velocityOnB;}),
            support:__kinSnapshot(platform.body),sphere:__kinSnapshot(sphere.body)};
        contacts.push(item);record('actual-multi-step-moving-contact',item);
    };
    cc.eventManager.addListener(passive,-127);
    check(contactWorld.isGlobalEventEnabled(2)===hitBefore,'passive native contact helper does not enable world demand');
    sphere.body.setCollisionCallback(function(info){
        if((info.objA===platform.body&&info.objB===sphere.body)||(info.objB===platform.body&&info.objA===sphere.body))++productionContacts;
    },sphere.node);
    contactScene.onEnter();check(contactWorld.isGlobalEventEnabled(2),'genuine production legacy callback enables contact demand');
    var contactItem={scene:contactScene};
    if(withPendingZero)frame(contactItem,0);
    for(var ci=0;ci<60;++ci){
        contactFrameIndex=ci;
        if(withPendingZero){var zf=frame(contactItem,.005);stable(zf,'contact-zero-step');check(zf.steps.length===0,'moving contact sequence has actual Scene N0');record('actual-moving-contact-zero-step',{frameIndex:ci,observed:zf});}
        var contactFrame=frame(contactItem,withPendingZero?.04500001:.05);contactFrames.push(contactFrame);stable(contactFrame,'contact-platform');
        maxSphereVelocityX=Math.max(maxSphereVelocityX,sphere.body.getLinearVelocity().x);
    }
    var movingPoints=0;
    contacts.forEach(function(c){
        var actual=contactFrames[c.frameIndex],step=actual.steps[c.stepIndex];
        check(actual.steps.length===3&&step.solverStepped,'contact occurs inside actual N3 automatic frame');
        check(close(step.native.position.x,c.support.nativePosition.x,1e-5),'contact snapshot and same-step native body position');
        c.supportVelocity.forEach(function(v){if(v&&close(v.x,7.5,.025)){
            ++movingPoints;check(close(v.x,step.native.velocity.x,.025),'manifold velocity equals same-step true solver velocity');
        }});
    });
    check(contacts.length>0&&productionContacts===contacts.length,'real moving contact and production callback counts');
    check(movingPoints>0,'real native contact side contains original 7.5 velocity during N3 motion');
    check(maxSphereVelocityX>.05,'real dynamic sphere responds to moving platform');
    record('actual-multi-step-moving-contact-summary',{nativeEvents:contacts.length,productionCallbacks:productionContacts,
        movingNativeSidePoints:movingPoints,maxActualSphereVelocityX:maxSphereVelocityX,sphereFinal:__kinSnapshot(sphere.body),
        originalPlatformIndex:2,withPendingZero:withPendingZero,scope:'Moving impact/response only; original small platform may subsequently lose sphere support.'});
    cc.eventManager.removeListener(passive);contactScene.onExit();contactScene.cleanup();contactScene.release();__kinDrainAutoreleases();
    });
    // Native C++ public mutations run before an actual selected fixedUpdate.
    // These controls enter public native setters compiled with the isolated candidate.
    ['sync','kinematic','enabled','mass','inertia','transform','nodeSync','position','rotation','maxSteps','fixedDelta','autoStep','sceneTimeScale'].forEach(function(kind){
        var item=isolated();item.node.setPosition3D(cc.math.vec3(.75,0,0));
        if(kind==='sync'){
            __kinStepMutation(item.scene,1,'sync',sync.PHYSICS_TO_NODE);__kinStepMutation(item.scene,1,'velocity',3);
            __kinStepMutation(item.scene,2,'sync',sync.NODE_TO_PHYSICS|sync.PHYSICS_TO_NODE);
        }else if(kind==='kinematic'||kind==='enabled'){
            __kinStepMutation(item.scene,1,kind,0);__kinStepMutation(item.scene,2,kind,1);
        }else if(kind==='mass'){
            __kinStepMutation(item.scene,1,'mass',1);__kinStepMutation(item.scene,1,'velocity',0);
        }else if(kind==='inertia')__kinStepMutation(item.scene,1,'inertia',0);
        else if(kind==='transform')__kinStepMutation(item.scene,1,'transform',1);
        else if(kind==='nodeSync')__kinStepMutation(item.scene,1,'nodeSync',12);
        else if(kind==='position')__kinStepMutation(item.scene,1,'position',12);
        else if(kind==='rotation')__kinStepMutation(item.scene,1,'rotation',Math.PI/2);
        else if(kind==='maxSteps')__kinStepMutation(item.scene,0,'maxSteps',1);
        else if(kind==='fixedDelta')__kinStepMutation(item.scene,1,'fixedDelta',.02);
        else if(kind==='autoStep'){
            __kinStepMutation(item.scene,1,'autoStep',0);__kinStepMutation(item.scene,2,'autoStep',1);
        }else if(kind==='sceneTimeScale')__kinStepMutation(item.scene,1,'sceneTimeScale',0);
        var changed=frame(item,.05);
        check(close(changed.steps[0].native.position.x,.25),kind+' first actual step uses stable initial frame budget');
        if(kind==='sync'){
            check(close(changed.steps[1].native.position.x,.3)&&close(changed.after.position.x,.3),
                'sync disable real independent velocity then re-enable does not revive captured target');
        }else if(kind==='kinematic'||kind==='inertia'){
            check(close(changed.after.position.x,.25)&&close(changed.after.velocity.x,0),kind+' cannot revive old target');
        }else if(kind==='enabled'){
            check(!changed.steps[1].native.attached&&changed.after.attached&&changed.before.bodyId!==changed.after.bodyId,
                'disable/re-enter establishes actual new BodyID generation');
            check(close(changed.after.position.x,.25),'re-enter does not revive old BodyID target');
        }else if(kind==='mass'){
            check(changed.after.inverseMass===1&&close(changed.after.position.x,.25),'mass mutation selects unchanged positive-mass native path');
            item.body.setMass(0);
        }else if(kind==='transform'){
            check(close(changed.after.position.x,-.75)&&close(changed.after.node.x,.25),'changed transform invalidates old physical-space target');
            __kinNativeTransformControl(item.body,0);
        }else if(kind==='nodeSync'||kind==='position')check(close(changed.after.position.x,12),kind+' real public entry cannot revive old target');
        else if(kind==='rotation'){
            check(close(changed.after.position.x,.25)&&close(2*Math.atan2(changed.after.rotation.z,changed.after.rotation.w),Math.PI/2),
                'real public rotation entry invalidates old positional/quaternion target');
        }
        else if(kind==='maxSteps'){
            check(changed.steps.length===1&&changed.accumulatorAfter===0&&close(changed.after.position.x,.25),
                'mid-frame cap interruption preserves original shortened while and drop without forced arrival');
            item.scene.setMaxFixedStepsPerFrame(5);
        }else if(kind==='fixedDelta'){
            check(changed.steps.length===2&&changed.accumulatorAfter===0&&close(changed.after.position.x,.5),
                'mid-frame h reset preserves original actual shortened loop without forced arrival');
        }else if(kind==='autoStep'){
            check(changed.steps.length===3&&!changed.steps[1].solverStepped&&changed.steps[2].solverStepped&&close(changed.after.position.x,.25),
                'native auto-step interruption clears frame target without forced arrival');
        }else if(kind==='sceneTimeScale'){
            check(changed.steps.length===3&&close(changed.after.position.x,.75),
                'mid-frame timeScale change preserves already applied original accumulator');
            var stopped=frame(item,.05);check(stopped.steps.length===0&&close(stopped.after.position.x,.75),'next frame scale0 naturally has no solver steps');
            item.scene.setTimeScale(1);
        }
        item.node.setPosition3D(cc.math.vec3(2,0,0));var fresh=frame(item,.05);stable(fresh,'fresh-after-'+kind);
        check(close(fresh.after.position.x,2),kind+' lexical cleanup permits fresh next-frame target');
        record('native-mid-frame-invalidation',{mutation:kind,interrupted:changed,fresh:fresh,
            attribution:'The fixture uses isolated candidate headers and true public native setters. Scene variable while behavior is preserved; shortened frames are not forced to final target.'});
        finish(item);
    });
    // Enter the real exported JS setter before the middle/last fixedUpdate.
    // The first step is normal; false/true clears its captured target without
    // making up the skipped solver step or forcing arrival at the old .75.
    var shippingJsAutoStepBeforeChecks=checks,shippingJsItem=isolated();
    shippingJsItem.node.setPosition3D(cc.math.vec3(.75,0,0));
    __kinStepMutation(shippingJsItem.scene,1,'jsAutoStep',0);
    __kinStepMutation(shippingJsItem.scene,2,'jsAutoStep',1);
    var shippingJsChanged=frame(shippingJsItem,.05);
    check(shippingJsChanged.steps.length===3,'generated JS autoStep interruption preserves actual N3 Scene loop');
    check(shippingJsChanged.jsSetterCallsSucceeded,'generated JS autoStep calls both return success');
    check(shippingJsChanged.jsAutoStepCalls.length===2,'generated JS autoStep records both exported method calls');
    shippingJsChanged.jsAutoStepCalls.forEach(function(call,index){
        check(call.step===index+1,'generated JS autoStep call '+index+' precedes actual middle/last step');
        check(call.enabled===(index===1),'generated JS autoStep call '+index+' has original false/true argument');
        check(call.registeredPrototypeSetter&&call.callSucceeded,'generated JS autoStep call '+index+' enters registered callable');
        check(call.autoStepBefore===(index===0)&&call.autoStepAfter===(index===1),
            'generated JS autoStep call '+index+' records real native before/after state');
    });
    shippingJsChanged.steps.forEach(function(step,index){
        check(step.solverStepped===(index!==1),'generated JS autoStep step '+index+' records actual solver entry/skip');
        check(close(step.native.position.x,.25)&&close(step.native.node.x,.25),
            'generated JS autoStep step '+index+' preserves native and Node x .25');
        check(step.native.motionType===1&&step.native.inverseMass===0&&step.native.hasMotionProperties,
            'generated JS autoStep step '+index+' keeps actual mass-zero Kinematic');
        check(step.native.bodyId===shippingJsChanged.before.bodyId&&
            step.native.ownerNativeAddress===shippingJsChanged.before.ownerNativeAddress&&
            step.native.worldNativeAddress===shippingJsChanged.before.worldNativeAddress,
            'generated JS autoStep step '+index+' retains actual BodyID/owner/World');
    });
    check(close(shippingJsChanged.after.position.x,.25)&&close(shippingJsChanged.after.node.x,.25),
        'generated JS autoStep clears captured target without forced .75 arrival');
    check(shippingJsItem.world.isAutoStep(),'generated JS autoStep resumes true native World state');
    shippingJsItem.node.setPosition3D(cc.math.vec3(2,0,0));
    var shippingJsFresh=frame(shippingJsItem,.05);stable(shippingJsFresh,'fresh-after-shipping-js-auto-step');
    check(close(shippingJsFresh.before.position.x,.25)&&close(shippingJsFresh.before.node.x,2),
        'generated JS autoStep next frame records fresh native start and Node target');
    check(close(shippingJsFresh.after.position.x,2)&&close(shippingJsFresh.after.node.x,2),
        'generated JS autoStep next frame reaches fresh target with actual repeated-comparison budget');
    record('shipping-js-mid-frame-auto-step-invalidation',{interrupted:shippingJsChanged,fresh:shippingJsFresh,
        attribution:'Lexical-root JS_CallFunctionValue enters the actual registered PhysicsWorld3D.setAutoStep consumer; no direct native setter or script replacement participates in these two mutations.'});
    finish(shippingJsItem);
    var shippingJsAutoStepChecks=checks-shippingJsAutoStepBeforeChecks;
    // Native scene/world detach and re-entry also start a fresh BodyID lease.
    var lifecycle=isolated();lifecycle.node.setPosition3D(cc.math.vec3(.75,0,0));stable(frame(lifecycle,.05),'before-scene-exit');
    var exitBefore=__kinSnapshot(lifecycle.body);lifecycle.scene.onExit();
    check(!lifecycle.body.isAttached(),'real Scene exit detaches body');lifecycle.scene.onEnter();
    var reentered=__kinSnapshot(lifecycle.body);check(reentered.attached&&reentered.nativeBodyId!==exitBefore.nativeBodyId,'Scene re-entry uses new Jolt BodyID generation');
    lifecycle.node.setPosition3D(cc.math.vec3(4,0,0));var reentryFrame=frame(lifecycle,.05);stable(reentryFrame,'scene-reentry');
    record('scene-exit-reentry',{before:exitBefore,reentered:reentered,observed:reentryFrame});finish(lifecycle);
    ['ownerDirect','ownerTransfer'].forEach(function(kind){
        var item=isolated(),replacement=new cc.Node();replacement.setPosition3D(cc.math.vec3(6,0,0));item.scene.addChild(replacement);
        item.node.setPosition3D(cc.math.vec3(.75,0,0));__kinConfigureTransfer(item.scene,replacement,null);
        __kinStepMutation(item.scene,1,kind,1);var changed=frame(item,.05);
        check(changed.steps.length===3&&close(changed.steps[0].native.position.x,.25),kind+' real initial step');
        check(changed.before.ownerNativeAddress!==changed.after.ownerNativeAddress&&
            changed.before.worldNativeAddress===changed.after.worldNativeAddress,kind+' true owner identity changes in same World');
        check(close(changed.after.position.x,6)&&close(changed.after.node.x,6),kind+' old owner target cannot revive');
        if(kind==='ownerDirect'){
            check(changed.before.bodyId===changed.after.bodyId,'owner-only change invalidates target with unchanged real BodyID');
            __kinNativeOwnerControl(item.body,item.node);
        }else{
            check(changed.before.bodyId!==changed.after.bodyId,'proper native detach/transfer/re-enter uses new BodyID generation');
            item.node=replacement;
        }
        item.node.setPosition3D(cc.math.vec3(2,0,0));var fresh=frame(item,.05);stable(fresh,'fresh-after-'+kind);
        record('native-owner-invalidation',{mutation:kind,interrupted:changed,fresh:fresh,
            attribution:'Direct owner setter is restored before cleanup; proper transfer calls real component onExit/remove/add/onEnter. No original fixture change.'});finish(item);
    });
    var transferred=isolated(),destination=__kinObservedScene();destination.retain();destination.onEnter();
    __kinObserveBody(destination,transferred.body);transferred.node.setPosition3D(cc.math.vec3(.75,0,0));
    __kinConfigureTransfer(transferred.scene,null,destination);__kinStepMutation(transferred.scene,1,'crossWorld',1);
    var leftWorld=frame(transferred,.05);
    check(leftWorld.steps.length===3&&leftWorld.before.worldMatchesObservedScene&&!leftWorld.after.worldMatchesObservedScene,
        'live Node transfer leaves actual previous World before remaining solver entries');
    check(leftWorld.before.worldNativeAddress!==leftWorld.after.worldNativeAddress&&
        leftWorld.before.ownerNativeAddress===leftWorld.after.ownerNativeAddress,'cross-World retains same owner with different true World identity');
    check(close(leftWorld.after.position.x,.25)&&close(leftWorld.after.velocity.x,0),'old World cannot continue captured target after transfer');
    transferred.node.setPosition3D(cc.math.vec3(2,0,0));var destinationFrame=frame({scene:destination},.05);stable(destinationFrame,'fresh-cross-world');
    check(destinationFrame.before.worldMatchesObservedScene&&close(destinationFrame.after.position.x,2),'new World captures its independent actual budget and target');
    record('native-cross-world-invalidation',{oldWorldFrame:leftWorld,newWorldFrame:destinationFrame,
        bodyIdsCanCoincideAcrossWorlds:leftWorld.before.bodyId===leftWorld.after.bodyId});
    destination.onExit();destination.cleanup();destination.release();finish(transferred);
    check(seenN[0]&&seenN[1]&&seenN[2]&&seenN[3]&&seenN[5],'actual observed fixed N0/N1/N2/N3/N5 all exercised');
    // Consecutive actual N0 frames retain the last real solver pose, independently
    // of all published native/Node poses. Expected N uses the original float loop.
    [1,2,3,5].forEach(function(wanted){
        var item=isolated();item.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));var prime=frame(item,0);stable(prime,'consecutive-prime-'+wanted);
        [.003,.004,.002].forEach(function(dt,index){var f=frame(item,dt);stable(f,'consecutive-N0-'+wanted+'-'+index);check(f.steps.length===0,'three consecutive real Scene N0 frames');record('consecutive-zero-native-frame',{wanted:wanted,index:index,observed:f});});
        var next=frame(item,wanted===1?.01:wanted===2?.03:wanted===3?.05:.1);stable(next,'consecutive-positive-'+wanted);
        check(next.steps.length===wanted,'pending-baseline sequence reaches actual requested N'+wanted);
        check(close(next.expectedLegacySolverBaseline,prime.after.position.x,1e-7),'oracle baseline remains prior real solver result');
        record('consecutive-zero-positive-frame',{wanted:wanted,lastRealSolver:prime.after,observed:next});finish(item);
    });
    var pendingClamp=isolated();pendingClamp.node.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));stable(frame(pendingClamp,0),'pending-clamp-prime');
    stable(frame(pendingClamp,.005),'pending-clamp-zero');pendingClamp.scene.setMaxDeltaTime(.02);
    var pc=frame(pendingClamp,.2);stable(pc,'pending-clamp-positive');
    check(pc.steps.length===1&&close(pc.appliedDelta,.02,1e-8)&&pc.accumulatorAfter>0,'pending-baseline clamp keeps original remainder');
    record('pending-zero-clamp-remainder',{observed:pc});finish(pendingClamp);
    var pendingRotation=isolated();frame(pendingRotation,0);var rotationBaseline=__kinSnapshot(pendingRotation.body);
    [.2,.4].forEach(function(angle){pendingRotation.node.setRotationQuat({x:0,y:0,z:Math.sin(angle/2),w:Math.cos(angle/2)});var f=frame(pendingRotation,.001);check(f.steps.length===0,'quaternion uses actual N0');check(close(2*Math.atan2(f.after.rotation.z,f.after.rotation.w),angle,.00001),'N0 publishes real native quaternion');});
    pendingRotation.node.setRotationQuat({x:0,y:0,z:Math.sin(.6/2),w:Math.cos(.6/2)});
    var pr=frame(pendingRotation,.05);check(pr.steps.length===3,'pending quaternion reaches actual N3');
    pr.steps.forEach(function(step,index){check(close(2*Math.atan2(step.native.rotation.z,step.native.rotation.w),.6*(index+1)/3,.0001),'N0 quaternion keeps last real solver angular baseline');check(close(step.native.angularVelocity.z,.6/(3*pr.fixedDeltaBefore),.001),'N0 next solver actual angular velocity');});
    record('pending-zero-native-quaternion',{lastRealSolver:rotationBaseline,observed:pr});finish(pendingRotation);
    ['none','base-sync-ABA','base-owner-ABA','policy-ABA','raw-position','raw-rotation','explicit-position','explicit-rotation','explicit-node-sync','offset','mass-ABA','motion-ABA','disable-reenter','scene-reenter'].forEach(function(kind){
        var item=isolated();frame(item,0);var oldPhysical=__kinSnapshot(item.body);item.node.setPosition3D(cc.math.vec3(.15,0,0));var z=frame(item,.005),published=__kinSnapshot(item.body);
        check(z.steps.length===0&&close(published.nativePosition.x,.15,1e-6),'invalidation precondition actual Scene N0 publication');
        var expectedBaseline=kind==='none'?oldPhysical.nativePosition.x:published.nativePosition.x,expectedTarget=.3;
        var expectedAngular=0,extra={};
        if(kind==='base-sync-ABA'){
            extra.registeredBase=typeof cc.PhysicsActor.prototype.setSyncFlag==='function';
            cc.PhysicsActor.prototype.setSyncFlag.call(item.body,sync.NONE);extra.disabled=item.body.getSyncFlag();
            cc.PhysicsActor.prototype.setSyncFlag.call(item.body,sync.NODE_AND_PHYSICS);extra.restored=item.body.getSyncFlag();
            check(extra.registeredBase&&extra.disabled===0&&extra.restored===3,'actual recompiled generated PhysicsActor base entry NONE then restore');
        }else if(kind==='base-owner-ABA'){
            var replacement=new cc.Node();replacement.setPosition3D(cc.math.vec3(.9,0,0));item.scene.addChild(replacement);
            cc.Component.prototype.setOwner.call(item.body,replacement);extra.intermediateOwnerIsB=item.body.getOwner()===replacement;
            cc.Component.prototype.setOwner.call(item.body,item.node);extra.finalOwnerIsA=item.body.getOwner()===item.node;
            check(extra.intermediateOwnerIsB&&extra.finalOwnerIsA,'actual generated Component base virtual entry A to B to A');
        }else if(kind==='policy-ABA'){item.body.__setKinematicZeroStepSync(false);check(!__kinSnapshot(item.body).zeroStepSyncEnabled,'real native policy false');item.body.__setKinematicZeroStepSync(true);}
        else if(kind==='raw-position'){__kinNativeRawPose(item.body,'position',.2);expectedBaseline=.2;}
        else if(kind==='raw-rotation'){__kinNativeRawPose(item.body,'rotation',.4);expectedAngular=-.4/(1/60);}
        else if(kind==='explicit-position'){item.body.setPosition(cc.math.vec3(.2,0,0),false);expectedBaseline=.2;}
        else if(kind==='explicit-rotation'){item.body.setRotation({x:0,y:0,z:Math.sin(.4/2),w:Math.cos(.4/2)},false);expectedAngular=-.4/(1/60);}
        else if(kind==='explicit-node-sync'){item.node.setPosition3D(cc.math.vec3(.2,0,0));item.body.syncNodeToPhysics();expectedBaseline=.2;}
        else if(kind==='offset'){item.body.setTransformInPhysics(cc.math.vec3(.05,0,0),cc.math.quaternion(0,0,0,1));expectedTarget=.25;}
        else if(kind==='mass-ABA'){item.body.setMass(1);extra.positiveMass=__kinSnapshot(item.body);item.body.setMass(0);check(extra.positiveMass.nativeInverseMass===1&&__kinSnapshot(item.body).nativeInverseMass===0,'actual mass properties change and restore');}
        else if(kind==='motion-ABA'){item.body.setMotionType(0);extra.static=__kinSnapshot(item.body);item.body.setMotionType(1);check(extra.static.nativeJoltMotionType===0&&__kinSnapshot(item.body).nativeJoltMotionType===1,'actual native motion type changes Static to Kinematic');}
        else if(kind==='disable-reenter'){item.body.setEnabled(false);check(!__kinSnapshot(item.body).attached,'actual native disable detaches');item.body.setEnabled(true);check(__kinSnapshot(item.body).nativeBodyId!==published.nativeBodyId,'actual reenable uses new BodyID');}
        else if(kind==='scene-reenter'){item.scene.onExit();check(!__kinSnapshot(item.body).attached,'actual Scene exit detaches');item.scene.onEnter();check(__kinSnapshot(item.body).nativeBodyId!==published.nativeBodyId,'actual Scene reentry uses new BodyID');}
        var beforePositive=__kinSnapshot(item.body);item.node.setPosition3D(cc.math.vec3(.3,0,0));item.world.stepSimulation(1/60);var afterPositive=__kinSnapshot(item.body);
        check(close(afterPositive.nativePosition.x,expectedTarget,.00001),kind+' positive solver reaches actual physical target');
        check(close(afterPositive.nativeVelocity.x,(expectedTarget-expectedBaseline)/(1/60),.0001),kind+' positive solver uses independently expected fresh physical baseline');
        extra.actualAngularVelocity=item.body.getAngularVelocity().z;
        if(kind==='raw-rotation'||kind==='explicit-rotation')check(close(extra.actualAngularVelocity,expectedAngular,.001),kind+' raw edit invalidates both positional and angular history');
        var rotation=item.body.getRotation();var expectedRotationAfter=2*Math.atan2(rotation.z,rotation.w);
        check(close(expectedRotationAfter,0,.00001),kind+' positive solver reaches actual native target quaternion');
        if(kind==='base-owner-ABA'||kind==='base-sync-ABA')check(afterPositive.nativeBodyId===published.nativeBodyId,kind+' identity remains same while intermediate change invalidates history');
        record('pending-zero-invalidation',{mutation:kind,lastRealSolver:oldPhysical,published:published,beforePositive:beforePositive,afterPositive:afterPositive,expectedBaseline:expectedBaseline,expectedTarget:expectedTarget,expectedAngular:expectedAngular,extra:extra});finish(item);
    });
    var inactive=isolated();frame(inactive,0);__kinNativeDeactivate(inactive.body);var asleep=__kinSnapshot(inactive.body);
    inactive.node.setPosition3D(cc.math.vec3(.15,0,0));var inactiveZero=frame(inactive,.005),publishedInactive=__kinSnapshot(inactive.body);
    check(!asleep.nativeActive&&!publishedInactive.nativeActive&&inactiveZero.steps.length===0,'N0 real native pose publication preserves deactivation');
    check(publishedInactive.nativeVelocity.x===asleep.nativeVelocity.x,'N0 preserves inactive actual velocity');
    inactive.node.setPosition3D(cc.math.vec3(.3,0,0));inactive.world.stepSimulation(1/60);var inactiveNext=__kinSnapshot(inactive.body);
    check(inactiveNext.nativeActive&&close(inactiveNext.nativeVelocity.x,18,.0001),'next actual MoveKinematic activates and uses original physical baseline');
    record('pending-zero-inactive-native-control',{before:asleep,zero:publishedInactive,positive:inactiveNext});finish(inactive);
    var pendingCross=isolated();frame(pendingCross,0);pendingCross.node.setPosition3D(cc.math.vec3(.15,0,0));var crossZero=frame(pendingCross,.005),crossBefore=__kinSnapshot(pendingCross.body);
    var nextScene=__kinObservedScene();nextScene.retain();nextScene.onEnter();__kinObserveBody(nextScene,pendingCross.body);
    pendingCross.node.retain();pendingCross.scene.removeChild(pendingCross.node,false);nextScene.addChild(pendingCross.node);pendingCross.node.release();
    var crossAttached=__kinSnapshot(pendingCross.body);pendingCross.node.setPosition3D(cc.math.vec3(.3,0,0));
    pendingCross.world.stepSimulation(1/60);var afterOldWorld=__kinSnapshot(pendingCross.body);nextScene.getPhysics3DWorld().stepSimulation(1/60);var afterNewWorld=__kinSnapshot(pendingCross.body);
    check(crossZero.steps.length===0&&crossAttached.attached&&close(crossAttached.nativePosition.x,.15),'actual cross World transfer follows pending N0');
    check(close(afterOldWorld.nativePosition.x,.15)&&close(afterNewWorld.nativePosition.x,.3)&&close(afterNewWorld.nativeVelocity.x,9,.0001),'old World cannot restore history in new World');
    pendingCross.node.retain();nextScene.removeChild(pendingCross.node,false);pendingCross.scene.addChild(pendingCross.node);pendingCross.node.release();
    var crossReturned=__kinSnapshot(pendingCross.body);pendingCross.node.setPosition3D(cc.math.vec3(.45,0,0));pendingCross.world.stepSimulation(1/60);var afterReturn=__kinSnapshot(pendingCross.body);
    check(crossReturned.nativeBodyId!==crossBefore.nativeBodyId&&close(afterReturn.nativeVelocity.x,9,.0001),'World A to B to A cannot revive previous BodyID history');
    record('pending-zero-world-ABA',{before:crossBefore,attachedToB:crossAttached,oldWorldResult:afterOldWorld,newWorldResult:afterNewWorld,returnedToA:crossReturned,afterReturn:afterReturn});
    nextScene.onExit();nextScene.cleanup();nextScene.release();finish(pendingCross);
    var manualBoundaryResult=(function () {
    var checks=0,failures=[],serial=0;
    function check(v,m){++checks;if(!v)failures.push(m);}
    function close(a,b){return Math.abs(a-b)<.00002;}
    function record(kind,v){v.kind=kind;v.serial=serial++;v.vmGeneration=__kinGeneration;__kinRecord(JSON.stringify(v));}
    function make(legacy){
        var s=__kinObservedScene();s.retain();var n=new cc.Node(),b;
        if(legacy){var d=jsb.physics3DRigidBodyDes();d.mass=0;d.shape=new jsb.Physics3DShape(jsb.Physics3DShape.ShapeType.BOX,cc.math.vec3(2,2,2));b=new jsb.Physics3DRigidBody(d);b.setKinematic();}
        else{b=cc.Rigidbody3D.create(cc.BoxCollider3D.create(cc.math.vec3(2,2,2)),0);__kinModernSetKinematic(b);}
        n.addComponent(b);s.addChild(n);__kinObserveBody(s,b);s.onEnter();n.runAction(cc.moveBy(2,cc.math.vec3(30,0,0)));
        return {scene:s,node:n,body:b,world:s.getPhysics3DWorld(),legacy:legacy};
    }
    function frame(i,dt){__kinFrame(i.scene,dt);return JSON.parse(__kinLastStepFrame(i.scene));}
    function done(i){i.scene.onExit();check(!__kinSnapshot(i.body).attached,'actual Scene exit detaches');i.scene.cleanup();i.scene.release();__kinDrainAutoreleases();}
    [true,false].forEach(function(legacy){
        var i=make(legacy),prime=frame(i,0),zero=frame(i,.01),before=__kinSnapshot(i.body);
        check(prime.steps.length===1&&zero.steps.length===0,'actual prime N1 then Scene N0');
        check(before.zeroStepSyncEnabled===legacy,'native opt-in policy is explicit');
        check(close(before.nativePosition.x,legacy?.15:0),'real Scene N0 pose follows policy');
        i.world.setAutoStep(false);i.node.setPosition3D(cc.math.vec3(.2,0,0));
        i.world.stepSimulation(0);var afterZero=__kinSnapshot(i.body);
        i.world.stepSimulation(0);var afterRepeated=__kinSnapshot(i.body);
        check(close(afterZero.nativePosition.x,.2)&&close(afterRepeated.nativePosition.x,.2),'actual exported manual0 publishes native pose');
        check(close(afterZero.nativeVelocity.x,before.nativeVelocity.x)&&afterZero.nativeActive===before.nativeActive,'manual0 preserves real native velocity and activation');
        i.node.stopAllActions();i.node.setPosition3D(cc.math.vec3(.3,0,0));
        i.world.stepSimulation(1/60);var afterPositive=__kinSnapshot(i.body);
        check(close(afterPositive.nativePosition.x,.3),'actual positive solver reaches target');
        check(close(afterPositive.nativeVelocity.x,legacy?18:6),'positive solver uses expected physical baseline after two manual0');
        // A moving N0 must preserve the prior nonzero solver velocity too.
        i.world.setAutoStep(true);i.node.setPosition3D(cc.math.vec3(.315,0,0));
        var movingN0=frame(i,.001),movingBefore=__kinSnapshot(i.body);
        i.world.stepSimulation(0);var movingAfter=__kinSnapshot(i.body);
        check(movingN0.steps.length===0,'moving control is actual Scene N0');
        check(close(movingAfter.nativeVelocity.x,movingBefore.nativeVelocity.x)&&movingAfter.nativeActive===movingBefore.nativeActive,'manual0 after moving N0 preserves native velocity/activation');
        i.node.setPosition3D(cc.math.vec3(.6,0,0));i.world.stepSimulation(1/60);var movingPositive=__kinSnapshot(i.body);
        check(close(movingPositive.nativeVelocity.x,legacy?18:17.1),'manual0 after moving N0 preserves next positive physical baseline');
        record('zero-step-manual-zero-boundary',{legacy:legacy,sceneZero:zero,before:before,afterZero:afterZero,afterRepeated:afterRepeated,afterPositive:afterPositive,movingN0:movingN0,movingBefore:movingBefore,movingAfter:movingAfter,movingPositive:movingPositive});
        done(i);
    });
    return {checks:checks,failures:failures};
})();
    checks+=manualBoundaryResult.checks;failures=failures.concat(manualBoundaryResult.failures);record('manual-zero-subset-result',manualBoundaryResult);
    // Mandatory entered scene retained natively across actual VM restart/stop.
    var live=__kinObservedScene(),support=make(live,0,new jsb.Physics3DShape(types.BOX,cc.math.vec3(20,1,20)),cc.math.vec3(0,0,0),true);
    var ball=make(live,1,new jsb.Physics3DShape(types.SPHERE,.5),cc.math.vec3(0,5,0),false),liveCallbacks=0;
    ball.body.setSyncFlag(sync.PHYSICS_TO_NODE);__kinObserveBody(live,support.body);
    ball.body.setCollisionCallback(function(){++liveCallbacks;__kinLiveCallback(__kinGeneration);},ball.node);
    live.onEnter();for(var li=0;li<60;++li)__kinFrame(live,.05);
    check(liveCallbacks>0,'live real callback runs before VM shutdown');
    var livePhysicalBeforeZero=__kinSnapshot(support.body);
    // First a real small frame drains any almost-h remainder; the next .001 is
    // deterministically N0, without resetting the Scene or calling step(0).
    var drain=frame({scene:live},.001);livePhysicalBeforeZero=__kinSnapshot(support.body);
    support.node.setPosition3D(cc.math.vec3(.15,0,0));var liveZero=frame({scene:live},.001);
    check(liveZero.steps.length===0&&close(__kinSnapshot(support.body).nativePosition.x,.15,1e-6),'held native Scene has actual pending N0 history before VM shutdown');
    __kinHoldAcrossVm(live,ball.body,support.body,__kinGeneration,livePhysicalBeforeZero.nativePosition.x,.3);
    __kinDrainAutoreleases();var nativeRetainedAfterDrain=__kinSnapshot(support.body);
    check(nativeRetainedAfterDrain.attached&&nativeRetainedAfterDrain.nativeBodyId===livePhysicalBeforeZero.nativeBodyId&&close(nativeRetainedAfterDrain.nativePosition.x,.15,1e-6),'native retain alone preserves entered scene and published N0 pose across actual pool drain');
    record('live-native-retain-after-autorelease-drain',{native:nativeRetainedAfterDrain,lastRealNative:livePhysicalBeforeZero,nextTarget:.3});
    record('live-pending-zero-step-history',{lastRealNative:livePhysicalBeforeZero,observed:liveZero,nextTarget:.3});
    record('live-native-scene-held-before-vm-shutdown',{callbacks:liveCallbacks,support:__kinSnapshot(support.body),sphere:__kinSnapshot(ball.body),
        worldHitEnabled:live.getPhysics3DWorld().isGlobalEventEnabled(2)});
    kinematicStaticResult={checks:checks,inheritedAssertionChecks:checks-shippingJsAutoStepChecks,
        shippingJsAutoStepChecks:shippingJsAutoStepChecks,failures:failures,vmGeneration:__kinGeneration,observedStepCounts:seenN,
        scope:'Actual partial candidate SM33/Jolt CPU per-step Scene controls, original three actions, moving contact, exported JS autoStep consumer and retained VM lifecycle only.',
        unverified:['Original GPU category, application rendering and real OS input acceptance are outside this native fixture.']};
    record('zero-step-matrix-result',kinematicStaticResult);
})();
