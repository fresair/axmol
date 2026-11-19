/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Runs inside the real SpiderMonkey VM against native Axmol objects. */
(function () {
    var checks = 0;
    function check(condition, message) {
        ++checks;
        if (!condition) throw new Error('Studio regression: ' + message);
    }
    function near(actual, expected, message) {
        check(Math.abs(actual - expected) < 0.001, message + ': ' + actual + ' != ' + expected);
    }
    function position(node, x, y, message) {
        var value = node.getPosition();
        near(value.x, x, message + '.x');
        near(value.y, y, message + '.y');
    }
    function fails(file, expected) {
        var error;
        try { ccs.load(file); } catch (e) { error = e; }
        check(error && expected.test(String(error)), 'loading ' + file + ' reports ' + expected);
    }

    var old = ccs.load('studio/timeline-1.json');
    check(old.node instanceof cc.Node && old.action instanceof ccs.ActionTimeline, '1.x native root/action');
    check(old.node.getName() === 'old-root' && old.node.getTag() === 100, '1.x names/tags');
    check(old.node.getLocalZOrder() === 3, '1.x z order is independent of red color');
    position(old.node, 5, 8, '1.x root position');
    near(old.node.getContentSize().width, 80, '1.x width');
    near(old.node.getContentSize().height, 40, '1.x height');
    check(old.node.getUserObject().getActionTag() === 11, '1.x legacy ActionTimelineData');
    check(old.node.getComponent('ComExtensionData').getActionTag() === 11, '1.x native animation tag');
    var oldChild = old.node.getChildByTag(101);
    check(oldChild === old.node.getChildren()[0], 'stable native wrapper identity');
    check(old.action.getTimelines().length === 2, '1.x position and event timelines');
    check(old.action.duration === 60 && old.action.timeSpeed === 1, 'legacy timeline properties call native getters');
    check(old.action.getTimelines()[0].getFrames().length === 2, '1.x position frames');
    var eventTarget = {count: 0};
    old.action.setFrameEventCallFunc(function (frame) {
        check(this === eventTarget, 'frame event callback target');
        check(frame instanceof ccs.EventFrame && frame.getEvent() === 'midpoint', 'native EventFrame callback');
        this.count++;
    }, eventTarget);
    old.action.startWithTarget(old.node);
    old.action.gotoFrameAndPlay(0, 60, false);
    old.action.gotoFrameAndPause(30);
    position(oldChild, 39, 28, '1.x native interpolation');
    check(eventTarget.count === 1, '1.x native frame event delivery');
    check(old.action.getTimelines()[0].getFrames()[0].getNode() === oldChild, '1.x animation binds descendant tag');
    var oldAgain = ccs.load('studio/timeline-1.json');
    check(oldAgain.node !== old.node && oldAgain.action !== old.action, 'each load has independent native objects');
    position(oldAgain.node.getChildByTag(101), 9, 13, 'fresh 1.x child position');
    var oldWidget = ccs.load('studio/widget-timeline-1.json');
    check(oldWidget.node instanceof ccui.Layout && oldWidget.node.getChildren().length === 1, '1.x timeline widget has no duplicate children');
    check(oldWidget.node.getUserObject().getActionTag() === 31, '1.x timeline widget tag');
    oldWidget.action.startWithTarget(oldWidget.node);
    oldWidget.action.gotoFrameAndPause(30);
    position(oldWidget.node.getChildByTag(101), 39, 28, '1.x widget descendant interpolation');

    var controller = {count: 0, sender: null, onExportedClick: function (sender) {
        check(this === controller, 'exported controller receiver');
        this.count++;
        this.sender = sender;
    }};
    var modern = ccs.load('studio/timeline-2.json', controller);
    check(modern.node.getName() === 'new-root' && modern.node.getChildren().length === 3, '2.x hierarchy');
    position(modern.node, 7, 11, '2.x root position');
    var modernChild = modern.node.getChildByTag(201);
    var layout = ccui.LayoutComponent.bindLayoutComponent(modernChild);
    check(layout === ccui.LayoutComponent.bindLayoutComponent(modernChild), 'stable native layout component identity');
    near(layout.getPositionPercentX(), 0.1, '2.x PrePosition x');
    near(layout.getPositionPercentY(), 0.2, '2.x PrePosition y without PreSize');
    var nested = modern.node.getChildByTag(202);
    check(nested.getName() === 'nested-project' && nested.getChildByTag(301).getName() === 'nested-leaf', 'nested project JSON');
    position(nested, 5, 6, 'nested project export transform');
    var exportedWidget = modern.node.getChildByTag(203);
    check(exportedWidget instanceof ccui.Layout, 'native exported UI class');
    studioEmitClick(exportedWidget);
    check(controller.count === 1 && controller.sender === exportedWidget, 'real native controller click dispatch');
    var info = modern.action.getAnimationInfo('walk');
    check(info.name === 'walk' && info.startIndex === 0 && info.endIndex === 60, '2.x AnimationInfo native conversion');
    modern.action.startWithTarget(modern.node);
    modern.action.gotoFrameAndPause(30);
    position(modernChild, 50, 45, '2.x native interpolation');
    var flat = ccs.load('studio/flat-content-2.json');
    check(flat.node.getChildren().length === 3 && flat.action.getDuration() === 120, 'flat 2.x Content export variant');
    check(flat.node !== modern.node && flat.action !== modern.action, '2.x parser has no shared native object cache');
    check(ccs.load('studio/nested-2.json').action === null, 'project without animation');

    var binary = ccs.load('studio/timeline.csb');
    check(binary.node.getName() === 'csb-root' && binary.node.getTag() === 400, 'native CSB reader');
    var binaryChild = binary.node.getChildByTag(401);
    position(binaryChild, 15, 25, 'CSB exported position');
    check(binary.action.getDuration() === 60 && binary.action.getTimelines().length === 1, 'native CSB action');
    binary.action.startWithTarget(binary.node);
    binary.action.gotoFrameAndPause(30);
    position(binaryChild, 45, 40, 'CSB native interpolation');
    var binaryInfo = binary.action.getAnimationInfo('csb-walk');
    check(binaryInfo.name === 'csb-walk' && binaryInfo.endIndex === 60, 'CSB AnimationInfo');

    var scene = ccs.load('studio/scene-1.json');
    check(scene.node.getName() === 'scene-root' && scene.action === null, '1.x scene without timeline');
    position(scene.node.getChildByTag(501), 17, 19, '1.x scene child');
    check(ccs.sceneReader.getNodeByTag(501) === scene.node.getChildByTag(501), 'legacy sceneReader searches native tree');
    var ui = ccs.load('studio/ui-1.json');
    check(ui.node instanceof ccui.Layout && ui.node.getActionTag() === 61, '1.x UI parser');
    near(ui.node.getContentSize().width, 320, '1.x design width');
    near(ui.node.getContentSize().height, 240, '1.x design height');
    check(ui.node.isVisible() && !ui.node.isTouchEnabled(), '1.x sparse UI defaults');
    var uiAction = ccs.actionManager.getActionByName('studio/ui-1.json', 'ui-move');
    check(uiAction && uiAction.getName() === 'ui-move', '1.x ActionManager JSON dictionary conversion');
    near(uiAction.getTotalTime(), 1, '1.x ActionManager duration');
    uiAction.updateToFrameByTime(0.5);
    position(ui.node, 30, 15, '1.x ActionManager native interpolation');
    check(ccs.csLoader.getJsonPath !== undefined, 'native CSLoader methods survive legacy aliases');

    // This mode already has a renderer, so use a populated native BMFont and
    // its original font texture. Fixed v3 delegated setColor to the child but
    // left getColor white; consistent getters are an intentional extension.
    function rgb(value, r, g, b, message) {
        check(value.r === r && value.g === g && value.b === b,
            message + ': ' + JSON.stringify(value));
        check(!('a' in value), message + ' preserves the public RGB contract');
    }
    var bmfont = new cc.LabelBMFont('Test', 'legacy/js-tests/res/fonts/bitmapFontTest2.fnt');
    var bmInner = bmfont.getInnerLabel();
    check(bmInner === bmfont.getChildren()[0] && bmInner instanceof cc.Label,
        'BMFont color uses the actual native Label child');
    check(bmfont.getContentSize().width > 0 && bmfont.getContentSize().height > 0,
        'BMFont fixture has actual native font geometry');
    var bmGlyph = bmfont.getLetter(0);
    check(bmGlyph instanceof cc.Sprite && bmGlyph === bmInner.getLetter(0) && bmGlyph.getParent() === bmInner,
        'BMFont fixture exposes a real native glyph with canonical identity');
    bmfont.setOpacity(177);
    bmInner.setOpacity(133);
    bmfont.setColor(cc.color(84, 116, 148));
    rgb(bmfont.getColor(), 84, 116, 148, 'BMFont native getter follows setColor');
    rgb(cc.Node.prototype.getColor.call(bmfont), 84, 116, 148, 'native Node getter dispatches to Label tint');
    rgb(bmfont.color, 84, 116, 148, 'BMFont color property follows native tint');
    rgb(bmfont.getDisplayedColor(), 84, 116, 148, 'BMFont public displayed getter follows inner Label');
    check(bmfont.getOpacity() === 177 && bmInner.getOpacity() === 133 &&
        bmInner.getDisplayedOpacity() === Math.floor(133 * 177 / 255),
        'BMFont RGB writes preserve independent native container and Label opacity');
    bmfont.color = cc.color(52, 88, 124);
    rgb(bmfont.getColor(), 52, 88, 124, 'BMFont color property writes actual native tint');
    bmInner.setColor(cc.color(84, 116, 148));
    rgb(bmfont.getColor(), 84, 116, 148, 'BMFont getter observes direct native child recoloring');
    rgb(bmfont.color, 84, 116, 148, 'BMFont property observes direct native child recoloring');

    // Native actions bypass the JS setColor wrapper. The default BMFont mode
    // cascades opacity only, so native tint must explicitly preserve that path.
    var bmOpacityTint = cc.tintTo(1, 196, 92, 44);
    bmOpacityTint.startWithTarget(bmfont);
    bmOpacityTint.update(0.5);
    rgb(bmfont.getColor(), 140, 104, 96, 'opacity-only native TintTo starts at actual Label color');
    check(bmfont.getOpacity() === 177 && bmInner.getOpacity() === 133 &&
        bmInner.getDisplayedOpacity() === Math.floor(133 * 177 / 255),
        'opacity-only native TintTo preserves displayed alpha');
    bmOpacityTint.stop();
    bmfont.setColor(cc.color(84, 116, 148));

    var bmParent = new cc.Node();
    bmParent.setCascadeColorEnabled(true);
    bmfont.setCascadeColorEnabled(true);
    bmParent.addChild(bmfont);
    bmParent.setColor(cc.color(128, 192, 224));
    function checkBMCascade(r, g, b, message) {
        rgb(bmInner.getDisplayedColor(), Math.floor(r * 128 / 255), Math.floor(g * 192 / 255),
            Math.floor(b * 224 / 255), message + ' inner native state');
        rgb(bmfont.getDisplayedColor(), Math.floor(r * 128 / 255), Math.floor(g * 192 / 255),
            Math.floor(b * 224 / 255), message + ' public displayed state');
    }
    checkBMCascade(84, 116, 148, 'BMFont inherits parent tint once');
    for (var bmUpdate = 0; bmUpdate < 3; ++bmUpdate) {
        bmfont.setColor(cc.color(84, 116, 148));
        bmInner.setColor(cc.color(84, 116, 148));
        bmParent.setColor(cc.color(128, 192, 224));
        checkBMCascade(84, 116, 148, 'BMFont repeated cascade ' + bmUpdate + ' has no tint feedback');
    }
    bmParent.setCascadeOpacityEnabled(true);
    bmParent.setOpacity(128);
    check(bmfont.getOpacity() === 177 && bmfont.getDisplayedOpacity() === Math.floor(177 * 128 / 255) &&
        bmInner.getOpacity() === 133 &&
        bmInner.getDisplayedOpacity() === Math.floor(133 * Math.floor(177 * 128 / 255) / 255),
        'BMFont native opacity cascades independently through both levels');
    // Invoke the actual native TintTo start/update path deterministically. This
    // verifies its virtual native color read, without claiming frame/GPU output.
    var bmTint = cc.tintTo(1, 196, 92, 44);
    bmTint.startWithTarget(bmfont);
    bmTint.update(0.5);
    rgb(bmfont.getColor(), 140, 104, 96, 'native TintTo starts from actual Label color');
    rgb(bmInner.getColor(), 140, 104, 96, 'native TintTo updates actual Label color');
    checkBMCascade(140, 104, 96, 'native TintTo preserves single parent cascade');
    bmTint.stop();
    check(bmfont.getOpacity() === 177 && bmInner.getOpacity() === 133,
        'native TintTo preserves independent container and Label opacity');

    // Read the real native quad. These are endpoint, magnitude, opacity and
    // uncompressed-control contracts, not the original inset-ROI oracle.
    function quadEquals(layer, expected, message) {
        var actual = studioGradientQuad(layer);
        check(JSON.stringify(actual) === JSON.stringify(expected), message + ': ' + JSON.stringify(actual));
    }
    var endpoint = new cc.LayerGradient(cc.color(255, 0, 0, 255), cc.color(0, 255, 0, 255), cc.p(.9, .9));
    quadEquals(endpoint, [[255,0,0,255],[127,127,0,255],[127,127,0,255],[0,255,0,255]],
        'compressed gradient exact saturated endpoints');
    var gradient = new cc.LayerGradient(cc.color(17,83,149,77), cc.color(233,177,41,199), cc.p(.9,.9));
    gradient.setOpacity(93);
    var S = [17,83,149,28], E = [233,177,41,72], M = [125,130,95,50];
    var vectors = [
        [.9,.9,[S,M,M,E]], [-.9,.9,[M,S,E,M]], [.9,-.9,[M,E,S,M]], [-.9,-.9,[E,M,M,S]],
        [1,0,[S,E,S,E]], [0,-1,[E,E,S,S]],
        [3,-7,[[168,148,73,59],E,S,[81,111,116,41]]],
        [9e-30,9e-30,[S,M,M,E]], [9e30,9e30,[S,M,M,E]]
    ];
    vectors.forEach(function (sample, index) {
        gradient.setVector(cc.p(sample[0], sample[1]));
        quadEquals(gradient, sample[2], 'compressed native gradient direction ' + index);
        check(gradient.getStartOpacity() === 77 && gradient.getEndOpacity() === 199 &&
            gradient.getOpacity() === 93, 'gradient endpoint opacity remains independent ' + index);
    });
    var previousQuad = JSON.stringify(studioGradientQuad(gradient));
    gradient.setVector(cc.p(0, 0));
    check(JSON.stringify(studioGradientQuad(gradient)) === previousQuad, 'zero vector retains the previous quad');
    endpoint.setVector(cc.p(0, -1));
    endpoint.setCompressedInterpolation(false);
    quadEquals(endpoint, [[37,217,0,255],[37,217,0,255],[217,37,0,255],[217,37,0,255]],
        'uncompressed native gradient keeps original interpolation');

    fails('studio/unsupported.json', /Unsupported CocoStudio node type/);
    fails('studio/unsupported-3d.json', /Unsupported CocoStudio node type/);
    fails('studio/cycle-2.json', /Cyclic CocoStudio project reference/);
    fails('studio/missing.json', /does not exist/);
    cc.log('Studio native regression checks passed: ' + checks);
})();
function jsStudioSmokeResult() { return 0; }
