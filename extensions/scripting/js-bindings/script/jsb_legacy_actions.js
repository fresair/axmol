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


/* Original v3 action descriptor and fluent speed helpers. */
(function () {
var easeActions = {
    easeIn : 0,
    easeOut : 1,
    easeInOut : 2,
    easeExponentialIn : 3,
    easeExponentialOut : 4,
    easeExponentialInOut : 5,
    easeSineIn : 6,
    easeSineOut : 7,
    easeSineInOut : 8,
    easeElasticIn : 9,
    easeElasticOut : 10,
    easeElasticInOut : 11,
    easeBounceIn : 12,
    easeBounceOut : 13,
    easeBounceInOut : 14,
    easeBackIn : 15,
    easeBackOut : 16,
    easeBackInOut : 17,

    easeBezierAction : 18,
    easeQuadraticActionIn : 19,
    easeQuadraticActionOut : 20,
    easeQuadraticActionInOut : 21,
    easeQuarticActionIn : 22,
    easeQuarticActionOut : 23,
    easeQuarticActionInOut : 24,
    easeQuinticActionIn : 25,
    easeQuinticActionOut : 26,
    easeQuinticActionInOut : 27,
    easeCircleActionIn : 28,
    easeCircleActionOut : 29,
    easeCircleActionInOut : 30,
    easeCubicActionIn : 31,
    easeCubicActionOut : 32,
    easeCubicActionInOut : 33
};

function templateEaseActions(actionTag) {
    return function(param, param2, param3, param4) {
        return {tag: actionTag, param: param, param2: param2, param3: param3, param4: param4};
    }
}

for (var a in easeActions) {
    var actionTag = easeActions[a];
    cc[a] = templateEaseActions(actionTag);
}


cc.Speed.prototype._getSpeed=cc.Speed.prototype.getSpeed;
cc.Speed.prototype._setSpeed=cc.Speed.prototype.setSpeed;
cc.Speed.prototype.speed = cc.ActionInterval.prototype.speed = function(speed) {
//    if (speed < 0) {
//        cc.warn("cc.ActionInterval#speed : Speed must not be negative");
//        return;
//    }
    var action = this, found = false;
    while (action.getInnerAction && !found) {
        if (action instanceof cc.Speed) {
            found = true;
        }
        else {
            action = action.getInnerAction();
        }
    }
    if (found) {
        speed = speed * action._getSpeed();
        action._setSpeed(speed);
    }
    else {
        this._speed(speed);
    }
    return this;
};
cc.Speed.prototype.setSpeed = cc.ActionInterval.prototype.setSpeed = function(speed) {
//    if (speed < 0) {
//        cc.warn("cc.ActionInterval#setSpeed : Speed must not be negative");
//        return;
//    }
    var action = this, found = false;
    while (action.getInnerAction && !found) {
        if (action instanceof cc.Speed) {
            found = true;
        }
        else {
            action = action.getInnerAction();
        }
    }
    if (found) {
        action._setSpeed(speed);
    }
    else {
        this._speed(speed);
    }
};
cc.Speed.prototype.getSpeed = cc.ActionInterval.prototype.getSpeed = function() {
    var action = this, found = false;
    while (action.getInnerAction && !found) {
        if (action instanceof cc.Speed) {
            found = true;
        }
        else {
            action = action.getInnerAction();
        }
    }
    if (found) {
        return action._getSpeed();
    }
    else {
        return 1;
    }
};


})();
