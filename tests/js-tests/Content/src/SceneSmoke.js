// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
var jsSceneFrames = 0;
var jsSceneEnteredCount = 0;
var jsSceneStageFrames = [0, 0];
var jsSceneStageActions = [false, false];
var jsSceneFirstExited = false;
var jsSceneFirstStopped = false;
var jsSceneFirstFramesAtExit = -1;
var JsSmokeScene = cc.Scene.extend({
    onEnter: function () {
        this._super();
        this._stage = ++jsSceneEnteredCount;
        var stage = this._stage;
        var size = cc.director.getWinSize();
        var title = new cc.LabelTTF("Axmol JavaScript Tests", "Arial", 32);
        title.setPosition(size.width / 2, size.height - 80);
        this.addChild(title);
        var button = new ccui.Button();
        button.setTitleText("UI · Scene · Actions · Scheduler");
        button.setTitleFontSize(24);
        button.setPosition(size.width / 2, size.height / 2);
        this.addChild(button);
        title.runAction(cc.sequence(
            cc.moveBy(0.1, cc.p(10, 0)),
            cc.callFunc(function () { jsSceneStageActions[stage - 1] = true; })
        ));
        this.scheduleUpdate();
        if (stage === 1) {
            this.scheduleOnce(function () {
                cc.director.runScene(new JsSmokeScene());
            }, 0.2);
        }
    },
    onExit: function () {
        this._super();
        if (this._stage === 1) {
            jsSceneFirstExited = true;
            jsSceneFirstStopped = !this.isRunning();
            jsSceneFirstFramesAtExit = jsSceneStageFrames[0];
        }
    },
    update: function (dt) {
        if (dt >= 0) {
            jsSceneFrames += 1;
            jsSceneStageFrames[this._stage - 1] += 1;
        }
    }
});
function jsSceneSmokeResult() {
    if (jsSceneEnteredCount !== 2) { return 10; }
    if (jsSceneFrames < 2) { return 11; }
    if (!jsSceneStageActions[0] || !jsSceneStageActions[1]) { return 12; }
    if (!jsSceneFirstExited || !jsSceneFirstStopped) { return 13; }
    if (jsSceneStageFrames[0] !== jsSceneFirstFramesAtExit) { return 14; }
    if (jsSceneStageFrames[0] < 2 || jsSceneStageFrames[1] < 2) { return 15; }
    cc.log("JavaScript scene smoke passed: " + jsSceneFrames + " frames; live replacement, first exit/native stop, " +
        "second enter, paused first update and both actions; stage frames=" + jsSceneStageFrames.join(","));
    return 0;
}
