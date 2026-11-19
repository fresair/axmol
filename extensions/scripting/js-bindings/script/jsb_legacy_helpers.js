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


/* Pure JavaScript helpers retained from cocos2d-x/v3; see legacy-helpers-provenance.json. */
(function () {

// jsb_prepare.js:174-218 — original prototype and base-method inheritance.
cc.inherits = function (childCtor, parentCtor) {
    /** @constructor */
    function tempCtor() {};
    tempCtor.prototype = parentCtor.prototype;
    childCtor.superClass_ = parentCtor.prototype;
    childCtor.prototype = new tempCtor();
    childCtor.prototype.constructor = childCtor;

    // Copy "static" method, but doesn't generate subclasses.
//  for( var i in parentCtor ) {
//      childCtor[ i ] = parentCtor[ i ];
//  }
};
cc.base = function(me, opt_methodName, var_args) {
    var caller = arguments.callee.caller;
    if (caller.superClass_) {
        // This is a constructor. Call the superclass constructor.
        ret =  caller.superClass_.constructor.apply( me, Array.prototype.slice.call(arguments, 1));
        return ret;
    }

    var args = Array.prototype.slice.call(arguments, 2);
    var foundCaller = false;
    for (var ctor = me.constructor;
        ctor; ctor = ctor.superClass_ && ctor.superClass_.constructor) {
        if (ctor.prototype[opt_methodName] === caller) {
            foundCaller = true;
        } else if (foundCaller) {
            return ctor.prototype[opt_methodName].apply(me, args);
        }
    }

    // If we did not find the caller in the prototype chain,
    // then one of two things happened:
    // 1) The caller is an instance method.
    // 2) This method was not called by the right caller.
    if (me[opt_methodName] === caller) {
        return me.constructor.prototype[opt_methodName].apply(me, args);
    } else {
        throw Error(
                    'cc.base called from a method of one name ' +
                    'to a method of a different name');
    }
};


// jsb_cocos2d.js:1285-1305 — the original native LoaderScene contract.
// Cocos2d-html5 supports multi scene resources preloading.
// This is a compatible function for JSB.
cc.Loader = cc.Class.extend({
    initWith:function (resources, selector, target) {
        if (selector) {
            this._selector = selector;
            this._target = target;
        }
        this._selector.call(this._target);
    }
});

cc.Loader.preload = function (resources, selector, target) {
    if (!this._instance) {
        this._instance = new cc.Loader();
    }
    this._instance.initWith(resources, selector, target);
    return this._instance;
};

cc.LoaderScene = cc.Loader;

// jsb_cocos2d.js:611-1284
//
// Reusable objects
//
cc._reuse_p = [ {x:0, y:0}, {x:0,y:0}, {x:0,y:0}, {x:0,y:0} ];
cc._reuse_p_index = 0;
cc._reuse_size = {width:0, height:0};
cc._reuse_rect = {x:0, y:0, width:0, height:0};
cc._reuse_color3b = {r:255, g:255, b:255 };
cc._reuse_color4b = {r:255, g:255, b:255, a:255 };



//
// Basic structure : Point
//
cc.p = function( x, y )
{
    if (x == undefined)
        return {x: 0, y: 0};
    if (y == undefined)
        return {x: x.x, y: x.y};
    return {x:x, y:y};
};
cc._p = function( x, y )
{
    if( cc._reuse_p_index == cc._reuse_p.length )
        cc._reuse_p_index = 0;

    var p = cc._reuse_p[ cc._reuse_p_index];
    cc._reuse_p_index++;
    p.x = x;
    p.y = y;
    return p;
};

cc.pointEqualToPoint = function (point1, point2) {
    return ((point1.x == point2.x) && (point1.y == point2.y));
};

cc.PointZero = function () {
    return cc.p(0, 0);
};


/**
 * Unrotates two points.
 * @param  {cc.Point} v1
 * @param  {cc.Point} v2
 * @return {cc.Point}
 */
cc.pUnrotate = function (v1, v2) {
    return cc.p(v1.x * v2.x + v1.y * v2.y, v1.y * v2.x - v1.x * v2.y);
};

/**
 * Converts radians to a normalized vector.
 * @param {Number} a
 * @return {cc.Point}
 */
cc.pForAngle = function (a) {
    return cc.p(Math.cos(a), Math.sin(a));
};

/**
 * Converts a vector to radians.
 * @param {cc.Point} v
 * @return {Number}
 */
cc.pToAngle = function (v) {
    return Math.atan2(v.y, v.x);
};

/**
 * Clamp a value between from and to.
 * @param {Number} value
 * @param {Number} min_inclusive
 * @param {Number} max_inclusive
 * @return {Number}
 */
cc.clampf = function (value, min_inclusive, max_inclusive) {
    if (min_inclusive > max_inclusive) {
        var temp = min_inclusive;
        min_inclusive = max_inclusive;
        max_inclusive = temp;
    }
    return value < min_inclusive ? min_inclusive : value < max_inclusive ? value : max_inclusive;
};

/**
 * Quickly convert cc.Size to a cc.Point
 * @param {cc.Size} s
 * @return {cc.Point}
 */
cc.pFromSize = function (s) {
    return cc.p(s.width, s.height);
};

/**
 * Run a math operation function on each point component <br />
 * Math.abs, Math.fllor, Math.ceil, Math.round.
 * @param {cc.Point} p
 * @param {Function} opFunc
 * @return {cc.Point}
 * @example
 * //For example: let's try to take the floor of x,y
 * var p = cc.pCompOp(cc.p(10,10),Math.abs);
 */
cc.pCompOp = function (p, opFunc) {
    return cc.p(opFunc(p.x), opFunc(p.y));
};

/**
 * Linear Interpolation between two points a and b
 * alpha == 0 ? a
 * alpha == 1 ? b
 * otherwise a value between a..b
 * @param {cc.Point} a
 * @param {cc.Point} b
 * @param {Number} alpha
 * @return {cc.pAdd}
 */
cc.pLerp = function (a, b, alpha) {
    return cc.pAdd(cc.pMult(a, 1 - alpha), cc.pMult(b, alpha));
};

/**
 * @param {cc.Point} a
 * @param {cc.Point} b
 * @param {Number} variance
 * @return {Boolean} if points have fuzzy equality which means equal with some degree of variance.
 */
cc.pFuzzyEqual = function (a, b, variance) {
    if (a.x - variance <= b.x && b.x <= a.x + variance) {
        if (a.y - variance <= b.y && b.y <= a.y + variance)
            return true;
    }
    return false;
};

/**
 * Multiplies a nd b components, a.x*b.x, a.y*b.y
 * @param {cc.Point} a
 * @param {cc.Point} b
 * @return {cc.Point}
 */
cc.pCompMult = function (a, b) {
    return cc.p(a.x * b.x, a.y * b.y);
};

/**
 * @param {cc.Point} a
 * @param {cc.Point} b
 * @return {Number} the signed angle in radians between two vector directions
 */
cc.pAngleSigned = function (a, b) {
    var a2 = cc.pNormalize(a);
    var b2 = cc.pNormalize(b);
    var angle = Math.atan2(a2.x * b2.y - a2.y * b2.x, cc.pDot(a2, b2));
    if (Math.abs(angle) < cc.POINT_EPSILON)
        return 0.0;
    return angle;
};

/**
 * @param {cc.Point} a
 * @param {cc.Point} b
 * @return {Number} the angle in radians between two vector directions
 */
cc.pAngle = function (a, b) {
    var angle = Math.acos(cc.pDot(cc.pNormalize(a), cc.pNormalize(b)));
    if (Math.abs(angle) < cc.POINT_EPSILON) return 0.0;
    return angle;
};

/**
 * Rotates a point counter clockwise by the angle around a pivot
 * @param {cc.Point} v v is the point to rotate
 * @param {cc.Point} pivot pivot is the pivot, naturally
 * @param {Number} angle angle is the angle of rotation cw in radians
 * @return {cc.Point} the rotated point
 */
cc.pRotateByAngle = function (v, pivot, angle) {
    var r = cc.pSub(v, pivot);
    var cosa = Math.cos(angle), sina = Math.sin(angle);
    var t = r.x;
    r.x = t * cosa - r.y * sina + pivot.x;
    r.y = t * sina + r.y * cosa + pivot.y;
    return r;
};

/**
 * A general line-line intersection test
 * @param {cc.Point} A A is the startpoint for the first line P1 = (p1 - p2).
 * @param {cc.Point} B B is the endpoint for the first line P1 = (p1 - p2).
 * @param {cc.Point} C C is the startpoint for the second line P2 = (p3 - p4).
 * @param {cc.Point} D D is the endpoint for the second line P2 = (p3 - p4).
 * @param {cc.Point} retP retP.x is the range for a hitpoint in P1 (pa = p1 + s*(p2 - p1)), <br />
 * retP.y is the range for a hitpoint in P3 (pa = p2 + t*(p4 - p3)).
 * @return {Boolean}
 * indicating successful intersection of a line<br />
 * note that to truly test intersection for segments we have to make<br />
 * sure that s & t lie within [0..1] and for rays, make sure s & t > 0<br />
 * the hit point is        p3 + t * (p4 - p3);<br />
 * the hit point also is    p1 + s * (p2 - p1);
 */
cc.pLineIntersect = function (A, B, C, D, retP) {
    if ((A.x == B.x && A.y == B.y) || (C.x == D.x && C.y == D.y)) {
        return false;
    }
    var BAx = B.x - A.x;
    var BAy = B.y - A.y;
    var DCx = D.x - C.x;
    var DCy = D.y - C.y;
    var ACx = A.x - C.x;
    var ACy = A.y - C.y;

    var denom = DCy * BAx - DCx * BAy;

    retP.x = DCx * ACy - DCy * ACx;
    retP.y = BAx * ACy - BAy * ACx;

    if (denom == 0) {
        if (retP.x == 0 || retP.y == 0) {
            // Lines incident
            return true;
        }
        // Lines parallel and not incident
        return false;
    }

    retP.x = retP.x / denom;
    retP.y = retP.y / denom;

    return true;
};

/**
 * ccpSegmentIntersect return YES if Segment A-B intersects with segment C-D.
 * @param {cc.Point} A
 * @param {cc.Point} B
 * @param {cc.Point} C
 * @param {cc.Point} D
 * @return {Boolean}
 */
cc.pSegmentIntersect = function (A, B, C, D) {
    var retP = cc.p(0, 0);
    if (cc.pLineIntersect(A, B, C, D, retP))
        if (retP.x >= 0.0 && retP.x <= 1.0 && retP.y >= 0.0 && retP.y <= 1.0)
            return true;
    return false;
};


/**
 * ccpIntersectPoint return the intersection point of line A-B, C-D
 * @param {cc.Point} A
 * @param {cc.Point} B
 * @param {cc.Point} C
 * @param {cc.Point} D
 * @return {cc.Point}
 */
cc.pIntersectPoint = function (A, B, C, D) {
    var retP = cc.p(0, 0);

    if (cc.pLineIntersect(A, B, C, D, retP)) {
        // Point of intersection
        var P = cc.p(0, 0);
        P.x = A.x + retP.x * (B.x - A.x);
        P.y = A.y + retP.x * (B.y - A.y);
        return P;
    }

    return cc.p(0,0);
};

/**
 * check to see if both points are equal
 * @param {cc.Point} A A ccp a
 * @param {cc.Point} B B ccp b to be compared
 * @return {Boolean} the true if both ccp are same
 */
cc.pSameAs = function (A, B) {
    if ((A != null) && (B != null)) {
        return (A.x == B.x && A.y == B.y);
    }
    return false;
};

/**
  * sets the position of the point to 0
  */
cc.pZeroIn = function(v) {
    v.x = 0;
    v.y = 0;
};

/**
  * copies the position of one point to another
  */
cc.pIn = function(v1, v2) {
    v1.x = v2.x;
    v1.y = v2.y;
};

/**
  * multiplies the point with the given factor (inplace)
  */
cc.pMultIn = function(point, floatVar) {
    point.x *= floatVar;
    point.y *= floatVar;
};

/**
  * subtracts one point from another (inplace)
  */
cc.pSubIn = function(v1, v2) {
    v1.x -= v2.x;
    v1.y -= v2.y;
};

/**
  * adds one point to another (inplace)
  */
cc.pAddIn = function(v1, v2) {
    v1.x += v2.x;
    v1.y += v2.y;
};

/**
  * normalizes the point (inplace)
  */
cc.pNormalizeIn = function(v) {
    cc.pMultIn(v, 1.0 / Math.sqrt(v.x * v.x + v.y * v.y));
};

//
// Grid
//
cc._g = function( x, y )
{
    cc._reuse_grid.x = x;
    cc._reuse_grid.y = y;
    return cc._reuse_grid;
};

//
// Basic structure : Size
//
cc.size = function(w,h)
{
    return {width:w, height:h};
};
cc._size = function(w,h)
{
    cc._reuse_size.width = w;
    cc._reuse_size.height = h;
    return cc._reuse_size;
};
cc.sizeEqualToSize = function (size1, size2)
{
    return ((size1.width == size2.width) && (size1.height == size2.height));
};

cc.SizeZero = function () {
    return cc.size(0, 0);
};

/**
 * create a cc.rect object
 * @param {Number|cc.point|cc.rect} [x] a Number value as x or a cc.point object as origin or a cc.rect clone object
 * @param {Number|cc.size} [y] x1 a Number value as y or a cc.size object as size
 * @param {Number} [w]
 * @param {Number} [h]
 * @return {Object} a cc.rect object
 */
cc.rect = function(x,y,w,h)
{
    var argLen = arguments.length;
    if (argLen === 0)
        return { x: 0, y: 0, width: 0, height: 0 };

    if (argLen === 1)
        return { x: x.x, y: x.y, width: x.width, height: x.height };

    if (argLen === 2)
        return { x: x.x, y: x.y, width: y.width, height: y.height };

    if (argLen === 4)
        return { x: x, y: y, width: w, height: h };

    throw "unknown argument type";
};
cc._rect = function(x,y,w,h)
{
    cc._reuse_rect.x = x;
    cc._reuse_rect.y = y;
    cc._reuse_rect.width = w;
    cc._reuse_rect.height = h;
    return cc._reuse_rect;
};
cc.rectEqualToRect = function (rect1, rect2) {
    return ( rect1.x==rect2.x && rect1.y==rect2.y && rect1.width==rect2.width && rect1.height==rect2.height);
};

cc.rectContainsRect = function (rect1, rect2) {
    if ((rect1.x >= rect2.x) || (rect1.y >= rect2.y) ||
        ( rect1.x + rect1.width <= rect2.x + rect2.width) ||
        ( rect1.y + rect1.height <= rect2.y + rect2.height))
        return false;
    return true;
};

cc.rectGetMaxX = function (rect) {
    return (rect.x + rect.width);
};

cc.rectGetMidX = function (rect) {
    return (rect.x + rect.width / 2.0);
};

cc.rectGetMinX = function (rect) {
    return rect.x;
};

cc.rectGetMaxY = function (rect) {
    return(rect.y + rect.height);
};

cc.rectGetMidY = function (rect) {
    return rect.y + rect.height / 2.0;
};

cc.rectGetMinY = function (rect) {
    return rect.y;
};

cc.rectContainsPoint = function (rect, point) {
    var ret = false;
    if (point.x >= rect.x && point.x <= rect.x + rect.width &&
        point.y >= rect.y && point.y <= rect.y + rect.height) {
        ret = true;
    }
    return ret;
};

cc.rectIntersectsRect = function( rectA, rectB )
{
    var bool = ! (  rectA.x > rectB.x + rectB.width ||
                    rectA.x + rectA.width < rectB.x ||
                    rectA.y > rectB.y +rectB.height ||
                    rectA.y + rectA.height < rectB.y );

    return bool;
};

cc.rectOverlapsRect = function (rectA, rectB)
{
    return !((rectA.x + rectA.width < rectB.x) ||
             (rectB.x + rectB.width < rectA.x) ||
             (rectA.y + rectA.height < rectB.y) ||
             (rectB.y + rectB.height < rectA.y));
};

cc.rectUnion = function (rectA, rectB) {
    var rect = cc.rect(0, 0, 0, 0);
    rect.x = Math.min(rectA.x, rectB.x);
    rect.y = Math.min(rectA.y, rectB.y);
    rect.width = Math.max(rectA.x + rectA.width, rectB.x + rectB.width) - rect.x;
    rect.height = Math.max(rectA.y + rectA.height, rectB.y + rectB.height) - rect.y;
    return rect;
};

cc.rectIntersection = function (rectA, rectB) {
    var intersection = cc.rect(
        Math.max(rectA.x, rectB.x),
        Math.max(rectA.y, rectB.y),
        0, 0);

    intersection.width = Math.min(rectA.x+rectA.width, rectB.x+rectB.width) - intersection.x;
    intersection.height = Math.min(rectA.y+rectA.height, rectB.y+rectB.height) - intersection.y;
    return intersection;
};

cc.RectZero = function () {
    return cc.rect(0, 0, 0, 0);
};

// Basic structure : Color
cc.Color = function (r, g, b, a) {
    this.r = r || 0;
    this.g = g || 0;
    this.b = b || 0;
    this.a = (a === undefined) ? 255 : a;
};

/**
 * Generate a color object based on multiple forms of parameters
 * @example
 *
 * // 1. All channels separately as parameters
 * var color1 = cc.color(255, 255, 255, 255);
 *
 * // 2. Convert a hex string to a color
 * var color2 = cc.color("#000000");
 *
 * // 3. An color object as parameter
 * var color3 = cc.color({r: 255, g: 255, b: 255, a: 255});
 *
 * Alpha channel is optional. Default value is 255
 *
 * @param {Number|String|cc.Color} r
 * @param {Number} g
 * @param {Number} b
 * @param {Number} [a=255]
 * @returns {cc.Color}
 */
cc.color = function (r, g, b, a) {
    if (r === undefined)
        return {r: 0, g: 0, b: 0, a: 255};
    if (typeof r === "string")
        return cc.hexToColor(r);
    if (typeof r === "object")
        return {r: r.r, g: r.g, b: r.b, a: (r.a === undefined) ? 255 : r.a};
    return  {r: r, g: g, b: b, a: (a === undefined ? 255 : a)};
};

/**
 * returns true if both ccColor3B are equal. Otherwise it returns false.
 * @param {cc.Color} color1
 * @param {cc.Color} color2
 * @return {Boolean}  true if both ccColor3B are equal. Otherwise it returns false.
 */
cc.colorEqual = function(color1, color2){
    return color1.r === color2.r && color1.g === color2.g && color1.b === color2.b;
};

/**
 * convert a string of color for style to Color.
 * e.g. "#ff06ff"  to : cc.color(255,6,255)
 * @param {String} hex
 * @return {cc.Color}
 */
cc.hexToColor = function (hex) {
    hex = hex.replace(/^#?/, "0x");
    var c = parseInt(hex);
    var r = c >> 16;
    var g = (c >> 8) % 256;
    var b = c % 256;
    return cc.color(r, g, b);
};

/**
 * convert Color to a string of color for style.
 * e.g.  cc.color(255,6,255)  to : "#ff06ff"
 * @param {cc.Color} color
 * @return {String}
 */
cc.colorToHex = function (color) {
    var hR = color.r.toString(16);
    var hG = color.g.toString(16);
    var hB = color.b.toString(16);
    var hex = "#" + (color.r < 16 ? ("0" + hR) : hR) + (color.g < 16 ? ("0" + hG) : hG) + (color.b < 16 ? ("0" + hB) : hB);
    return hex;
};

/**
 * White color (255, 255, 255, 255)
 * @returns {cc.Color}
 * @private
 */
cc.color._getWhite = function(){
    return cc.color(255, 255, 255, 255);
};

/**
 *  Yellow color (255, 255, 0, 255)
 * @returns {cc.Color}
 * @private
 */
cc.color._getYellow = function () {
    return cc.color(255, 255, 0, 255);
};

/**
 *  Blue color (0, 0, 255, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getBlue = function () {
    return  cc.color(0, 0, 255, 255);
};

/**
 *  Green Color (0, 255, 0, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getGreen = function () {
    return cc.color(0, 255, 0, 255);
};

/**
 *  Red Color (255, 0, 0, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getRed = function () {
    return cc.color(255, 0, 0, 255);
};

/**
 *  Magenta Color (255, 0, 255, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getMagenta = function () {
    return cc.color(255, 0, 255, 255);
};

/**
 *  Black Color (0, 0, 0, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getBlack = function () {
    return cc.color(0, 0, 0, 255);
};

/**
 *  Orange Color (255, 127, 0, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getOrange = function () {
    return cc.color(255, 127, 0, 255);
};

/**
 *  Gray Color (166, 166, 166, 255)
 * @type {cc.Color}
 * @private
 */
cc.color._getGray = function () {
    return cc.color(166, 166, 166, 255);
};
var _proto = cc.color;
/** @expose */
_proto.WHITE;
cc.defineGetterSetter(_proto, "WHITE", _proto._getWhite);
/** @expose */
_proto.YELLOW;
cc.defineGetterSetter(_proto, "YELLOW", _proto._getYellow);
/** @expose */
_proto.BLUE;
cc.defineGetterSetter(_proto, "BLUE", _proto._getBlue);
/** @expose */
_proto.GREEN;
cc.defineGetterSetter(_proto, "GREEN", _proto._getGreen);
/** @expose */
_proto.RED;
cc.defineGetterSetter(_proto, "RED", _proto._getRed);
/** @expose */
_proto.MAGENTA;
cc.defineGetterSetter(_proto, "MAGENTA", _proto._getMagenta);
/** @expose */
_proto.BLACK;
cc.defineGetterSetter(_proto, "BLACK", _proto._getBlack);
/** @expose */
_proto.ORANGE;
cc.defineGetterSetter(_proto, "ORANGE", _proto._getOrange);
/** @expose */
_proto.GRAY;
cc.defineGetterSetter(_proto, "GRAY", _proto._getGray);


// jsb_cocos2d.js:1660-1776
// Predefined font definition
cc.FontDefinition = function () {
    this.fontName = "Arial";
    this.fontSize = 12;
    this.textAlign = cc.TEXT_ALIGNMENT_CENTER;
    this.verticalAlign = cc.VERTICAL_TEXT_ALIGNMENT_TOP;
    this.fillStyle = cc.color(255, 255, 255, 255);
    this.boundingWidth = 0;
    this.boundingHeight = 0;

    this.strokeEnabled = false;
    this.strokeStyle = cc.color(255, 255, 255, 255);
    this.lineWidth = 1;

    this.shadowEnabled = false;
    this.shadowOffsetX = 0;
    this.shadowOffsetY = 0;
    this.shadowBlur = 0;
    this.shadowOpacity = 1.0;
};


// Array utils

/**
 * Verify Array's Type
 * @param {Array} arr
 * @param {function} type
 * @return {Boolean}
 * @function
 */
cc.arrayVerifyType = function (arr, type) {
    if (arr && arr.length > 0) {
        for (var i = 0; i < arr.length; i++) {
            if (!(arr[i] instanceof  type)) {
                cc.log("element type is wrong!");
                return false;
            }
        }
    }
    return true;
};

/**
 * Searches for the first occurrence of object and removes it. If object is not found the function has no effect.
 * @function
 * @param {Array} arr Source Array
 * @param {*} delObj  remove object
 */
cc.arrayRemoveObject = function (arr, delObj) {
    for (var i = 0, l = arr.length; i < l; i++) {
        if (arr[i] == delObj) {
            arr.splice(i, 1);
            break;
        }
    }
};

/**
 * Removes from arr all values in minusArr. For each Value in minusArr, the first matching instance in arr will be removed.
 * @function
 * @param {Array} arr Source Array
 * @param {Array} minusArr minus Array
 */
cc.arrayRemoveArray = function (arr, minusArr) {
    for (var i = 0, l = minusArr.length; i < l; i++) {
        cc.arrayRemoveObject(arr, minusArr[i]);
    }
};

/**
 * Inserts some objects at index
 * @function
 * @param {Array} arr
 * @param {Array} addObjs
 * @param {Number} index
 * @return {Array}
 */
cc.arrayAppendObjectsToIndex = function(arr, addObjs,index){
    arr.splice.apply(arr, [index, 0].concat(addObjs));
    return arr;
};

/**
 * Copy an array's item to a new array (its performance is better than Array.slice)
 * @param {Array} arr
 * @returns {Array}
 */
cc.copyArray = function(arr){
    var i, len = arr.length, arr_clone = new Array(len);
    for (i = 0; i < len; i += 1)
        arr_clone[i] = arr[i];
    return arr_clone;
};



//
// DrawNode JS API Wrapper
//

cc.cardinalSplineAt = function (p0, p1, p2, p3, tension, t) {
    var t2 = t * t;
    var t3 = t2 * t;

    var s = (1 - tension) / 2;

    var b1 = s * ((-t3 + (2 * t2)) - t);                      // s(-t3 + 2 t2 - t)P1
    var b2 = s * (-t3 + t2) + (2 * t3 - 3 * t2 + 1);          // s(-t3 + t2)P2 + (2 t3 - 3 t2 + 1)P2
    var b3 = s * (t3 - 2 * t2 + t) + (-2 * t3 + 3 * t2);      // s(t3 - 2 t2 + t)P3 + (-2 t3 + 3 t2)P3
    var b4 = s * (t3 - t2);                                   // s(t3 - t2)P4

    var x = (p0.x * b1 + p1.x * b2 + p2.x * b3 + p3.x * b4);
    var y = (p0.y * b1 + p1.y * b2 + p2.y * b3 + p3.y * b4);
    return cc.p(x, y);
};


// jsb_cocos2d.js:2240-2497
//
//AffineTransform API
//

/**
 * @memberOf cc
 * @function
 * @param {Number} a
 * @param {Number} b
 * @param {Number} c
 * @param {Number} d
 * @param {Number} tx
 * @param {Number} ty
 */
cc.AffineTransform = function (a, b, c, d, tx, ty) {
    this.a = a;
    this.b = b;
    this.c = c;
    this.d = d;
    this.tx = tx;
    this.ty = ty;
};

/**
 * @memberOf cc
 * @function
 * @param {Number} a
 * @param {Number} b
 * @param {Number} c
 * @param {Number} d
 * @param {Number} tx
 * @param {Number} ty
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformMake = function (a, b, c, d, tx, ty) {
    return {a: a, b: b, c: c, d: d, tx: tx, ty: ty};
};

/**
 * @memberOf cc
 * @function
 * @param {cc.Point} point
 * @param {cc.AffineTransform} t
 * @return {cc.Point}
 * Constructor
 */
cc.pointApplyAffineTransform = function (point, t) {
    return {x: t.a * point.x + t.c * point.y + t.tx, y: t.b * point.x + t.d * point.y + t.ty};
};

cc._pointApplyAffineTransform = function (x, y, t) {
    return {x: t.a * x + t.c * y + t.tx,
        y: t.b * x + t.d * y + t.ty};
};

/**
 * @memberOf cc
 * @function
 * @param {cc.Size} size
 * @param {cc.AffineTransform} t
 * @return {cc.Size}
 * Constructor
 */
cc.sizeApplyAffineTransform = function (size, t) {
    return {width: t.a * size.width + t.c * size.height, height: t.b * size.width + t.d * size.height};
};

/**
 * @memberOf cc
 * @function
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformMakeIdentity = function () {
    return {a: 1.0, b: 0.0, c: 0.0, d: 1.0, tx: 0.0, ty: 0.0};
};

/**
 * @memberOf cc
 * @function
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformIdentity = function () {
    return {a: 1.0, b: 0.0, c: 0.0, d: 1.0, tx: 0.0, ty: 0.0};
};

/**
 * @memberOf cc
 * @function
 * @param {cc.Rect} rect
 * @param {cc.AffineTransform} anAffineTransform
 * @return {cc.Rect}
 * Constructor
 */
cc.rectApplyAffineTransform = function (rect, anAffineTransform) {
    var top = cc.rectGetMinY(rect);
    var left = cc.rectGetMinX(rect);
    var right = cc.rectGetMaxX(rect);
    var bottom = cc.rectGetMaxY(rect);

    var topLeft = cc._pointApplyAffineTransform(left, top, anAffineTransform);
    var topRight = cc._pointApplyAffineTransform(right, top, anAffineTransform);
    var bottomLeft = cc._pointApplyAffineTransform(left, bottom, anAffineTransform);
    var bottomRight = cc._pointApplyAffineTransform(right, bottom, anAffineTransform);

    var minX = Math.min(topLeft.x, topRight.x, bottomLeft.x, bottomRight.x);
    var maxX = Math.max(topLeft.x, topRight.x, bottomLeft.x, bottomRight.x);
    var minY = Math.min(topLeft.y, topRight.y, bottomLeft.y, bottomRight.y);
    var maxY = Math.max(topLeft.y, topRight.y, bottomLeft.y, bottomRight.y);

    return cc.rect(minX, minY, (maxX - minX), (maxY - minY));
};

cc._rectApplyAffineTransformIn = function(rect, anAffineTransform){
    var top = cc.rectGetMinY(rect);
    var left = cc.rectGetMinX(rect);
    var right = cc.rectGetMaxX(rect);
    var bottom = cc.rectGetMaxY(rect);

    var topLeft = cc._pointApplyAffineTransform(left, top, anAffineTransform);
    var topRight = cc._pointApplyAffineTransform(right, top, anAffineTransform);
    var bottomLeft = cc._pointApplyAffineTransform(left, bottom, anAffineTransform);
    var bottomRight = cc._pointApplyAffineTransform(right, bottom, anAffineTransform);

    var minX = Math.min(topLeft.x, topRight.x, bottomLeft.x, bottomRight.x);
    var maxX = Math.max(topLeft.x, topRight.x, bottomLeft.x, bottomRight.x);
    var minY = Math.min(topLeft.y, topRight.y, bottomLeft.y, bottomRight.y);
    var maxY = Math.max(topLeft.y, topRight.y, bottomLeft.y, bottomRight.y);

    rect.x = minX;
    rect.y = minY;
    rect.width = maxX - minX;
    rect.height = maxY - minY;
    return rect;
};

/**
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} t
 * @param {Number} tx
 * @param {Number}ty
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformTranslate = function (t, tx, ty) {
    return {
        a: t.a,
        b: t.b,
        c: t.c,
        d: t.d,
        tx: t.tx + t.a * tx + t.c * ty,
        ty: t.ty + t.b * tx + t.d * ty
    };
};

/**
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} t
 * @param {Number} sx
 * @param {Number} sy
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformScale = function (t, sx, sy) {
    return {a: t.a * sx, b: t.b * sx, c: t.c * sy, d: t.d * sy, tx: t.tx, ty: t.ty};
};

/**
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} aTransform
 * @param {Number} anAngle
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformRotate = function (aTransform, anAngle) {
    var fSin = Math.sin(anAngle);
    var fCos = Math.cos(anAngle);

    return {a: aTransform.a * fCos + aTransform.c * fSin,
        b: aTransform.b * fCos + aTransform.d * fSin,
        c: aTransform.c * fCos - aTransform.a * fSin,
        d: aTransform.d * fCos - aTransform.b * fSin,
        tx: aTransform.tx,
        ty: aTransform.ty};
};

/**
 * Concatenate `t2' to `t1' and return the result:<br/>
 * t' = t1 * t2
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} t1
 * @param {cc.AffineTransform} t2
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformConcat = function (t1, t2) {
    return {a: t1.a * t2.a + t1.b * t2.c,                          //a
        b: t1.a * t2.b + t1.b * t2.d,                               //b
        c: t1.c * t2.a + t1.d * t2.c,                               //c
        d: t1.c * t2.b + t1.d * t2.d,                               //d
        tx: t1.tx * t2.a + t1.ty * t2.c + t2.tx,                    //tx
        ty: t1.tx * t2.b + t1.ty * t2.d + t2.ty};                   //ty
};

/**
 * Concatenate a transform matrix to another<br/>
 * The results are reflected in the first matrix.<br/>
 * t' = t1 * t2
 * @function
 * @param {cc.AffineTransform} t1 The first transform object
 * @param {cc.AffineTransform} t2 The transform object to concatenate
 * @return {cc.AffineTransform} The result of concatenation
 */
cc.affineTransformConcatIn = function (t1, t2) {
    var a = t1.a, b = t1.b, c = t1.c, d = t1.d, tx = t1.tx, ty = t1.ty;
    t1.a = a * t2.a + b * t2.c;
    t1.b = a * t2.b + b * t2.d;
    t1.c = c * t2.a + d * t2.c;
    t1.d = c * t2.b + d * t2.d;
    t1.tx = tx * t2.a + ty * t2.c + t2.tx;
    t1.ty = tx * t2.b + ty * t2.d + t2.ty;
    return t1;
};

/**
 * Return true if `t1' and `t2' are equal, false otherwise.
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} t1
 * @param {cc.AffineTransform} t2
 * @return {Boolean}
 * Constructor
 */
cc.affineTransformEqualToTransform = function (t1, t2) {
    return ((t1.a === t2.a) && (t1.b === t2.b) && (t1.c === t2.c) && (t1.d === t2.d) && (t1.tx === t2.tx) && (t1.ty === t2.ty));
};

/**
 * Get the invert value of an AffineTransform object
 * @memberOf cc
 * @function
 * @param {cc.AffineTransform} t
 * @return {cc.AffineTransform}
 * Constructor
 */
cc.affineTransformInvert = function (t) {
    var determinant = 1 / (t.a * t.d - t.b * t.c);
    return {a: determinant * t.d, b: -determinant * t.b, c: -determinant * t.c, d: determinant * t.a,
        tx: determinant * (t.c * t.ty - t.d * t.tx), ty: determinant * (t.b * t.tx - t.a * t.ty)};
};



// 3d/jsb_cocos2d_3d.js:136-254
cc.math = cc.math || {};

cc.math.Vec3 = function(x=0, y=0, z=0){
    this.x = x;
    this.y = y;
    this.z = z;
};

cc.math.Vec3.prototype.normalize = function(){
    var n = this.x * this.x + this.y * this.y + this.z * this.z;
    n = 1 / Math.sqrt(n);
    this.x *= n;
    this.y *= n;
    this.z *= n;
};

cc.math.vec3 = function(x, y, z){
    return new cc.math.Vec3(x, y, z);
};

cc.math.vec3Cross = function(v1, v2){
    return new cc.math.Vec3(v1.y * v2.z - v1.z * v2.y,
                       v1.z * v2.x - v1.x * v2.z,
                       v1.x * v2.y - v1.y * v2.x);
};

cc.math.vec3Dot = function(v1, v2){
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
};

cc.math.vec3Length = function(v){
    return Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
};

cc.math.vec3Normalize = function(v){
    var n = v.x * v.x + v.y * v.y + v.z * v.z;
    n = 1 / Math.sqrt(n);
    return cc.math.vec3(v.x * n, v.y * n, v.z * n);
};

cc.math.vec3Add = function(v1, v2){
    return new cc.math.Vec3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z);
};

cc.math.vec3Sub = function(v1, v2){
    return new cc.math.Vec3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z);
};

cc.math.Quaternion = function(x=0, y=0, z=0, w=0){
    this.x = x;
    this.y = y;
    this.z = z;
    this.w = w;
};

cc.math.quaternion = function(xOrAxis, yOrAngle, z, w){
    if(w !== undefined){
        return new cc.math.Quaternion(xOrAxis, yOrAngle, z, w);
    }
    else if(yOrAngle !== undefined){
        var sinHalfAngle = Math.sin(yOrAngle / 2);
        var normal = cc.math.vec3(xOrAxis.x, xOrAxis.y, xOrAxis.z);
        normal.normalize();
        return cc.math.quaternion(normal.x * sinHalfAngle, normal.y * sinHalfAngle, normal.z * sinHalfAngle, Math.cos(yOrAngle / 2));
    }
};

cc.math.AABB = function(min=cc.math.vec3(99999, 99999, 99999), max=cc.math.vec3(-99999, -99999, -99999)){
    this.min = min;
    this.max = max;
};

cc.math.aabb = function(min, max){
    return new cc.math.AABB(min, max);
};

cc.math.aabbGetCorners = function(aabb){
    var corners = new Array(8);
    corners[0] = cc.math.vec3(aabb.min.x, aabb.max.y, aabb.max.z);
    corners[1] = cc.math.vec3(aabb.min.x, aabb.min.y, aabb.max.z);
    corners[2] = cc.math.vec3(aabb.max.x, aabb.min.y, aabb.max.z);
    corners[3] = cc.math.vec3(aabb.max.x, aabb.max.y, aabb.max.z);

    corners[4] = cc.math.vec3(aabb.max.x, aabb.max.y, aabb.min.z);
    corners[5] = cc.math.vec3(aabb.max.x, aabb.min.y, aabb.min.z);
    corners[6] = cc.math.vec3(aabb.min.x, aabb.min.y, aabb.min.z);
    corners[7] = cc.math.vec3(aabb.min.x, aabb.max.y, aabb.min.z);
    return corners;
};

cc.math.OBB = function(aabb){
    this.center = cc.math.vec3((aabb.min.x + aabb.max.x)/2, (aabb.min.y + aabb.max.y)/2, (aabb.min.z + aabb.max.z)/2);   // obb center
    this.xAxis = cc.math.vec3(1, 0, 0);    // x axis of obb, unit vector
    this.yAxis = cc.math.vec3(0, 1, 0);    // y axis of obb, unit vecotr
    this.zAxis = cc.math.vec3(0, 0, 1);    // z axis of obb, unit vector
    this.extents = cc.math.vec3((aabb.max.x - aabb.min.x)/2, (aabb.max.y - aabb.min.y)/2, (aabb.max.z - aabb.min.z)/2);  // obb length along each axis
    this.extentX = cc.math.vec3((aabb.max.x - aabb.min.x)/2, 0, 0);  // _xAxis * _extents.x
    this.extentY = cc.math.vec3(0, (aabb.max.y - aabb.min.y)/2, 0);  // _yAxis * _extents.y
    this.extentZ = cc.math.vec3(0, 0, (aabb.max.z - aabb.min.z)/2);  // _zAxis * _extents.z
};

cc.math.obb = function(aabb){
    return new cc.math.OBB(aabb);
};

cc.math.Ray = function(origin = cc.math.vec3(0, 0, 0), direction = cc.math.vec3(0, 0, 1)){
    this.origin = origin;
    this.direction = direction;
};

cc.math.ray = function(origin, direction){
    return new cc.math.Ray(origin, direction);
};

cc.math.Vec4 = cc.math.Quaternion;

cc.math.vec4 = function(x, y, z, w){
    return new cc.math.Vec4(x, y, z, w);
};

// jsb_boot.js:836-863 — preserve target, remote loading and cache-hit returns.
cc.TextureCache.prototype._addImageAsync = cc.TextureCache.prototype.addImageAsync;
cc.TextureCache.prototype.addImageAsync = function(url, cb, target) {
    var localTex = null;
    cc.loader.loadImg(url, function(err, tex) {
        if (err) tex = null;
        if (cb) {
            cb.call(target, tex);
        }
        localTex = tex;
    });
    return localTex;
};
// Fix for compatibility with old APIs
cc.TextureCache.prototype._addImage = cc.TextureCache.prototype.addImage;
cc.TextureCache.prototype.addImage = function(url, cb, target) {
    if (typeof cb === "function") {
        return this.addImageAsync(url, cb, target);
    }
    else {
        if (cb) {
            return this._addImage(url, cb);
        }
        else {
            return this._addImage(url);
        }
    }
};


})();
