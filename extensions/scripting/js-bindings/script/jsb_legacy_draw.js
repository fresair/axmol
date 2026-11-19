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


/* v3 DrawNode argument order and interpolation, backed by current native geometry. */
(function () {
    if(!cc.DrawNode)return;
    var prototype=cc.DrawNode.prototype,native={};
    ['drawSegment','drawDot','drawQuadBezier','drawCubicBezier'].forEach(function (name) { native[name]=prototype[name]; });
    native.drawDot=prototype._legacyDrawDot;
    native.drawQuadBezier=prototype._legacyDrawQuadBezier;
    native.drawCubicBezier=prototype._legacyDrawCubicBezier;
    native.drawSegment=prototype._legacyDrawSegment;
    native.drawPoly=prototype._legacyDrawPolygon;
var adapters = {
    _drawColor: null,
    _lineWidth: 1,


    setLineWidth: function (width) {
        this._lineWidth = width;
    },

    getLineWidth: function () {
        return this._lineWidth;
    },

    setDrawColor: function(color) {
        var locDrawColor = this._drawColor;
        locDrawColor.r = color.r;
        locDrawColor.g = color.g;
        locDrawColor.b = color.b;
        locDrawColor.a = (color.a == null) ? 255 : color.a;
    },

    getDrawColor: function () {
        return  cc.color(this._drawColor.r, this._drawColor.g, this._drawColor.b, this._drawColor.a);
    },

    drawRect: function (origin, destination, fillColor, lineWidth, lineColor) {
        lineWidth = lineWidth || this._lineWidth;
        lineColor = lineColor || this._drawColor;
        var points = [origin, cc.p(origin.x, destination.y), destination, cc.p(destination.x, origin.y)];
        if (fillColor)
            native.drawPoly.call(this, points, fillColor, lineWidth, lineColor);
        else {
            points.push(origin);
            var drawSeg = native.drawSegment;
            for (var i = 0, len = points.length; i < len - 1; i++)
                drawSeg.call(this, points[i], points[i + 1], lineWidth, lineColor);
        }
    },

    drawCircle: function (center, radius, angle, segments, drawLineToCenter, lineWidth, color) {
        lineWidth = lineWidth || this._lineWidth;
        color = color || this._drawColor;
        if (color.a == null)
            color.a = 255;
        var coef = 2.0 * Math.PI / segments, vertices = [], i, len;
        for (i = 0; i <= segments; i++) {
            var rads = i * coef;
            var j = radius * Math.cos(rads + angle) + center.x;
            var k = radius * Math.sin(rads + angle) + center.y;
            vertices.push(cc.p(j, k));
        }
        if (drawLineToCenter)
            vertices.push(cc.p(center.x, center.y));

        lineWidth *= 0.5;
        var drawSeg = native.drawSegment;
        for (i = 0, len = vertices.length; i < len - 1; i++)
            drawSeg.call(this, vertices[i], vertices[i + 1], lineWidth, color);
    },

    drawQuadBezier: function (origin, control, destination, segments, lineWidth, color) {
        lineWidth = lineWidth || this._lineWidth;
        color = color || this._drawColor;
        native.drawQuadBezier.call(this, origin, control, destination, segments, color);
    },

    drawCubicBezier: function (origin, control1, control2, destination, segments, lineWidth, color) {
        lineWidth = lineWidth || this._lineWidth;
        color = color || this._drawColor;
        native.drawCubicBezier.call(this, origin, control1, control2, destination, segments, color);
    },

    drawCatmullRom: function (points, segments, lineWidth, color) {
        this.drawCardinalSpline(points, 0.5, segments, lineWidth, color);
    },

    drawCardinalSpline: function (config, tension, segments, lineWidth, color) {
        lineWidth = lineWidth || this._lineWidth;
        color = color || this._drawColor;
        if (color.a == null)
            color.a = 255;
        var vertices = [], p, lt, deltaT = 1.0 / config.length, m1len = config.length - 1;

        for (var i = 0; i < segments + 1; i++) {
            var dt = i / segments;

            // border
            if (dt == 1) {
                p = m1len;
                lt = 1;
            } else {
                p = 0 | (dt / deltaT);
                lt = (dt - deltaT * p) / deltaT;
            }

            // Interpolate
            var newPos = cc.cardinalSplineAt(
                config[Math.min(m1len, Math.max(p - 1, 0))],
                config[Math.min(m1len, Math.max(p + 0, 0))],
                config[Math.min(m1len, Math.max(p + 1, 0))],
                config[Math.min(m1len, Math.max(p + 2, 0))],
                tension, lt);
            vertices.push(newPos);
        }

        lineWidth *= 0.5;
        var drawSeg = native.drawSegment;
        for (var j = 0, len = vertices.length; j < len - 1; j++)
            drawSeg.call(this, vertices[j], vertices[j + 1], lineWidth, color);
    },

    drawDot:function (pos, radius, color) {
        color = color || this._drawColor;
        native.drawDot.call(this, pos, radius, color);
    },

    drawSegment:function (from, to, lineWidth, color) {
        lineWidth = lineWidth || this._lineWidth;
        color = color || this._drawColor;
        native.drawSegment.call(this, from, to, lineWidth, color);
    },

    drawPoly:function (verts, fillColor, borderWidth, borderColor) {
        borderColor = borderColor || this._drawColor;
        if (fillColor)
            native.drawPoly.call(this, verts, fillColor, borderWidth, borderColor);
        else {
            verts.push(verts[0]);
            var drawSeg = native.drawSegment;
            for (var i = 0, len = verts.length; i < len - 1; i++)
                drawSeg.call(this, verts[i], verts[i + 1], borderWidth, borderColor);
        }
    }
};

    Object.keys(adapters).forEach(function (name) {
        if(typeof adapters[name]!=="function")return;
        prototype[name]=function () {
            if(!this._drawColor)this._drawColor=cc.color(255,255,255,255);
            if(this._lineWidth===undefined)this._lineWidth=1;
            return adapters[name].apply(this,arguments);
        };
    });
})();
