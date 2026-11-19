// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// The upstream files are unchanged. This harness selects their headless cases.
var legacySourceState = {rect: false, updatePriority: false, pauseResume: false,
    unscheduleAll: false, logs: [], capture: false};
var legacySourceOriginalLog = cc.log;
cc.log = function () {
    if (legacySourceState.capture)
        legacySourceState.logs.push(String(arguments[0]));
    return legacySourceOriginalLog.apply(cc, arguments);
};
function legacySourceCheck(value, message) {
    if (!value) throw new Error('Original v3 js-tests: ' + message);
}
function legacySourcePrepare() {
    // Run the original test method and all of its assertions without the
    // GUI base-layer constructor or onEnter navigation controls.
    var rectCase = Object.create(RectUnitTest.prototype);
    legacySourceCheck(rectCase.compareResults(rectCase.getCurrentResult(),
        rectCase.getExpectedResult()), 'original RectUnitTest expected result');
    legacySourceState.rect = true;

    legacySourceParent = new cc.Node();
    legacySourceParent.retain();
    legacySourceNodes = [];
    var cases = [['---', 50], ['3rd', 0], ['1st', -10],
        ['4th', 10], ['5th', 20], ['2nd', -5]];
    for (var i = 0; i < cases.length; ++i) {
        // This is the original TestNode constructor, including its _super,
        // init and scheduleUpdateWithPriority calls, with its original update.
        var node = new TestNode(cases[i][0], cases[i][1]);
        legacySourceCheck(node instanceof cc.Node && node._pString === cases[i][0],
            'original TestNode native construction and JS property');
        legacySourceParent.addChild(node);
        legacySourceNodes.push(node);
    }
    legacySourceCheck(legacySourceParent.children.length === 6,
        'native parent owns all original TestNode instances');
    legacySourceParent.onEnter();
    for (var n = 0; n < legacySourceNodes.length; ++n)
        legacySourceCheck(legacySourceNodes[n].isRunning(), 'native child onEnter');
    legacySourceState.capture = true;
    return 0;
}
function legacySourceVerifyPriority() {
    legacySourceCheck(JSON.stringify(legacySourceState.logs) ===
        JSON.stringify(['1st', '2nd', '3rd', '4th', '5th', '---']),
        'original TestNode updates use native Scheduler priorities: ' +
        JSON.stringify(legacySourceState.logs));
    legacySourceState.updatePriority = true;
    legacySourceState.logs.length = 0;
    legacySourceParent.onExit();
    return 0;
}
function legacySourceVerifyPaused() {
    legacySourceCheck(legacySourceState.logs.length === 0,
        'native onExit pauses original update callbacks');
    legacySourceParent.onEnter();
    return 0;
}
function legacySourceVerifyResumed() {
    legacySourceCheck(JSON.stringify(legacySourceState.logs) ===
        JSON.stringify(['1st', '2nd', '3rd', '4th', '5th', '---']),
        'native onEnter resumes original update callbacks');
    legacySourceState.pauseResume = true;
    legacySourceState.logs.length = 0;
    // Use the unmodified original removal routine against this native parent.
    SchedulerUpdate.prototype.onRemoveUpdates.call(legacySourceParent, 0);
    return 0;
}
function legacySourceFinish() {
    legacySourceCheck(legacySourceState.logs.length === 0,
        'original SchedulerUpdate.onRemoveUpdates unschedules every child');
    legacySourceState.unscheduleAll = true;
    legacySourceState.capture = false;
    cc.log = legacySourceOriginalLog;
    legacySourceParent.onExit();
    legacySourceParent.cleanup();
    legacySourceParent.release();
    legacySourceNodes = null;
    legacySourceParent = null;
    legacySourceCheck(legacySourceState.rect && legacySourceState.updatePriority &&
        legacySourceState.pauseResume && legacySourceState.unscheduleAll,
        'all selected original test cases completed');
    return 0;
}
