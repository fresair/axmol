/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Imported parsers retain the Cocos2d-JS license in script/studio/. */
(function () {
    ccs.Node = cc.Node;
    ccs.Sprite = cc.Sprite;
    ccs.Component = cc.Component;
    ccs.Class = cc.Class;
    ccs.ArmatureAnimation = ccs.ArmatureController;
    ccs.Tween = ccs.BoneTweenController;
    // Legacy ProcessBase and the renamed native base share the same scale
    // state and dt multiplier; keep these aliases on the real base prototype.
    if (ccs.AnimationController) {
        ccs.ProcessBase = ccs.AnimationController;
        var process = ccs.AnimationController.prototype;
        if (typeof process.getAnimationScale === 'function')
            process.getProcessScale = process.getAnimationScale;
        if (typeof process.setAnimationScale === 'function')
            process.setProcessScale = process.setAnimationScale;
    }
    ccs.ActionManager = ccs.ActionManagerEx;
    ccs.actionManager = ccs.ActionManager.getInstance();
    ccs.csLoader = ccs.CSLoader.getInstance();
    if (!cc.BlendFunc) cc.BlendFunc = function (source, destination) { this.src = source; this.dst = destination; };
    if (ccs.Armature) ccs.Armature.prototype.getAnimation = ccs.Armature.prototype.getAnimationController;
    if (ccs.Bone && ccs.Bone.prototype.getTweenController)
        ccs.Bone.prototype.getTween = ccs.Bone.prototype.getTweenController;
    if (ccs.ArmatureDataManager)
        cc.defineGetterSetter(ccs, 'armatureDataManager', function () { return ccs.ArmatureDataManager.getInstance(); });
    if (ccs.ActionManagerEx) ccs.ActionManagerEx.prototype.clear = function () { this.releaseActions(); };

    // Current enum spelling and auto-size mode have the same native behavior.
    if (!ccui.Layout) ccui.Layout = ccui.LayoutGroup;
    if (ccui.LayoutComponent) {
        var horizontal = ccui.LayoutComponent.HorizontalEdge;
        var vertical = ccui.LayoutComponent.VerticalEdge;
        ccui.LayoutComponent.horizontalEdge = {NONE: horizontal.None, LEFT: horizontal.Left,
            RIGHT: horizontal.Right, CENTER: horizontal.Center};
        ccui.LayoutComponent.verticalEdge = {NONE: vertical.None, BOTTOM: vertical.Bottom,
            TOP: vertical.Top, CENTER: vertical.Center};
    }
    var widget = ccui.Widget.prototype;
    widget.ignoreContentAdaptWithSize = widget.setAutoSize;
    widget.isIgnoreContentAdaptWithSize = widget.isAutoSize;
    ccui.Widget.POSITION_ABSOLUTE = ccui.Widget.PositionType.ABSOLUTE;
    ccui.Widget.POSITION_PERCENT = ccui.Widget.PositionType.PERCENT;
    ccui.Widget.SIZE_ABSOLUTE = ccui.Widget.SizeType.ABSOLUTE;
    ccui.Widget.SIZE_PERCENT = ccui.Widget.SizeType.PERCENT;

    require('script/studio/jsb_cocos2d_studio.js');
    require('script/studio/jsb_studio_property_apis.js');
    ['setMovementEventCallFunc', 'setFrameEventCallFunc'].forEach(function (name) {
        var native = ccs.ArmatureController && ccs.ArmatureController.prototype[name];
        if (native) ccs.ArmatureController.prototype[name] = function (callback, target) {
            return native.call(this, typeof callback === 'function' ? function () {
                return callback.apply(target, arguments);
            } : callback);
        };
    });
    require('script/studio/jsb_studio_load.js');
    // Timeline 1.x captures the UI parser at module initialization.
    ['action-1.x', 'action-2.x', 'uiParser-1.x', 'scene-1.x',
        'timelineParser-1.x', 'timelineParser-2.x', 'compatible'].forEach(function (name) {
        require('script/studio/parsers/' + name + '.js');
    });

    var parseLoad = ccs.load;
    var loading = {};
    ccs.load = function (file, path, controller) {
        if (typeof file !== 'string' || !file) throw new TypeError('ccs.load requires a resource filename');
        if (path && typeof path === 'object' && controller === undefined) { controller = path; path = undefined; }
        if (/\.csb$/i.test(file)) return ccs._loadCsb(file, controller);
        var key = jsb.fileUtils.fullPathForFilename(file) || file;
        if (loading[key]) throw new Error('Cyclic CocoStudio project reference: ' + file);
        loading[key] = true;
        try {
            var cache = cc.loader.cache || cc.loader._cache;
            if (!cache || !cache[file]) {
                if (!jsb.fileUtils.isFileExist(file)) throw new Error('CocoStudio resource does not exist: ' + file);
                var json = JSON.parse(jsb.fileUtils.getStringFromFile(file));
                if (json.Content && json.Content.ObjectData) json.Content = {Content: json.Content};
                if (cc.loader.cache) cc.loader.cache[file] = json;
                if (cc.loader._cache) cc.loader._cache[file] = json;
            }
            var result = parseLoad(file, path);
            if (!result || !result.node) throw new Error('Failed to load CocoStudio resource: ' + file);
            if (controller) ccs.bindController(result.node, controller);
            return result;
        } finally {
            delete loading[key];
        }
    };
})();
