/*
 * Copyright (c) 2013-2016 Chukong Technologies Inc.
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

//
// CocosBuilder definitions
//

cc.BuilderReader = cc.BuilderReader || {};
cc.BuilderReader._resourcePath = "";

cc.BuilderReader._controllerClassCache = {};
cc.BuilderReader.registerController = function(controllerName, controller){
    cc.BuilderReader._controllerClassCache[controllerName] = cc.Class.extend(controller);
};

cc.BuilderReader.setResourcePath = function (rootPath) {
    cc.BuilderReader._resourcePath = rootPath;
};

cc.BuilderReader.load = function(file, owner, parentSize)
{
    // Load the node graph using the correct function
    var reader = cc._Reader.create();
    reader.setCCBRootPath(cc.BuilderReader._resourcePath);
    
    var node;

    if (parentSize)
    {
        node = reader.load(file, null, parentSize);
    }
    else
    {
        node = reader.load(file);
    }

    // Assign owner callbacks & member variables
    if (owner)
    {
        // Callbacks
        var ownerCallbackNames = reader.getOwnerCallbackNames();
        var ownerCallbackNodes = reader.getOwnerCallbackNodes();

        for (var i = 0; i < ownerCallbackNames.length; i++)
        {
            var callbackName = ownerCallbackNames[i];
            var callbackNode = ownerCallbackNodes[i];

            if (owner[callbackName] === undefined)
            {
                cc.log("Warning: " + "owner." + callbackName + " is undefined.");
            }
            else
            {
                if(callbackNode instanceof cc.ControlButton)
                {
                    var ownerCallbackControlEvents = reader.getOwnerCallbackControlEvents();
                    callbackNode.addTargetWithActionForControlEvents(owner, owner[callbackName], ownerCallbackControlEvents[i]);
                }
                else
                {
                    callbackNode.setCallback(owner[callbackName], owner);
                }
            }
        }

        // Variables
        var ownerOutletNames = reader.getOwnerOutletNames();
        var ownerOutletNodes = reader.getOwnerOutletNodes();

        for (var i = 0; i < ownerOutletNames.length; i++)
        {
            var outletName = ownerOutletNames[i];
            var outletNode = ownerOutletNodes[i];

            owner[outletName] = outletNode;
        }
    }

    var nodesWithAnimationManagers = reader.getNodesWithAnimationManagers();
    node._nodesWithAnimationManagers = nodesWithAnimationManagers;
    var animationManagersForNodes = reader.getAnimationManagersForNodes();

    var controllerClassCache = cc.BuilderReader._controllerClassCache;
    // Attach animation managers to nodes and assign root node callbacks and member variables
    for (var i = 0; i < nodesWithAnimationManagers.length; i++)
    {
        var innerNode = nodesWithAnimationManagers[i];
        var animationManager = animationManagersForNodes[i];

        innerNode.animationManager = animationManager;

        var controllerName = animationManager.getDocumentControllerName();
        if (!controllerName) continue;

        // Create a controller
        var controllerClass = controllerClassCache[controllerName];
        if(!controllerClass) throw "Can not find controller : " + controllerName;
        var controller = new controllerClass();
        controller.controllerName = controllerName;

        innerNode.controller = controller;
        controller.rootNode = innerNode;

        // Callbacks
        var documentCallbackNames = animationManager.getDocumentCallbackNames();
        var documentCallbackNodes = animationManager.getDocumentCallbackNodes();
        innerNode._documentCallbackNodes = documentCallbackNodes;

        for (var j = 0; j < documentCallbackNames.length; j++)
        {
            var callbackName = documentCallbackNames[j];
            var callbackNode = documentCallbackNodes[j];

            if (controller[callbackName] === undefined)
            {
                cc.log("Warning: " + controllerName + "." + callbackName + " is undefined.");
            }
            else
            {
                if(callbackNode instanceof cc.ControlButton)
                {
                    var documentCallbackControlEvents = animationManager.getDocumentCallbackControlEvents();
                    callbackNode.addTargetWithActionForControlEvents(controller, controller[callbackName], documentCallbackControlEvents[j]); 
                }
                else
                {
                    callbackNode.setCallback(controller[callbackName], controller);
                }
            }
        }


        // Variables
        var documentOutletNames = animationManager.getDocumentOutletNames();
        var documentOutletNodes = animationManager.getDocumentOutletNodes();

        for (var j = 0; j < documentOutletNames.length; j++)
        {
            var outletName = documentOutletNames[j];
            var outletNode = documentOutletNodes[j];

            controller[outletName] = outletNode;
        }

        if (typeof(controller.onDidLoadFromCCB) == "function")
        {
            controller.onDidLoadFromCCB();
        }

        // Setup timeline callbacks
        var keyframeCallbacks = animationManager.getKeyframeCallbacks();
        for (var j = 0; j < keyframeCallbacks.length; j++)
        {
            var callbackSplit = keyframeCallbacks[j].split(":");
            var callbackType = callbackSplit[0];
            var callbackName = callbackSplit[1];
            
            if (callbackType == 1) // Document callback
            {
                var callfunc = cc.CallFunc.create(controller[callbackName], controller);
                animationManager.setCallFunc(callfunc, keyframeCallbacks[j]);
            }
            else if (callbackType == 2 && owner) // Owner callback
            {
                var callfunc = cc.CallFunc.create(owner[callbackName], owner);
                animationManager.setCallFunc(callfunc, keyframeCallbacks[j]);
            }
        }
        
        // Start animation
        var autoPlaySeqId = animationManager.getAutoPlaySequenceId();
        if (autoPlaySeqId != -1)
        {
            animationManager.runAnimationsForSequenceIdTweenDuration(autoPlaySeqId, 0);
        }
    }

    return node;
};

cc.BuilderReader.loadAsScene = function(file, owner, parentSize)
{
    var node = cc.BuilderReader.load(file, owner, parentSize);
    var scene = cc.Scene.create();
    scene.addChild( node );

    return scene;
};

/* Axmol native extension adapters. Constructor and property names follow the
 * cocos2d-x/v3 extension helpers above; widgets retain their native behavior. */
