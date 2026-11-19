/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Real empty native owners exercise inherited ProcessBase APIs without a
// texture, renderer, animation resource, scene tick, or manual update call.
var studioProcessBaseSmokeReport = (function () {
    var checks = 0;
    function check(value, message) {
        ++checks;
        if (!value) throw Error('Studio ProcessBase: ' + message);
    }
    check(typeof ccs.AnimationController === 'function' &&
        ccs.ProcessBase === ccs.AnimationController, 'legacy class is the real native base');
    check(ccs.ArmatureAnimation === ccs.ArmatureController &&
        ccs.Tween === ccs.BoneTweenController, 'legacy derived class aliases');
    var base = ccs.ProcessBase.prototype;
    check(typeof base.getAnimationScale === 'function' &&
        base.getProcessScale === base.getAnimationScale &&
        typeof base.setAnimationScale === 'function' &&
        base.setProcessScale === base.setAnimationScale, 'scale aliases invoke the real native methods');

    var armature = new ccs.Armature();
    var bone = new ccs.Bone();
    var animation = armature.getAnimation();
    var tween = bone.getTween();
    check(jsb.isObjectValid(armature) && jsb.isObjectValid(bone), 'real empty native owners');
    check(animation === armature.getAnimationController() && tween === bone.getTween(),
        'canonical native controller getter identity');
    var results = [];
    function verify(controller, Type, name) {
        check(jsb.isObjectValid(controller) && controller instanceof Type &&
            controller instanceof ccs.ProcessBase && controller instanceof cc.Ref,
            name + ' inherits the actual registered native base');
        var index = controller.getCurrentFrameIndex();
        var percent = controller.getCurrentPercent();
        check(typeof index === 'number' && isFinite(index) && index === 0 &&
            typeof percent === 'number' && isFinite(percent) && percent === 0,
            name + ' exposes genuine initial frame/percent getters');
        check(controller.getRawDuration() === 0 && controller.isPause() === true &&
            controller.isComplete() === true && controller.isPlaying() === false,
            name + ' retains native empty-controller flags');
        check(controller.getProcessScale() === 1 && controller.getAnimationScale() === 1,
            name + ' initial native scale');
        controller.setProcessScale(0.375);
        check(controller.getAnimationScale() === 0.375 && controller.getProcessScale() === 0.375,
            name + ' old setter changes the actual renamed native scale');
        controller.setAnimationScale(1.25);
        check(controller.getProcessScale() === 1.25 && controller.getAnimationScale() === 1.25,
            name + ' new setter is visible through the legacy getter');
        controller.pause();
        check(controller.isPause() === true && controller.isPlaying() === false,
            name + ' native pause flags');
        controller.resume();
        check(controller.isPause() === false && controller.isPlaying() === true &&
            controller.isComplete() === true, name + ' native resume flags without inventing a movement');
        controller.pause();
        controller.stop();
        controller.setProcessScale(1);
        check(controller.isPause() === true && controller.isPlaying() === false &&
            controller.isComplete() === true && controller.getAnimationScale() === 1 &&
            controller.getCurrentFrameIndex() === index && controller.getCurrentPercent() === percent,
            name + ' restored flags/scale and no synthetic time progression');
        results.push({type:name, frame_index:index, current_percent:percent,
            raw_duration:controller.getRawDuration(), process_scale:controller.getProcessScale()});
    }
    verify(new ccs.ProcessBase(), ccs.AnimationController, 'ProcessBase');
    verify(animation, ccs.ArmatureController, 'ArmatureController');
    verify(tween, ccs.BoneTweenController, 'BoneTweenController');
    cc.log('STUDIO_PROCESS_BASE_SMOKE_PASS ' + checks + ' checks');
    return {passed:true, checks:checks, controllers:results,
        scope:'Native inherited API and empty-controller state only; no animation-loop or GPU claim.'};
})();
