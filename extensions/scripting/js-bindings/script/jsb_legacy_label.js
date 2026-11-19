/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* v3 font definitions and properties applied to the actual Axmol Label. */
(function () {
    if(!cc.Label)return;
    var p=cc.Label.prototype;
    // The native deprecated getStringLength is exactly getCharCount, including
    // Unicode character counting; do not substitute JavaScript string.length.
    p.getStringLength=p.getCharCount;
    // v3 jsval_to_std_string accepts primitive text, with null/undefined as empty.
    // Apply that contract at text entry points without changing overload selection
    // for unrelated native APIs.
    function textValue(value) {
        if(value==null)return '';
        if(typeof value==='string'||typeof value==='number'||typeof value==='boolean')return String(value);
        throw new TypeError('Label text expects a string, number or boolean');
    }
    function textArgument(owner,name,index) {
        var native=owner[name];if(typeof native!=='function')return;
        owner[name]=function () {
            var args=Array.prototype.slice.call(arguments);
            var textIndex=typeof index==='function'?index(args):index;
            if(args.length>textIndex)args[textIndex]=textValue(args[textIndex]);
            return native.apply(this,args);
        };
    }
    textArgument(cc.Label,'createWithBMFont',1);
    textArgument(cc.Label,'createWithSystemFont',0);
    textArgument(cc.Label,'createWithTTF',function (args) { return typeof args[0]==='object'?1:0; });
    textArgument(p,'setString',0);
    if(cc.LabelAtlas)textArgument(cc.LabelAtlas.prototype,'setString',0);
    p.enableStroke=p.enableOutline;
    p.setFontFillColor=p.setTextColor;
    p._getFillStyle=p.getTextColor;
    p._getAlignment=p.getHorizontalAlignment;
    p._setStringForSetter=p.setString;
    p.setBoundingWidth=function (width) { return this.setWidth(width); };
    p.setBoundingHeight=function (height) { return this.setHeight(height); };
    if(cc.LabelBMFont)cc.LabelBMFont.prototype._getBoundingWidth=p.getMaxLineWidth;
    p._getStrokeStyle=p.getEffectColor;
    p._getLineWidth=p.getOutlineSize;
    p._setStrokeStyle=function (color) {
        var width=this.getOutlineSize();
        this.enableOutline(color,width>0?width:1);
    };
    p._setLineWidth=function (width) { this.enableOutline(this.getEffectColor(),width); };
    p._getShadowOffsetX=function () { return this.getShadowOffset().x; };
    p._getShadowOffsetY=function () { return this.getShadowOffset().y; };
    p._getShadowOpacity=function () { return this.getShadowColor().a/255; };
    p._getShadowBlur=p.getShadowBlurRadius;
    p._setShadowOffsetX=function (x) {
        this.enableShadow(this.getShadowColor(),cc.p(x,this.getShadowOffset().y),this.getShadowBlurRadius());
    };
    p._setShadowOffsetY=function (y) {
        this.enableShadow(this.getShadowColor(),cc.p(this.getShadowOffset().x,y),this.getShadowBlurRadius());
    };
    p._setShadowOpacity=function (opacity) {
        var color=this.getShadowColor();color.a=opacity*255;
        this.enableShadow(color,this.getShadowOffset(),this.getShadowBlurRadius());
    };
    p._setShadowBlur=function (blur) { this.enableShadow(this.getShadowColor(),this.getShadowOffset(),blur); };

    function applyDefinition(label,text,definition) {
        var defaults=new cc.FontDefinition();
        function value(name) { return definition[name]===undefined?defaults[name]:definition[name]; }
        label.setSystemFontName(value('fontName'));
        label.setSystemFontSize(value('fontSize'));
        label.setDimensions(value('boundingWidth'),value('boundingHeight'));
        label.setHorizontalAlignment(value('textAlign'));
        label.setVerticalAlignment(value('verticalAlign'));
        label.setTextColor(value('fillStyle'));
        if(value('strokeEnabled'))label.enableOutline(value('strokeStyle'),value('lineWidth'));
        else label.disableEffect(cc.LabelEffect.OUTLINE);
        if(value('shadowEnabled'))label.enableShadow(cc.color(0,0,0,value('shadowOpacity')*255),
            cc.p(value('shadowOffsetX'),value('shadowOffsetY')),value('shadowBlur'));
        else label.disableEffect(cc.LabelEffect.SHADOW);
        label.setString(textValue(text));
        return true;
    }
    p.initWithStringAndTextDefinition=function (text,definition) {
        if(!this.init())return false;
        return applyDefinition(this,text,definition);
    };
    p.setTextDefinition=function (definition) { applyDefinition(this,this.getString(),definition); };
    p.getTextDefinition=function () {
        var definition=new cc.FontDefinition(),size=this.getDimensions(),offset=this.getShadowOffset();
        definition.fontName=this.getSystemFontName();definition.fontSize=this.getSystemFontSize();
        definition.textAlign=this.getHorizontalAlignment();definition.verticalAlign=this.getVerticalAlignment();
        definition.boundingWidth=size.width;definition.boundingHeight=size.height;
        definition.fillStyle=this.getTextColor();definition.strokeEnabled=this.getLabelEffectType()===cc.LabelEffect.OUTLINE;
        definition.strokeStyle=this.getEffectColor();definition.lineWidth=this.getOutlineSize();
        definition.shadowEnabled=this.isShadowEnabled();definition.shadowOffsetX=offset.x;definition.shadowOffsetY=offset.y;
        definition.shadowOpacity=this.getShadowColor().a/255;definition.shadowBlur=this.getShadowBlurRadius();
        return definition;
    };
    var previousTTF=cc.LabelTTF,previousCtor=p.ctor;
    function LegacyLabelTTF(text,font,size,dimensions,horizontal,vertical) {
        if(!(font instanceof cc.FontDefinition))return previousTTF.apply(null,arguments);
        var label=cc.Label.create();
        applyDefinition(label,text||'',font);
        return label;
    }
    Object.setPrototypeOf(LegacyLabelTTF,previousTTF);
    LegacyLabelTTF.prototype=previousTTF.prototype;
    LegacyLabelTTF.create=LegacyLabelTTF;
    LegacyLabelTTF.extend=previousTTF.extend;
    p.ctor=function (text,font) {
        if(font instanceof cc.FontDefinition)return this.initWithStringAndTextDefinition(text||'',font);
        return previousCtor.apply(this,arguments);
    };
    cc.LabelTTF=cc.labelTTF=LegacyLabelTTF;
})();

/* v3 Label::getLetter returned an empty LabelLetter for zero-size glyphs such as
   spaces; the modern Label returns null. Preserve that original whitespace
   contract without inventing letters for real glyphs. */
(function () {
    if (!cc.Label) return;
    var p = cc.Label.prototype;
    var nativeGetLetter = p.getLetter;
    if (typeof nativeGetLetter !== 'function') return;
    p.getLetter = function (index) {
        var letter = nativeGetLetter.call(this, index);
        if (letter || typeof this.getString !== 'function') return letter;
        var text = this.getString();
        var ch = text.charAt(index);
        if (ch === '' || !/\s/.test(ch)) return letter;
        // v3 cached one LabelLetter per index; do not allocate per frame.
        if (!this.__legacyEmptyLetters) this.__legacyEmptyLetters = {};
        if (!this.__legacyEmptyLetters[index]) {
            this.__legacyEmptyLetters[index] = new cc.Sprite();
            this.addChild(this.__legacyEmptyLetters[index]);
        }
        return this.__legacyEmptyLetters[index];
    };
})();
