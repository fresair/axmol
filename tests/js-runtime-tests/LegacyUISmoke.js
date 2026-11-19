/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function (global) {
    function check(value, message) { if (!value) throw Error('Legacy UI: ' + message); }
    var order = [];
    var Derived = ccui.Widget.extend({
        ctor: function () { order.push('ctor'); this._super(); order.push('after ctor'); },
        init: function () { order.push('init'); var ok = this._super(); order.push('after init'); return ok; }
    });
    var widget = new Derived();
    widget.retain();
    check(order.join(',') === 'ctor,init,after init,after ctor', 'derived native init order');
    widget.width = 80; widget.height = 35;
    check(widget.width === 80 && widget.height === 35, 'native widget dimensions');
    widget.xPercent = 0.25; widget.yPercent = 0.75;
    widget.widthPercent = 0.5; widget.heightPercent = 0.6;
    check(widget.getPositionPercent().x === 0.25 && widget.getPositionPercent().y === 0.75,
        'native widget position percentages');
    var dimensions = widget.getSizePercent();
    check(dimensions.width === 0.5 && Math.abs(dimensions.height - 0.6) < 0.0001, 'native widget size percentages');
    check(ccui.Layout === ccui.LayoutGroup && ccui.TextField === ccui.InputField, 'real native class aliases');
    widget.ignoreContentAdaptWithSize(false);
    check(!widget.isIgnoreContentAdaptWithSize() && !widget.isAutoSize(), 'old content adaptation calls native auto-size');
    widget.setPropagateTouchEvents(true);
    check(widget.isPropagatePointerEvents() && widget.isPropagateTouchEvents(), 'old touch propagation reaches native pointer propagation');
    check(widget.isSwallowTouches() && widget.isSwallowPointerEvents(), 'new widgets swallow pointer presses by default');
    widget.setSwallowTouches(false);
    check(!widget.isSwallowPointerEvents(), 'old swallow flag reaches the native widget');
    var widgetClone = widget.clone();
    check(!widgetClone.isSwallowTouches(), 'native clone copies the swallow flag');

    var parameter = new ccui.LinearLayoutParameter();
    parameter.setGravity(ccui.LinearLayoutParameter.CENTER_HORIZONTAL);
    parameter.setMargin(new ccui.Margin(1, 2, 3, 4));
    check(new ccui.Margin(parameter.getMargin()).equals(new ccui.Margin(1, 2, 3, 4)), 'native object margin');
    parameter.setMargin(5, 6, 7, 8);
    var copy = parameter.clone();
    check(new ccui.Margin(copy.getMargin()).equals(new ccui.Margin(5, 6, 7, 8)), 'native numeric margin and clone');
    widget.setLayoutParameter(parameter);
    check(widget.getLayoutParameter() === parameter, 'layout parameter wrapper identity');

    var focusCalls = 0, nextCalls = 0, focusEvents = 0, inputEvents = 0, pointerEvents = 0, urlEvents = 0;
    var sliderEvents = 0, audioSliderUpdating = true;
    var swallowOnPress = false;
    var pointer = global.__LegacyNativePointerWidget;
    pointer.setContentSize(cc.size(80, 35));
    pointer.addTouchEventListener(function (sender, phase) {
        check(sender === pointer && phase === ccui.Widget.TOUCH_BEGAN, 'native widget press sender/phase');
        pointerEvents++;
        sender.setSwallowTouches(swallowOnPress);
    });
    var other = new ccui.Widget();
    other.retain();
    function focus(previous, current) {
        check(this === widget && previous === other && current === widget, 'native focus callback identities');
        focusCalls++;
    }
    function next(direction) { check(this === widget && direction === ccui.Widget.RIGHT, 'focus navigation callback receiver'); nextCalls++; return other; }
    widget.onFocusChanged = focus;
    widget.onNextFocusedWidget = next;
    // The host owns a real, uninitialized InputField for safe CPU-side API and
    // callback checks. Native init/labels/cursor/IME are verified by the APK.
    var input = global.__LegacyNativeInputField, inputReceiver = {};
    input.maxLengthEnabled = true; input.maxLength = 3;
    check(input.isMaxLengthEnabled() && input.getCharLimit() === 3, 'old max length reaches native character limit');
    input.maxLengthEnabled = false;
    check(input.getCharLimit() === 0 && input.maxLength === 3, 'disabled maximum preserves configured length');
    input.maxLengthEnabled = true;
    check(input.getCharLimit() === 3, 'reenabled maximum restores native limit');
    input.setTouchSize(cc.size(90, 30));
    var touch = input.getTouchSize();
    check(touch.width === 90 && touch.height === 30, 'old touch size reaches native InputField');
    input.passwordEnabled = true;
    check(input.isPasswordEnabled() && typeof input.getPasswordStyleText() === 'string', 'old password properties reach native fields');
    input.addEventListener(function (sender, type) {
        check(this === inputReceiver && sender === input && type === ccui.TextField.EVENT_INSERT_TEXT,
            'real InputField event sender/type/receiver');
        inputEvents++;
    }, inputReceiver);
    var slider = global.__LegacyNativeSlider, sliderReceiver = {};
    slider.addEventListener(function (sender, type) {
        check(this === sliderReceiver && sender === slider && type === ccui.Slider.EVENT_PERCENT_CHANGED &&
            type === ccui.Slider.EVENT_PERCENTAGE_CHANGED, 'real Slider percentage sender/type/receiver');
        sliderEvents++;
        // The unmodified native AudioControlTest uses this legacy spelling.
        switch (type) {
        case ccui.Slider.EVENT_PERCENTAGE_CHANGED:
            audioSliderUpdating = false;
            break;
        }
    }, sliderReceiver);
    var focusListener = cc.eventManager.addListener({event: cc.EventListener.FOCUS,
        onFocusChanged: function (previous, current) {
            check(previous === other && current === widget, 'EventDispatcher focus widget identities');
            focusEvents++;
        }}, 1);
    check(widget.onFocusChanged === focus && widget.onNextFocusedWidget === next, 'callback property getters');
    jsb.garbageCollect();

    var text = new ccui.RichElementText(1, cc.color.RED, 128, '中文😀', 'Arial', 12);
    var image = ccui.RichElementImage.create(2, cc.color.WHITE, 200, 'fixture.png');
    var custom = new ccui.RichElementCustomNode(3, cc.color.BLUE, 180, new cc.Node());
    check(text.equalType(ccui.RichElement.Type.TEXT) && image.equalType(ccui.RichElement.Type.IMAGE) &&
        custom.equalType(ccui.RichElement.Type.CUSTOM), 'old RichElement opacity signatures reach native element types');
    var rich = new ccui.RichText();
    rich.retain();
    check(rich.initWithXML("regular <i>italics</i><a href='axmol-test://example'>link</a>", {}, function (url) {
        check(this === rich && url === 'axmol-test://example', 'native RichText URL callback receiver/URL');
        urlEvents++;
    }), 'actual native RichText XML parser');
    rich.ignoreContentAdaptWithSize(false);
    rich.setContentSize(cc.size(120, 100));
    jsb.garbageCollect();
    global.LegacyUIProbe = {widget: widget, other: other, rich: rich, listener: focusListener,
        swallowOnPress: function (value) { swallowOnPress = value; },
        counts: function () { return {focus: focusCalls, next: nextCalls, events: focusEvents, input: inputEvents,
            pointer: pointerEvents, url: urlEvents, slider: sliderEvents, audioSliderUpdating: audioSliderUpdating}; }};
})(this);