(function (global) {
    var cc = global.cc, ui = global.ccui;
    if (cc.CCBReader) {
        cc._Reader = cc.CCBReader;
        if (cc.CCBReader.prototype.getAnimationManager)
            cc.CCBReader.prototype.getActionManager = cc.CCBReader.prototype.getAnimationManager;
    }
    if (cc.CCBAnimationManager) cc.BuilderAnimationManager = cc.CCBAnimationManager;
    if (ui) {
        if (ui.Scale9Sprite) cc.Scale9Sprite = ui.Scale9Sprite;
        if (ui.EditBox) cc.EditBox = ui.EditBox;
    }
    function property(Type, name, getter, setter) {
        if (!Type || typeof Type.prototype[getter] !== 'function') return;
        var prototype = Type.prototype;
        cc.defineGetterSetter(prototype, name, prototype[getter],
            typeof prototype[setter] === 'function' ? prototype[setter] : undefined);
    }
    function constructor(Type, fn) {
        if (!Type) return;
        Type.prototype.ctor = fn;
        Object.defineProperty(Type.prototype, '__legacyCtor', {value: true, configurable: true});
    }
    function sizeValue(value) {
        return cc.size(value.width === undefined ? value.x : value.width,
            value.height === undefined ? value.y : value.height);
    }
    function properties(Type, list) {
        Object.keys(list).forEach(function (name) {
            property(Type, name, list[name][0], list[name][1]);
        });
    }

    cc.SCROLLVIEW_DIRECTION_NONE = -1;
    cc.SCROLLVIEW_DIRECTION_HORIZONTAL = 0;
    cc.SCROLLVIEW_DIRECTION_VERTICAL = 1;
    cc.SCROLLVIEW_DIRECTION_BOTH = 2;
    cc.TABLEVIEW_FILL_TOPDOWN = 0;
    cc.TABLEVIEW_FILL_BOTTOMUP = 1;
    cc.CONTROL_EVENT_TOTAL_NUMBER = 9;
    ['TOUCH_DOWN', 'TOUCH_DRAG_INSIDE', 'TOUCH_DRAG_OUTSIDE', 'TOUCH_DRAG_ENTER',
        'TOUCH_DRAG_EXIT', 'TOUCH_UP_INSIDE', 'TOUCH_UP_OUTSIDE', 'TOUCH_CANCEL',
        'VALUECHANGED'].forEach(function (name, index) { cc['CONTROL_EVENT_' + name] = 1 << index; });
    ['NORMAL', 'HIGHLIGHTED', 'DISABLED', 'SELECTED'].forEach(function (name, index) {
        cc['CONTROL_STATE_' + name] = 1 << index;
    });
    cc.CONTROL_STATE_INITIAL = cc.CONTROL_STATE_SELECTED;
    cc.CONTROL_ZOOM_ACTION_TAG = 0xCCCB0001;
    cc.CONTROL_STEPPER_PARTMINUS = 0;
    cc.CONTROL_STEPPER_PARTPLUS = 1;
    cc.CONTROL_STEPPER_PARTNONE = 2;
    cc.CONTROL_STEPPER_LABELCOLOR_ENABLED = cc.color(55, 55, 55);
    cc.CONTROL_STEPPER_LABELCOLOR_DISABLED = cc.color(147, 147, 147);
    cc.CONTROL_STEPPER_LABELFONT = 'CourierNewPSMT';
    cc.AUTOREPEAT_DELTATIME = 0.15;
    cc.AUTOREPEAT_INCREASETIME_INCREMENT = 12;
    ['DEFAULT', 'DONE', 'SEND', 'SEARCH', 'GO', 'NEXT'].forEach(function (name, index) {
        cc['KEYBOARD_RETURNTYPE_' + name] = index;
    });
    ['ANY', 'EMAILADDR', 'NUMERIC', 'PHONENUMBER', 'URL', 'DECIMAL', 'SINGLELINE'].forEach(function (name, index) {
        cc['EDITBOX_INPUT_MODE_' + name] = index;
    });
    ['PASSWORD', 'SENSITIVE', 'INITIAL_CAPS_WORD', 'INITIAL_CAPS_SENTENCE',
        'INITIAL_CAPS_ALL_CHARACTERS', 'LOWERCASE_ALL_CHARACTERS'].forEach(function (name, index) {
        cc['EDITBOX_INPUT_FLAG_' + name] = index;
    });

    properties(cc.Control, {
        opacityModifyRGB: ['isOpacityModifyRGB', 'setOpacityModifyRGB'], state: ['getState'],
        enabled: ['isEnabled', 'setEnabled'], selected: ['isSelected', 'setSelected'],
        highlighted: ['isHighlighted', 'setHighlighted']
    });
    constructor(cc.Control, function () { return this.init(); });
    if (cc.ControlButton && cc.ControlButton.prototype.doesAdjustBackgroundImage)
        cc.ControlButton.prototype.getAdjustBackgroundImage = cc.ControlButton.prototype.doesAdjustBackgroundImage;
    properties(cc.ControlButton, {
        color: ['getColor', 'setColor'], opacity: ['getOpacity', 'setOpacity'],
        adjustBackgroundImage: ['getAdjustBackgroundImage', 'setAdjustBackgroundImage'],
        zoomOnTouchDown: ['getZoomOnTouchDown', 'setZoomOnTouchDown'],
        preferredSize: ['getPreferredSize', 'setPreferredSize'],
        labelAnchor: ['getLabelAnchorPoint', 'setLabelAnchorPoint']
    });
    constructor(cc.ControlButton, function (label, background, fontSize, fontName, autoSize) {
        if (label === undefined) return this.init();
        // The original Styling case passes null font arguments and false here.
        // Preserve v3's explicit auto-size branch before its font overload.
        if (autoSize !== undefined && background)
            return this.initWithLabelAndBackgroundSprite(label, background, autoSize);
        if (typeof label === 'string' && typeof background === 'string' && typeof fontSize === 'number')
            return this.initWithTitleAndFontNameAndFontSize(label, background, fontSize);
        if (fontName !== undefined && fontSize !== undefined)
            return this.initWithTitleAndFontNameAndFontSize(label, fontName, fontSize);
        if (background !== undefined)
            return this.initWithLabelAndBackgroundSprite(label, background, autoSize === undefined ? true : autoSize);
        return this.initWithBackgroundSprite(label);
    });
    constructor(cc.ControlColourPicker, function () { return this.init(); });
    properties(cc.ControlColourPicker, {color: ['getColor', 'setColor'], background: ['getBackground']});
    properties(cc.ControlHuePicker, {
        hue: ['getHue', 'setHue'], huePercent: ['getHuePercentage', 'setHuePercentage'],
        background: ['getBackground'], slider: ['getSlider'], startPos: ['getStartPos']
    });
    properties(cc.ControlSaturationBrightnessPicker, {
        saturation: ['getSaturation'], brightness: ['getBrightness'], background: ['getBackground'],
        overlay: ['getOverlay'], shadow: ['getShadow'], slider: ['getSlider'], startPos: ['getStartPos']
    });
    properties(cc.ControlPotentiometer, {
        value: ['getValue', 'setValue'], minValue: ['getMinimumValue', 'setMinimumValue'],
        maxValue: ['getMaximumValue', 'setMaximumValue'], progressTimer: ['getProgressTimer', 'setProgressTimer'],
        thumbSprite: ['getThumbSprite', 'setThumbSprite'], prevLocation: ['getPreviousLocation', 'setPreviousLocation']
    });
    constructor(cc.ControlPotentiometer, function (background, progress, thumb) {
        if (!arguments.length) return true;
        if (arguments.length !== 3) throw Error('ControlPotentiometer requires background, progress and thumb resources');
        return this.initWithTrackSprite_ProgressTimer_ThumbSprite(new cc.Sprite(background),
            new cc.ProgressTimer(new cc.Sprite(progress)), new cc.Sprite(thumb));
    });
    properties(cc.ControlSlider, {
        value: ['getValue', 'setValue'], minValue: ['getMinimumValue', 'setMinimumValue'],
        maxValue: ['getMaximumValue', 'setMaximumValue'], minAllowedValue: ['getMinimumAllowedValue', 'setMinimumAllowedValue'],
        maxAllowedValue: ['getMaximumAllowedValue', 'setMaximumAllowedValue'], thumbSprite: ['getThumbSprite'],
        progressSprite: ['getProgressSprite'], backgroundSprite: ['getBackgroundSprite']
    });
    constructor(cc.ControlSlider, function (background, progress, thumb, selectedThumb) {
        if (!arguments.length) return true;
        function sprite(value) { return typeof value === 'string' ? new cc.Sprite(value) : value; }
        if (arguments.length !== 3 && arguments.length !== 4) throw Error('ControlSlider requires three or four sprites/resources');
        if (selectedThumb !== undefined)
            return this.initWithSprites(sprite(background), sprite(progress), sprite(thumb), sprite(selectedThumb));
        return this.initWithSprites(sprite(background), sprite(progress), sprite(thumb));
    });
    properties(cc.ControlStepper, {
        wraps: ['getWraps', 'setWraps'], value: ['getValue', 'setValue'],
        minValue: ['getMinimumValue', 'setMinimumValue'], maxValue: ['getMaximumValue', 'setMaximumValue'],
        stepValue: ['getStepValue', 'setStepValue'], continuous: ['isContinuous'],
        minusSprite: ['getMinusSprite', 'setMinusSprite'], plusSprite: ['getPlusSprite', 'setPlusSprite'],
        minusLabel: ['getMinusLabel', 'setMinusLabel'], plusLabel: ['getPlusLabel', 'setPlusLabel']
    });
    constructor(cc.ControlStepper, function (minus, plus) {
        if (!arguments.length) return true;
        return this.initWithMinusSpriteAndPlusSprite(minus, plus);
    });
    constructor(cc.ControlSwitch, function (mask, on, off, thumb, onLabel, offLabel) {
        if (!arguments.length) return true;
        if (arguments.length === 4) return this.initWithMaskSprite(mask, on, off, thumb);
        return this.initWithMaskSprite(mask, on, off, thumb, onLabel, offLabel);
    });
    property(cc.ControlSwitch, 'on', 'isOn', 'setOn');

    constructor(cc.ScrollView, function (size, container) {
        if (size === undefined) return this.init();
        size = sizeValue(size);
        return container === undefined ? this.initWithViewSize(size) : this.initWithViewSize(size, container);
    });
    if (cc.ScrollView) {
        var sp = cc.ScrollView.prototype, addChild = sp.addChild;
        if (addChild) sp.addChild = function (child, z, tag) {
            return addChild.call(this, child, z === undefined ? child.getLocalZOrder() : z,
                tag === undefined ? child.getTag() : tag);
        };
        sp._getWidth = function () { return sizeValue(this.getViewSize()).width; };
        sp._getHeight = function () { return sizeValue(this.getViewSize()).height; };
        sp._setWidth = function (width) { var size = sizeValue(this.getViewSize()); size.width = width; this.setViewSize(size); };
        sp._setHeight = function (height) { var size = sizeValue(this.getViewSize()); size.height = height; this.setViewSize(size); };
        properties(cc.ScrollView, {width: ['_getWidth', '_setWidth'], height: ['_getHeight', '_setHeight'],
            direction: ['getDirection', 'setDirection'], contentOffset: ['getContentOffset', 'setContentOffset'],
            zoomScale: ['getZoomScale', 'setZoomScale']});
    }
    constructor(cc.TableView, function () { return this._init.apply(this, arguments); });
    property(cc.TableViewCell, 'idx', 'getIdx', 'setIdx');

    if (cc.EditBox) {
        var ep = cc.EditBox.prototype;
        ep.setString = ep.setText;
        ep.getString = ep.getText;
        properties(cc.EditBox, {string: ['getText', 'setText'], fontName: ['getFontName', 'setFontName'],
            fontSize: ['getFontSize', 'setFontSize'], fontColor: ['getFontColor', 'setFontColor'],
            placeHolder: ['getPlaceHolder', 'setPlaceHolder'], maxLength: ['getMaxLength', 'setMaxLength'],
            inputMode: ['getInputMode', 'setInputMode'], inputFlag: ['getInputFlag', 'setInputFlag']});
        constructor(cc.EditBox, function (size, normal, pressed, disabled) {
            if (typeof normal === 'string')
                return this.initWithSizeAndTexture(size, normal, pressed || '', disabled || '');
            if (pressed !== undefined || disabled !== undefined)
                return this.initWithSizeAndBackgroundSprite(size, normal, pressed || null, disabled || null);
            return this.initWithSizeAndBackgroundSprite(size, normal);
        });
    }
})(this);
