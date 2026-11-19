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


/* Legacy constants and Margin copied from cocos2d-x/v3 jsb_cocos2d_ui.js.
 * Native constructor/property adaptations below use current Axmol classes. */
(function (global) {
    var cc = global.cc, ccui = global.ccui;
    if (!ccui) throw Error('The native UI module is not registered');
    if (ccui.LayoutGroup) ccui.Layout = ccui.LayoutGroup;
    if (ccui.InputField) ccui.TextField = ccui.InputField;
ccui.helper = ccui.Helper;

// =====================Constants=====================

/*
 * UILayout
 */
//layoutBackGround color type
ccui.Layout.BG_COLOR_NONE = 0;
ccui.Layout.BG_COLOR_SOLID = 1;
ccui.Layout.BG_COLOR_GRADIENT = 2;

//Layout type
ccui.Layout.ABSOLUTE = 0;
ccui.Layout.LINEAR_VERTICAL = 1;
ccui.Layout.LINEAR_HORIZONTAL = 2;
ccui.Layout.RELATIVE = 3;

//Layout clipping type
ccui.Layout.CLIPPING_STENCIL = 0;
ccui.Layout.CLIPPING_SCISSOR = 1;

ccui.Layout.BACKGROUND_IMAGE_ZORDER = -1;
ccui.Layout.BACKGROUND_RENDERER_ZORDER = -2;

/*
 * UILayoutDefine
 */
//LinearGravity
//old
ccui.LINEAR_GRAVITY_NONE = 0;
ccui.LINEAR_GRAVITY_LEFT = 1;
ccui.LINEAR_GRAVITY_TOP = 2;
ccui.LINEAR_GRAVITY_RIGHT = 3;
ccui.LINEAR_GRAVITY_BOTTOM = 4;
ccui.LINEAR_GRAVITY_CENTER_VERTICAL = 5;
ccui.LINEAR_GRAVITY_CENTER_HORIZONTAL = 6;
//new
ccui.LinearLayoutParameter.NONE = 0;
ccui.LinearLayoutParameter.LEFT = 1;
ccui.LinearLayoutParameter.TOP = 2;
ccui.LinearLayoutParameter.RIGHT = 3;
ccui.LinearLayoutParameter.BOTTOM = 4;
ccui.LinearLayoutParameter.CENTER_VERTICAL = 5;
ccui.LinearLayoutParameter.CENTER_HORIZONTAL = 6;

//RelativeAlign
//old
ccui.RELATIVE_ALIGN_NONE = 0;
ccui.RELATIVE_ALIGN_PARENT_TOP_LEFT = 1;
ccui.RELATIVE_ALIGN_PARENT_TOP_CENTER_HORIZONTAL = 2;
ccui.RELATIVE_ALIGN_PARENT_TOP_RIGHT = 3;
ccui.RELATIVE_ALIGN_PARENT_LEFT_CENTER_VERTICAL = 4;
ccui.RELATIVE_ALIGN_PARENT_CENTER = 5;
ccui.RELATIVE_ALIGN_PARENT_RIGHT_CENTER_VERTICAL = 6;
ccui.RELATIVE_ALIGN_PARENT_LEFT_BOTTOM = 7;
ccui.RELATIVE_ALIGN_PARENT_BOTTOM_CENTER_HORIZONTAL = 8;
ccui.RELATIVE_ALIGN_PARENT_RIGHT_BOTTOM = 9;

ccui.RELATIVE_ALIGN_LOCATION_ABOVE_LEFT = 10;
ccui.RELATIVE_ALIGN_LOCATION_ABOVE_CENTER = 11;
ccui.RELATIVE_ALIGN_LOCATION_ABOVE_RIGHT = 12;

ccui.RELATIVE_ALIGN_LOCATION_LEFT_TOP = 13;
ccui.RELATIVE_ALIGN_LOCATION_LEFT_CENTER = 14;
ccui.RELATIVE_ALIGN_LOCATION_LEFT_BOTTOM = 15;

ccui.RELATIVE_ALIGN_LOCATION_RIGHT_TOP = 16;
ccui.RELATIVE_ALIGN_LOCATION_RIGHT_CENTER = 17;
ccui.RELATIVE_ALIGN_LOCATION_RIGHT_BOTTOM = 18;

ccui.RELATIVE_ALIGN_LOCATION_BELOW_TOP = 19;
ccui.RELATIVE_ALIGN_LOCATION_BELOW_CENTER = 20;
ccui.RELATIVE_ALIGN_LOCATION_BELOW_BOTTOM = 21;

//new
ccui.RelativeLayoutParameter.NONE = 0;
ccui.RelativeLayoutParameter.PARENT_TOP_LEFT = 1;
ccui.RelativeLayoutParameter.PARENT_TOP_CENTER_HORIZONTAL = 2;
ccui.RelativeLayoutParameter.PARENT_TOP_RIGHT = 3;
ccui.RelativeLayoutParameter.PARENT_LEFT_CENTER_VERTICAL = 4;

ccui.RelativeLayoutParameter.CENTER_IN_PARENT = 5;

ccui.RelativeLayoutParameter.PARENT_RIGHT_CENTER_VERTICAL = 6;
ccui.RelativeLayoutParameter.PARENT_LEFT_BOTTOM = 7;
ccui.RelativeLayoutParameter.PARENT_BOTTOM_CENTER_HORIZONTAL = 8;
ccui.RelativeLayoutParameter.PARENT_RIGHT_BOTTOM = 9;

ccui.RelativeLayoutParameter.LOCATION_ABOVE_LEFTALIGN = 10;
ccui.RelativeLayoutParameter.LOCATION_ABOVE_CENTER = 11;
ccui.RelativeLayoutParameter.LOCATION_ABOVE_RIGHTALIGN = 12;
ccui.RelativeLayoutParameter.LOCATION_LEFT_OF_TOPALIGN = 13;
ccui.RelativeLayoutParameter.LOCATION_LEFT_OF_CENTER = 14;
ccui.RelativeLayoutParameter.LOCATION_LEFT_OF_BOTTOMALIGN = 15;
ccui.RelativeLayoutParameter.LOCATION_RIGHT_OF_TOPALIGN = 16;
ccui.RelativeLayoutParameter.LOCATION_RIGHT_OF_CENTER = 17;
ccui.RelativeLayoutParameter.LOCATION_RIGHT_OF_BOTTOMALIGN = 18;
ccui.RelativeLayoutParameter.LOCATION_BELOW_LEFTALIGN = 19;
ccui.RelativeLayoutParameter.LOCATION_BELOW_CENTER = 20;
ccui.RelativeLayoutParameter.LOCATION_BELOW_RIGHTALIGN = 21;

/*
 * LayoutParameter
 */
//layout parameter type
ccui.LayoutParameter.NONE = 0;
ccui.LayoutParameter.LINEAR = 1;
ccui.LayoutParameter.RELATIVE = 2;

//LayoutComponent
ccui.LayoutComponent.horizontalEdge = {};
ccui.LayoutComponent.horizontalEdge.NONE = 0;
ccui.LayoutComponent.horizontalEdge.LEFT = 1;
ccui.LayoutComponent.horizontalEdge.RIGHT = 2;
ccui.LayoutComponent.horizontalEdge.CENTER = 3;

ccui.LayoutComponent.verticalEdge = {};
ccui.LayoutComponent.verticalEdge.NONE = 0;
ccui.LayoutComponent.verticalEdge.BOTTOM = 1;
ccui.LayoutComponent.verticalEdge.TOP = 2;
ccui.LayoutComponent.verticalEdge.CENTER = 3;
/*
 * UIWidget
 */
//bright style
ccui.Widget.BRIGHT_STYLE_NONE = -1;
ccui.Widget.BRIGHT_STYLE_NORMAL = 0;
ccui.Widget.BRIGHT_STYLE_HIGH_LIGHT = 1;

//widget type
ccui.Widget.TYPE_WIDGET = 0;
ccui.Widget.TYPE_CONTAINER = 1;

//texture resource type
ccui.Widget.LOCAL_TEXTURE = 0;
ccui.Widget.PLIST_TEXTURE = 1;

//touch event type
ccui.Widget.TOUCH_BEGAN = 0;
ccui.Widget.TOUCH_MOVED = 1;
ccui.Widget.TOUCH_ENDED = 2;
ccui.Widget.TOUCH_CANCELED = 3;

//size type
ccui.Widget.SIZE_ABSOLUTE = 0;
ccui.Widget.SIZE_PERCENT = 1;

//position type
ccui.Widget.POSITION_ABSOLUTE = 0;
ccui.Widget.POSITION_PERCENT = 1;

//focus direction
ccui.Widget.LEFT = 0;
ccui.Widget.RIGHT = 1;
ccui.Widget.UP = 2;
ccui.Widget.DOWN = 3;

/*
 * UIListView
 */
//listView event type
ccui.ListView.EVENT_SELECTED_ITEM = 0;
ccui.ListView.ON_SELECTED_ITEM_START = 0;
ccui.ListView.ON_SELECTED_ITEM_END = 1;

//listView gravity
ccui.ListView.GRAVITY_LEFT = 0;
ccui.ListView.GRAVITY_RIGHT = 1;
ccui.ListView.GRAVITY_CENTER_HORIZONTAL = 2;
ccui.ListView.GRAVITY_TOP = 3;
ccui.ListView.GRAVITY_BOTTOM = 4;
ccui.ListView.GRAVITY_CENTER_VERTICAL = 5;

//list view magnetic type
ccui.ListView.MAGNETIC_NONE = 0;
ccui.ListView.MAGNETIC_CENTER = 1;
ccui.ListView.MAGNETIC_BOTH_END = 2;
ccui.ListView.MAGNETIC_LEFT = 3;
ccui.ListView.MAGNETIC_RIGHT = 4;
ccui.ListView.MAGNETIC_TOP = 5;
ccui.ListView.MAGNETIC_BOTTOM = 6;

/*
 * UIScrollView
 */
//ScrollView direction
ccui.ScrollView.DIR_NONE = 0;
ccui.ScrollView.DIR_VERTICAL = 1;
ccui.ScrollView.DIR_HORIZONTAL = 2;
ccui.ScrollView.DIR_BOTH = 3;

//ScrollView event
ccui.ScrollView.EVENT_SCROLL_TO_TOP = 0;
ccui.ScrollView.EVENT_SCROLL_TO_BOTTOM = 1;
ccui.ScrollView.EVENT_SCROLL_TO_LEFT = 2;
ccui.ScrollView.EVENT_SCROLL_TO_RIGHT = 3;
ccui.ScrollView.EVENT_SCROLLING = 4;
ccui.ScrollView.EVENT_BOUNCE_TOP = 5;
ccui.ScrollView.EVENT_BOUNCE_BOTTOM = 6;
ccui.ScrollView.EVENT_BOUNCE_LEFT = 7;
ccui.ScrollView.EVENT_BOUNCE_RIGHT = 8;
ccui.ScrollView.EVENT_CONTAINER_MOVED = 9;
ccui.ScrollView.EVENT_AUTOSCROLL_ENDED = 10;

ccui.ScrollView.MOVEDIR_TOP = 0;
ccui.ScrollView.MOVEDIR_BOTTOM = 1;
ccui.ScrollView.MOVEDIR_LEFT = 2;
ccui.ScrollView.MOVEDIR_RIGHT = 3;

/*
 * UIPageView
 */
//PageView event
ccui.PageView.EVENT_TURNING = 0;

//PageView touch direction
ccui.PageView.TOUCH_DIR_LEFT = 0;
ccui.PageView.TOUCH_DIR_RIGHT = 1;
ccui.PageView.TOUCH_DIR_UP = 2;
ccui.PageView.TOUCH_DIR_DOWN = 3;

//PageView direction
ccui.PageView.DIRECTION_LEFT = 0;
ccui.PageView.DIRECTION_RIGHT = 1;

/*
 * UIButton
 */
ccui.NORMAL_RENDERER_ZORDER = -2;
ccui.PRESSED_RENDERER_ZORDER = -2;
ccui.DISABLED_RENDERER_ZORDER = -2;
ccui.TITLE_RENDERER_ZORDER = -1;

ccui.Scale9Sprite.POSITIONS_CENTRE = 0;                //CCScale9Sprite.js
ccui.Scale9Sprite.POSITIONS_TOP = 1;
ccui.Scale9Sprite.POSITIONS_LEFT = 2;
ccui.Scale9Sprite.POSITIONS_RIGHT = 3;
ccui.Scale9Sprite.POSITIONS_BOTTOM = 4;
ccui.Scale9Sprite.POSITIONS_TOPRIGHT = 5;
ccui.Scale9Sprite.POSITIONS_TOPLEFT = 6;
ccui.Scale9Sprite.POSITIONS_BOTTOMRIGHT = 7;
ccui.Scale9Sprite.POSITIONS_BOTTOMLEFT = 8;

/*
 * UICheckBox
 */
//CheckBoxEvent type
ccui.CheckBox.EVENT_SELECTED = 0;
ccui.CheckBox.EVENT_UNSELECTED = 1;

//Render zorder
ccui.CheckBox.BOX_RENDERER_ZORDER = -1;
ccui.CheckBox.BOX_SELECTED_RENDERER_ZORDER = -1;
ccui.CheckBox.BOX_DISABLED_RENDERER_ZORDER = -1;
ccui.CheckBox.FRONT_CROSS_RENDERER_ZORDER = -1;
ccui.CheckBox.FRONT_CROSS_DISABLED_RENDERER_ZORDER = -1;

/*
 * UIImageView
 */
ccui.ImageView.RENDERER_ZORDER = -1;

/*
 * UILoadingBar
 */
//loadingBar Type
ccui.LoadingBar.TYPE_LEFT = 0;
ccui.LoadingBar.TYPE_RIGHT = 1;

ccui.LoadingBar.RENDERER_ZORDER = -1;

/*
 * UIRichElement
 */
//Rich element type
//ccui.RichElement.TYPE_TEXT = 0;
//ccui.RichElement.TYPE_IMAGE = 1;
//ccui.RichElement.TYPE_CUSTOM = 2;

/*
 * UISlider
 */
//Slider event type
ccui.Slider.EVENT_PERCENT_CHANGED = 0;
ccui.Slider.EVENT_PERCENTAGE_CHANGED = 0;
ccui.Slider.EVENT_SLIDEBALL_DOWN = 1;
ccui.Slider.EVENT_SLIDEBALL_UP = 2;
ccui.Slider.EVENT_SLIDEBALL_CANCEL = 3;

//Render zorder
ccui.Slider.BASEBAR_RENDERER_ZORDER = -3;
ccui.Slider.PROGRESSBAR_RENDERER_ZORDER = -2;
ccui.Slider.BALL_RENDERER_ZORDER = -1;

/*
 * UIText
 */
ccui.Text.RENDERER_ZORDER = -1;

/*
 * UITextAtlas
 */
ccui.TextAtlas.RENDERER_ZORDER = -1;

/*
 * UITextBMFont
 */
ccui.TextBMFont.RENDERER_ZORDER = -1;

/*
 * UITextField
 */
//TextField event
ccui.TextField.EVENT_ATTACH_WITH_IME = 0;
ccui.TextField.EVENT_DETACH_WITH_IME = 1;
ccui.TextField.EVENT_INSERT_TEXT = 2;
ccui.TextField.EVENT_DELETE_BACKWARD = 3;

ccui.TextField.RENDERER_ZORDER = -1;

/*
 * UIRadioButton
 */
ccui.RadioButton.EVENT_SELECTED = 0;
ccui.RadioButton.EVENT_UNSELECTED = 1;
ccui.RadioButtonGroup.EVENT_SELECT_CHANGED = 0;

/*
 * UIMargin
 */
ccui.Margin = cc.Class.extend({
    left: 0,
    top: 0,
    right: 0,
    bottom: 0,
    ctor: function () {
        if (arguments.length == 1) {
            var uiMargin = arguments[0];
            this.left = uiMargin.left;
            this.top = uiMargin.top;
            this.right = uiMargin.right;
            this.bottom = uiMargin.bottom;
        }
        if (arguments.length == 4) {
            this.left = arguments[0];
            this.top = arguments[1];
            this.right = arguments[2];
            this.bottom = arguments[3];
        }
    },
    setMargin: function (l, t, r, b) {
        this.left = l;
        this.top = t;
        this.right = r;
        this.bottom = b;
    },
    equals: function (target) {
        return (this.left == target.left && this.top == target.top && this.right == target.right && this.bottom == target.bottom);
    }
});

ccui.MarginZero = function(){
    return new ccui.Margin(0,0,0,0);
};


    function property(Type, name, getter, setter) {
        if (!Type || typeof Type.prototype[getter] !== 'function') return;
        var p = Type.prototype;
        cc.defineGetterSetter(p, name, p[getter], typeof p[setter] === 'function' ? p[setter] : undefined);
    }
    function properties(Type, map) {
        Object.keys(map).forEach(function (name) { property(Type, name, map[name][0], map[name][1]); });
    }
    function ctor(Type, fn) {
        if (!Type) return;
        Type.prototype.ctor = fn;
        // Base adapters can expose a non-writable inherited marker. Define an
        // own marker so the native constructor facade selects this typed ctor.
        Object.defineProperty(Type.prototype, '__legacyCtor', {value: true, configurable: true});
    }
    function alias(Type, oldName, newName) {
        if (Type && typeof Type.prototype[newName] === 'function') Type.prototype[oldName] = Type.prototype[newName];
    }
    if (ccui.Slider) {
        var nativeSliderPercent = ccui.Slider.prototype.setPercent;
        ccui.Slider.prototype.setPercent = function (value) {
            if (arguments.length !== 1) return nativeSliderPercent.apply(this, arguments);
            // v3 jsval_to_int32 first used JS::ToNumber, then truncated the
            // finite value for the real native int setter (e.g. sqrt(1/19)*100).
            // The historical C++ cast outside int32's range was undefined.
            value = Number(value);
            value = value < 0 ? Math.ceil(value) : Math.floor(value);
            if (!isFinite(value) || value < -2147483648 || value > 2147483647)
                throw new TypeError('Slider percentage must be a finite signed 32-bit number');
            return nativeSliderPercent.call(this, value);
        };
    }
    function dimension(object, method) {
        var value = object[method]();
        return cc.size(value.width === undefined ? value.x : value.width,
            value.height === undefined ? value.y : value.height);
    }
    function pair(Type, propertyName, read, write, component, other) {
        if (!Type || !Type.prototype[read] || !Type.prototype[write]) return;
        Object.defineProperty(Type.prototype, propertyName, {
            get: function () { return dimension(this, read)[component]; },
            set: function (value) { var size = dimension(this, read); size[component] = value; this[write](size); },
            enumerable: true, configurable: true
        });
    }
    function event(Type, method) {
        if (!Type || typeof Type.prototype[method] !== 'function') return;
        var native = Type.prototype[method];
        Type.prototype[method] = function (callback, receiver) {
            return native.call(this, receiver === undefined ? callback : callback.bind(receiver));
        };
    }

    alias(ccui.Widget, 'getSize', 'getContentSize');
    alias(ccui.Widget, 'setSize', 'setContentSize');
    alias(ccui.Widget, 'addNode', 'addChild');
    alias(ccui.Widget, 'ignoreContentAdaptWithSize', 'setAutoSize');
    alias(ccui.Widget, 'isIgnoreContentAdaptWithSize', 'isAutoSize');
    alias(ccui.Widget, 'getVirtualRenderer', 'getRenderNode');
    alias(ccui.Widget, 'getVirtualRendererSize', 'getPreferredSize');
    alias(ccui.Widget, 'setPropagateTouchEvents', 'setPropagatePointerEvents');
    alias(ccui.Widget, 'isPropagateTouchEvents', 'isPropagatePointerEvents');
    alias(ccui.Widget, 'setSwallowTouches', 'setSwallowPointerEvents');
    alias(ccui.Widget, 'isSwallowTouches', 'isSwallowPointerEvents');
    alias(ccui.CheckBox, 'getSelected', 'isSelected');
    alias(ccui.ScrollView, 'getBounceEnabled', 'isBounceEnabled');
    alias(ccui.ScrollView, 'getInertiaScrollEnabled', 'isInertiaScrollEnabled');
    alias(ccui.TextField, 'setPlaceHolder', 'setPlaceholderText');
    alias(ccui.TextField, 'getPlaceHolder', 'getPlaceholderText');
    alias(ccui.TextField, 'setPlaceHolderColor', 'setPlaceholderColor');
    alias(ccui.TextField, 'getPlaceHolderColor', 'getPlaceholderColor');
    alias(ccui.TextField, 'setPasswordStyleText', 'setPasswordChar');
    alias(ccui.TextField, 'getPasswordStyleText', 'getPasswordChar');
    alias(ccui.TextField, 'getStringLength', 'getCharCount');
    alias(ccui.TextField, 'setTouchSize', 'setTouchAreaSize');
    alias(ccui.TextField, 'getTouchSize', 'getTouchAreaSize');
    alias(ccui.Text, 'setText', 'setString');
    alias(ccui.Text, 'getStringValue', 'getString');
    alias(ccui.Text, 'setTouchScaleEnabled', 'setTouchScaleChangeEnabled');
    alias(ccui.Text, 'getTouchScaleEnabled', 'isTouchScaleChangeEnabled');
    alias(ccui.PageView, 'getCurPageIndex', 'getCurrentPageIndex');
    alias(ccui.PageView, 'setCurPageIndex', 'setCurrentPageIndex');
    alias(ccui.PageView, 'getPages', 'getItems');
    alias(ccui.PageView, 'getPage', 'getItem');
    alias(ccui.ListView, 'requestRefreshView', 'forceDoLayout');
    alias(ccui.ListView, 'refreshView', 'forceDoLayout');
    if (ccui.PageView && ccui.PageView.prototype.insertPage)
        ccui.PageView.prototype.addWidgetToPage = function (widget, index) { return this.insertPage(widget, index); };
    if (ccui.PageView) {
        // The pinned cocos2d-x/v3 UIPageView.cpp:189-207 explicitly deprecated
        // these APIs: setters warn, getters return 0/false. Preserve that actual
        // contract, without claiming a custom paging threshold is implemented.
        ccui.PageView.prototype.setCustomScrollThreshold = function (threshold) {
            if (typeof threshold !== 'number' || !isFinite(threshold)) throw TypeError('Invalid PageView threshold');
            cc.log('PageView::setCustomScrollThreshold() has no effect!');
        };
        ccui.PageView.prototype.getCustomScrollThreshold = function () { return 0; };
        ccui.PageView.prototype.setUsingCustomScrollThreshold = function (flag) {
            cc.log('PageView::setUsingCustomScrollThreshold() has no effect!');
        };
        ccui.PageView.prototype.isUsingCustomScrollThreshold = function () { return false; };
    }

    ctor(ccui.Widget, function () { return this.init(); });
    [ccui.RichText, ccui.Layout, ccui.ListView, ccui.PageView,
        ccui.ScrollView, ccui.HBox, ccui.VBox, ccui.RelativeBox,
        ccui.RadioButtonGroup].forEach(function (Type) {
        ctor(Type, function () { return this.init(); });
    });
    // Test-intent recovery extension matching native Slider::create(barTextureName,
    // normalBallTextureName, resType) in Slider.cpp:87-99 for UISliderTest.js:125,163.
    // Pinned upstream cocos2d-x/v3 script/ccui/jsb_ccui_create_apis.js:35 also bound
    // ccui.Slider._ctor as 0-arg only; this is not a native v3 binary regression.
    ctor(ccui.Slider, function (barTextureName, normalBallTextureName, texType) {
        if (arguments.length === 0 || (barTextureName === undefined && normalBallTextureName === undefined && texType === undefined))
            return this.init();
        if ((arguments.length !== 2 && arguments.length !== 3) || barTextureName === undefined || normalBallTextureName === undefined)
            throw new TypeError('ccui.Slider constructor expects 0, 2, or 3 arguments');
        var ok = ccui.Slider.prototype.init.call(this);
        if (!ok) return false;
        var resType = texType === undefined ? 0 : texType;
        this.loadBarTexture(barTextureName, resType);
        this.loadSlidBallTextureNormal(normalBallTextureName, resType);
        return ok;
    });
    ctor(ccui.Button, function (normal, pressed, disabled, texType) {
        var ok = normal === undefined ? ccui.Widget.prototype.init.call(this) :
            ccui.Button.prototype.init.call(this, normal, pressed || '', disabled || '', texType === undefined ? 0 : texType);
        this.setTouchEnabled(true);
        return ok;
    });
    [ccui.CheckBox, ccui.RadioButton].forEach(function (Type) {
        ctor(Type, function (background, backgroundSelected, cross, backgroundDisabled, crossDisabled, texType) {
            var ok;
            if (crossDisabled !== undefined)
                ok = this.init(background, backgroundSelected, cross, backgroundDisabled, crossDisabled, texType === undefined ? 0 : texType);
            else if (backgroundSelected !== undefined)
                ok = this.init(background, background, backgroundSelected, background, background, cross === undefined ? 0 : cross);
            else ok = ccui.Widget.prototype.init.call(this);
            this.setSelected(false);
            this.setTouchEnabled(true);
            return ok;
        });
    });
    ctor(ccui.ImageView, function (image, texType) {
        return image === undefined ? ccui.Widget.prototype.init.call(this) :
            ccui.ImageView.prototype.init.call(this, image, texType === undefined ? 0 : texType);
    });
    ctor(ccui.LoadingBar, function (texture, percentage) {
        var ok = ccui.Widget.prototype.init.call(this);
        if (texture !== undefined) this.loadTexture(texture);
        if (percentage !== undefined) this.setPercent(percentage);
        return ok;
    });
    ctor(ccui.TextAtlas, function (text, charMap, width, height, start) {
        var ok = ccui.Widget.prototype.init.call(this);
        if (start !== undefined) this.setProperty(text, charMap, width, height, start);
        return ok;
    });
    ctor(ccui.Text, function (text, font, fontSize) {
        return fontSize === undefined ? ccui.Widget.prototype.init.call(this) :
            ccui.Text.prototype.init.call(this, text, font, fontSize);
    });
    ctor(ccui.TextBMFont, function (text, file) {
        var ok = ccui.Widget.prototype.init.call(this);
        if (file !== undefined) { this.setFntFile(file); this.setString(text); }
        return ok;
    });
    ctor(ccui.TextField, function (placeholder, font, fontSize) {
        var ok = ccui.TextField.prototype.init.call(this, placeholder === undefined ? '' : placeholder,
            font === undefined ? 'Arial' : font, fontSize === undefined ? 24 : fontSize);
        this.setTouchEnabled(true);
        return ok;
    });
    if (ccui.TextField) {
        var fp = ccui.TextField.prototype;
        // Native factories and Studio-loaded fields also use the old Android
        // Editable contract once the legacy JS API owns their event callbacks.
        var nativeFieldCreate = ccui.TextField.create;
        ccui.TextField.create = function () {
            var field = nativeFieldCreate.apply(ccui.TextField, arguments);
            if (field) field._useLegacyIME();
            return field;
        };
        var nativeFieldClone = fp.clone;
        fp.clone = function () {
            var field = nativeFieldClone.apply(this, arguments);
            if (field) field._useLegacyIME();
            return field;
        };
        fp.setMaxLengthEnabled = function (enabled) {
            this.__legacyMaxLengthEnabled = !!enabled;
            this.setCharLimit(enabled ? this.getMaxLength() : 0);
        };
        fp.isMaxLengthEnabled = function () {
            return this.__legacyMaxLengthEnabled === undefined ? this.getCharLimit() > 0 : this.__legacyMaxLengthEnabled;
        };
        fp.setMaxLength = function (length) {
            if (!isFinite(length) || length < 0 || Math.floor(length) !== length || length > 0xffffffff)
                throw TypeError('TextField maximum length must be a nonnegative integer');
            this.__legacyMaxLength = length;
            if (this.isMaxLengthEnabled()) this.setCharLimit(length);
        };
        fp.getMaxLength = function () {
            return this.__legacyMaxLength === undefined ? this.getCharLimit() : this.__legacyMaxLength;
        };
    }

    // Cocos separated RGB and opacity; Axmol carries opacity in Color32.a.
    function elementArgs(args, text) {
        args = Array.prototype.slice.call(args);
        if (typeof args[2] === 'number') {
            var color = cc.color(args[1]); color.a = args[2];
            args[1] = color; args.splice(2, 1);
        }
        if (text) {
            if (args.length < 6) args[5] = 0;
            if (args.length < 7) args[6] = '';
        }
        return args;
    }
    [ccui.RichElementText, ccui.RichElementImage, ccui.RichElementCustomNode].forEach(function (Type) {
        if (!Type) return;
        var text = Type === ccui.RichElementText, nativeCreate = Type.create, nativeInit = Type.prototype.init;
        Type.create = function () { return nativeCreate.apply(Type, elementArgs(arguments, text)); };
        Type.prototype.init = function () { return nativeInit.apply(this, elementArgs(arguments, text)); };
        ctor(Type, function () { return arguments.length ? this.init.apply(this, arguments) : true; });
    });

    var wp = ccui.Widget.prototype;
    wp._getXPercent = function () { return this.getPositionPercent().x; };
    wp._getYPercent = function () { return this.getPositionPercent().y; };
    wp._setXPercent = function (x) { this.setPositionPercent(cc.p(x, this.getPositionPercent().y)); };
    wp._setYPercent = function (y) { this.setPositionPercent(cc.p(this.getPositionPercent().x, y)); };
    wp._getWidth = function () { return dimension(this, 'getContentSize').width; };
    wp._getHeight = function () { return dimension(this, 'getContentSize').height; };
    wp._setWidth = function (value) { this.setContentSize(cc.size(value, this._getHeight())); };
    wp._setHeight = function (value) { this.setContentSize(cc.size(this._getWidth(), value)); };
    properties(ccui.Widget, {
        xPercent: ['_getXPercent', '_setXPercent'], yPercent: ['_getYPercent', '_setYPercent'],
        width: ['_getWidth', '_setWidth'], height: ['_getHeight', '_setHeight'],
        enabled: ['isEnabled', 'setEnabled'], focused: ['isFocused', 'setFocused'],
        sizeType: ['getSizeType', 'setSizeType'], positionType: ['getPositionType', 'setPositionType'],
        widgetParent: ['getWidgetParent'], widgetType: ['getWidgetType'],
        touchEnabled: ['isTouchEnabled', 'setTouchEnabled'], updateEnabled: ['isUpdateEnabled', 'setUpdateEnabled'],
        bright: ['isBright', 'setBright'], actionTag: ['getActionTag', 'setActionTag'],
        flippedX: ['isFlippedX', 'setFlippedX'], flippedY: ['isFlippedY', 'setFlippedY'],
        childrenCount: ['getChildrenCount'],
        onFocusChanged: ['_getFocusChangedCallback', '_setFocusChangedCallback'],
        onNextFocusedWidget: ['_getNextFocusedWidgetCallback', '_setNextFocusedWidgetCallback']
    });
    pair(ccui.Widget, 'widthPercent', 'getSizePercent', 'setSizePercent', 'width');
    pair(ccui.Widget, 'heightPercent', 'getSizePercent', 'setSizePercent', 'height');
    properties(ccui.Layout, {clippingEnabled: ['isClippingEnabled', 'setClippingEnabled'],
        clippingType: ['getClippingType', 'setClippingType'], layoutType: ['getLayoutType', 'setLayoutType']});
    properties(ccui.Button, {titleText: ['getTitleText', 'setTitleText'], titleFontSize: ['getTitleFontSize', 'setTitleFontSize'],
        titleFontName: ['getTitleFontName', 'setTitleFontName'], titleFontColor: ['getTitleFontColor', 'setTitleFontColor'],
        pressedActionEnabled: ['isPressedActionEnabled', 'setPressedActionEnabled']});
    properties(ccui.CheckBox, {selected: ['isSelected', 'setSelected']});
    function font(Type, name, sizeGetter, sizeSetter, nameGetter, nameSetter) {
        if (!Type) return;
        Object.defineProperty(Type.prototype, name, {
            get: function () { return this[sizeGetter]() + "px '" + this[nameGetter]() + "'"; },
            set: function (value) {
                var match = /^(\d+(?:\.\d+)?)px\s+['"]?(.+?)['"]?$/.exec(value);
                if (match) { this[sizeSetter](parseFloat(match[1])); this[nameSetter](match[2]); }
            }, configurable: true, enumerable: true
        });
    }
    font(ccui.Button, 'titleFont', 'getTitleFontSize', 'setTitleFontSize', 'getTitleFontName', 'setTitleFontName');
    font(ccui.Text, 'font', 'getFontSize', 'setFontSize', 'getFontName', 'setFontName');
    font(ccui.TextField, 'font', 'getFontSize', 'setFontSize', 'getFontName', 'setFontName');
    properties(ccui.Text, {string: ['getString', 'setString'], stringLength: ['getStringLength'],
        fontName: ['getFontName', 'setFontName'], fontSize: ['getFontSize', 'setFontSize'],
        textAlign: ['getTextHorizontalAlignment', 'setTextHorizontalAlignment'],
        verticalAlign: ['getTextVerticalAlignment', 'setTextVerticalAlignment'],
        touchScaleEnabled: ['getTouchScaleEnabled', 'setTouchScaleEnabled']});
    pair(ccui.Text, 'boundingWidth', 'getTextAreaSize', 'setTextAreaSize', 'width');
    pair(ccui.Text, 'boundingHeight', 'getTextAreaSize', 'setTextAreaSize', 'height');
    [ccui.TextAtlas, ccui.TextBMFont].forEach(function (Type) { property(Type, 'string', 'getString', 'setString'); });
    properties(ccui.LoadingBar, {direction: ['getDirection', 'setDirection'], percent: ['getPercent', 'setPercent']});
    property(ccui.Slider, 'percent', 'getPercent', 'setPercent');
    properties(ccui.TextField, {maxLengthEnabled: ['isMaxLengthEnabled', 'setMaxLengthEnabled'],
        maxLength: ['getMaxLength', 'setMaxLength'], passwordEnabled: ['isPasswordEnabled', 'setPasswordEnabled'],
        string: ['getString', 'setString'], fontSize: ['getFontSize', 'setFontSize'],
        fontName: ['getFontName', 'setFontName'], placeHolder: ['getPlaceHolder', 'setPlaceHolder']});
    properties(ccui.ScrollView, {bounceEnabled: ['isBounceEnabled', 'setBounceEnabled'],
        inertiaScrollEnabled: ['isInertiaScrollEnabled', 'setInertiaScrollEnabled'], layoutType: ['getLayoutType', 'setLayoutType']});
    pair(ccui.ScrollView, 'innerWidth', 'getInnerContainerSize', 'setInnerContainerSize', 'width');
    pair(ccui.ScrollView, 'innerHeight', 'getInnerContainerSize', 'setInnerContainerSize', 'height');
    // Scale9Sprite.h keeps these deprecated methods as exact content-size
    // aliases. Legacy ControlButton Styling still calls them directly.
    alias(cc.Scale9Sprite, 'setPreferredSize', 'setContentSize');
    alias(cc.Scale9Sprite, 'getPreferredSize', 'getContentSize');
    // Native v3 exposed SLICED; current Axmol names the same mode SLICE.
    // Reuse the generated native value so both public spellings select it.
    var scale9RenderingType = ccui.Scale9Sprite.RenderingType;
    if (scale9RenderingType && typeof scale9RenderingType.SLICE === 'number' &&
        scale9RenderingType.SLICED === undefined)
        scale9RenderingType.SLICED = scale9RenderingType.SLICE;
    // The native v3 bootstrap implemented this deprecated API by constructing
    // a Sprite from the batch texture and using the real six-argument update.
    // Source: cocos2d-x/v3 script/ccui/jsb_cocos2d_ui.js (MIT).
    cc.Scale9Sprite.prototype.updateWithBatchNode = function (batchNode, originalRect, rotated, capInsets) {
        var sprite = new cc.Sprite(batchNode.getTexture());
        return this.updateWithSprite(sprite, originalRect, !!rotated, cc.p(0, 0),
            cc.size(originalRect.width, originalRect.height), capInsets);
    };
    properties(cc.Scale9Sprite, {opacityModifyRGB: ['isOpacityModifyRGB', 'setOpacityModifyRGB'],
        preferredSize: ['getPreferredSize', 'setPreferredSize'], capInsets: ['getCapInsets', 'setCapInsets'],
        insetLeft: ['getInsetLeft', 'setInsetLeft'], insetTop: ['getInsetTop', 'setInsetTop'],
        insetRight: ['getInsetRight', 'setInsetRight'], insetBottom: ['getInsetBottom', 'setInsetBottom']});
    ctor(cc.Scale9Sprite, function (file, rect, capInsets) {
        if (file === undefined) return this.init();
        rect = rect || cc.rect(); capInsets = capInsets || cc.rect();
        if (file instanceof cc.SpriteFrame) return this.initWithSpriteFrame(file, rect);
        var frame = cc.spriteFrameCache.getSpriteFrame(file);
        return frame ? this.initWithSpriteFrame(frame, rect) : this.initWithFile(file, rect, capInsets);
    });

    event(ccui.Widget, 'addTouchEventListener');
    event(ccui.Widget, 'addClickEventListener');
    [ccui.CheckBox, ccui.Slider, ccui.TextField, ccui.PageView, ccui.ScrollView,
        ccui.ListView, ccui.RadioButton, ccui.RadioButtonGroup].forEach(function (Type) { event(Type, 'addEventListener'); });
    if (ccui.TextField) {
        var nativeFieldEvent = ccui.TextField.prototype.addEventListener;
        ccui.TextField.prototype.addEventListener = function () {
            this._useLegacyIME();
            return nativeFieldEvent.apply(this, arguments);
        };
    }

    if (ccui.WebView) {
        ccui.WebView.EventType = {LOADING: 'loading', LOADED: 'load', ERROR: 'error', JS_EVALUATED: 'js'};
        var webLoad = ccui.WebView.prototype.loadURL;
        ccui.WebView.prototype.loadURL = function (url) {
            return /^https?:\/\//i.test(url) ? webLoad.call(this, url) : this.loadFile(url);
        };
        ccui.WebView.prototype.setEventListener = function (event, callback) {
            var setters = {loading: 'setOnShouldStartLoading', load: 'setOnDidFinishLoading',
                error: 'setOnDidFailLoading', js: 'setOnJSCallback'};
            if (!setters[event]) throw Error('Unknown WebView event: ' + event);
            return this[setters[event]](callback);
        };
        ctor(ccui.WebView, function (url) { var ok = this.init(); if (url !== undefined) this.loadURL(url); return ok; });
    }
    if (ccui.VideoPlayer) {
        alias(ccui.VideoPlayer, 'setFullScreenEnabled', '_setLegacyFullscreen');
        alias(ccui.VideoPlayer, 'isFullScreenEnabled', '_isLegacyFullscreen');
        ccui.VideoPlayer.EventType = {PLAYING: 'play', PAUSED: 'pause', STOPPED: 'stop', COMPLETED: 'complete'};
        var videoURL = ccui.VideoPlayer.prototype.setURL;
        ccui.VideoPlayer.prototype.setURL = function (url) {
            return /^https?:\/\//i.test(url) ? videoURL.call(this, url) : this.setFileName(url);
        };
        ccui.VideoPlayer.prototype.setEventListener = function (event, callback) {
            var self = this;
            if (!this.__legacyVideoEvents) {
                this.__legacyVideoEvents = {};
                this.addEventListener(function (sender, type) {
                    var names = ['play', 'pause', 'stop', 'complete'], fn = self.__legacyVideoEvents[names[type]];
                    if (fn) fn.call(self, sender);
                });
            }
            this.__legacyVideoEvents[event] = callback;
        };
        ctor(ccui.VideoPlayer, function (url) { var ok = this.init(); if (url !== undefined) this.setURL(url); return ok; });
    }
})(this);
