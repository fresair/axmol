/*
 * Copyright (c) 2014-2016 Chukong Technologies Inc.
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

(function () {
    function install(Type, properties) {
        if (!Type || !Type.prototype) return;
        var prototype = Type.prototype;
        Object.keys(properties).forEach(function (name) {
            var methods = properties[name], getter = prototype[methods[0]], setter = prototype[methods[1]];
            if (typeof getter === "function")
                cc.defineGetterSetter(prototype, name, getter, typeof setter === "function" ? setter : undefined);
        });
    }
    install(ccs.Bone, {
        boneData: ["getBoneData", "setBoneData"], armature: ["getArmature", "setArmature"],
        parentBone: ["getParentBone", "setParentBone"], childArmature: ["getChildArmature", "setChildArmature"],
        childrenBone: ["getChildrenBone"], tween: ["getTween"], tweenData: ["getTweenData"],
        transformDirty: ["isTransformDirty", "setTransformDirty"],
        displayManager: ["getDisplayManager", "setDisplayManager"],
        ignoreMovementBoneData: ["isIgnoreMovementBoneData", "setIgnoreMovementBoneData"],
        blendDirty: ["isBlendDirty", "setBlendDirty"]
    });
    install(ccs.Armature, {
        parentBone: ["getParentBone", "setParentBone"],
        animation: ["getAnimation", "setAnimationController"],
        armatureData: ["getArmatureData", "setArmatureData"],
        batchNode: ["getBatchNode", "setBatchNode"], version: ["getVersion", "setVersion"]
    });
    install(ccs.Skin, {bone: ["getBone", "setBone"], displayName: ["getDisplayName"]});
    install(ccs.ActionTimeline, {
        duration: ["getDuration", "setDuration"], timeSpeed: ["getTimeSpeed", "setTimeSpeed"],
        currentFrame: ["getCurrentFrame", "setCurrentFrame"], startFrame: ["getStartFrame"],
        endFrame: ["getEndFrame"], timelines: ["getTimelines"]
    });
    install(ccs.Timeline, {actionTag: ["getActionTag", "setActionTag"], frames: ["getFrames"]});
    install(ccs.ActionTimelineData, {actionTag: ["getActionTag", "setActionTag"]});
    install(ccs.Frame, {
        frameIndex: ["getFrameIndex", "setFrameIndex"], tween: ["isTween", "setTween"],
        tweenType: ["getTweenType", "setTweenType"], easingParams: ["getEasingParams", "setEasingParams"]
    });
})();
