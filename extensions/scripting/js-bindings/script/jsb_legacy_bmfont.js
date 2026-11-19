/****************************************************************************
 Copyright (c) 2008-2010 Ricardo Quesada
 Copyright (c) 2010-2012 cocos2d-x.org
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2026 Axmol contributors.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
// Native composition follows cocos2d-x/v3 CCLabelBMFont.h/.cpp at e4b6a5ef8f.
// _LegacyBMFont also implements native LabelProtocol and BlendProtocol, which
// MenuItemLabel and other C++ consumers need in addition to the real child.
(function (global) {
    var cc = global.cc, Native = cc._LegacyBMFont;
    if (!Native) throw Error('The native legacy BMFont module is not registered');
    var native = Native.prototype;
    function textValue(value) {
        if (value == null) return '';
        if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return String(value);
        throw new TypeError('BMFont text expects a string, number or boolean');
    }
    var LabelBMFont = Native.extend({
        ctor: function (text, file, width, alignment, imageOffset) {
            if (this._super() === false) return false;
            this._label = this.getInnerLabel();
            // The native constructor has already created its actual Label
            // child. The old JS _ctor only initializes when a font is supplied.
            return file ? this.initWithString(text || '', file, width || 0,
                alignment === undefined ? cc.TEXT_ALIGNMENT_LEFT : alignment, imageOffset || cc.p(0, 0)) : true;
        },
        initWithString: function (text, file, width, alignment, imageOffset) {
            return native.initWithString.call(this, textValue(text), file, width === undefined ? 0 : width,
                alignment === undefined ? cc.TEXT_ALIGNMENT_LEFT : alignment, imageOffset || cc.p(0, 0));
        },
        setString: function (text) { return native.setString.call(this, textValue(text)); },
        setFntFile: function (file, imageOffsetOrRect, rotated) {
            var value = imageOffsetOrRect || cc.p(0, 0);
            var rect = value.width === undefined ? cc.rect(value.x, value.y, 0, 0) : value;
            return native.setFntFile.call(this, file, rect, !!rotated);
        },
        setBlendFunc: function (source, destination) {
            var value = destination === undefined ? source : {src: source, dst: destination};
            return native.setBlendFunc.call(this, value);
        },
        _setStringForSetter: function (text) { this.setString(text); },
        _getAlignment: function () { return this._label.getHorizontalAlignment(); },
        _getBoundingWidth: function () { return this._label.getMaxLineWidth(); },
        _getBoundingHeight: function () { return this._label.getDimensions().height; },
        setBoundingWidth: function (width) { this.setWidth(width); },
        setBoundingHeight: function (height) {
            var dimensions = this._label.getDimensions();
            this._label.setDimensions(dimensions.width, height);
            this.getContentSize(); // Synchronizes the actual native container.
        }
    });
    LabelBMFont.create = function () {
        var instance = Object.create(LabelBMFont.prototype);
        return LabelBMFont.apply(instance, arguments);
    };
    var p = LabelBMFont.prototype;
    cc.defineGetterSetter(p, 'color', p.getColor, p.setColor);
    cc.defineGetterSetter(p, 'opacityModifyRGB', p.isOpacityModifyRGB, p.setOpacityModifyRGB);
    cc.defineGetterSetter(p, 'string', p.getString, p.setString);
    cc.defineGetterSetter(p, 'textAlign', p._getAlignment, p.setAlignment);
    cc.defineGetterSetter(p, 'boundingWidth', p._getBoundingWidth, p.setBoundingWidth);
    cc.defineGetterSetter(p, 'boundingHeight', p._getBoundingHeight, p.setBoundingHeight);
    cc.defineGetterSetter(p, 'size', p.getContentSize, p.setContentSize);
    cc.LabelBMFont = cc.labelBMFont = LabelBMFont;
})(this);

/* v3 LabelBMFont exposed a per-character sprite for every character, including
   zero-size glyphs such as spaces, and the label color cascaded to those
   children. The native legacy container returns null for zero-size glyphs and
   keeps per-letter colors white for letters without an explicit child color;
   preserve the original getChildByTag contract with real cached sprites that
   inherit the label color when still white. */
(function () {
    if (!cc.LabelBMFont) return;
    var p = cc.LabelBMFont.prototype;
    var nativeGetChildByTag = p.getChildByTag;
    if (typeof nativeGetChildByTag !== 'function') return;
    function parentColor(label) {
        try {
            if (typeof label.getInnerLabel === 'function') return label.getInnerLabel().getColor();
            return label.getColor();
        } catch (e) { return null; }
    }
    function inherit(child, color) {
        if (!child || !color || typeof child.setColor !== 'function' || typeof child.getColor !== 'function') return child;
        var own = child.getColor();
        if (own && own.r === 255 && own.g === 255 && own.b === 255 &&
            (color.r !== 255 || color.g !== 255 || color.b !== 255)) child.setColor(color);
        return child;
    }
    p.getChildByTag = function (tag) {
        var color = parentColor(this);
        var child = nativeGetChildByTag.call(this, tag);
        if (child) return inherit(child, color);
        var text = typeof this.getString === 'function' ? this.getString() : '';
        var ch = text.charAt(tag);
        if (ch === '' || !/\s/.test(ch)) return child;
        if (!this.__legacyEmptyChildren) this.__legacyEmptyChildren = {};
        if (!this.__legacyEmptyChildren[tag]) {
            var empty = new cc.Sprite();
            this.addChild(empty);
            this.__legacyEmptyChildren[tag] = empty;
        }
        return inherit(this.__legacyEmptyChildren[tag], color);
    };
})();
