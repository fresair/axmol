/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Supplementary GPU diagnostic for the unchanged original NinePatch case.
// Call only after a real RenderView/RHI device has initialized. It deliberately
// uses the original files/frame names and reports native validity at each step.
function axmolScale9FrameProbe() {
    cc.spriteFrameCache.addSpriteFrames('ccs-res/cocosui/android9patch.plist');
    var frame = cc.spriteFrameCache.getSpriteFrame('animationbuttonpressed.png');
    var result = {frame: !!frame, frameValid: !!frame && jsb.isObjectValid(frame),
        hasOwnAllocator: Object.prototype.hasOwnProperty.call(ccui.Scale9Sprite, '__allocate'),
        allocator: typeof ccui.Scale9Sprite.__allocate,
        sameClassAlias: cc.Scale9Sprite === ccui.Scale9Sprite,
        ownCtorMarker: Object.prototype.hasOwnProperty.call(ccui.Scale9Sprite.prototype, '__legacyCtor'),
        ctorMarker: ccui.Scale9Sprite.prototype.__legacyCtor,
        spriteCtorMarkerWritable: (Object.getOwnPropertyDescriptor(cc.Sprite.prototype, '__legacyCtor') || {}).writable,
        cases: []};
    var paths = ['ccs-res/cocosui/player.9.png', 'animationbuttonpressed.png', 'monster.9.png',
        cc.spriteFrameCache.getSpriteFrame('buttonnormal.9.png')];
    paths.forEach(function (source, index) {
        var record = {index: index, source: typeof source === 'string' ? source : 'native SpriteFrame'};
        try {
            var explicit = ccui.Scale9Sprite.__allocate();
            record.allocatedValid = jsb.isObjectValid(explicit);
            record.allocatedType = explicit instanceof ccui.Scale9Sprite;
            record.ctorResult = explicit.ctor(source);
            record.initializedValid = jsb.isObjectValid(explicit);
            explicit.setPosition(10 + index * 100, 20);
            record.explicitPosition = explicit.getPosition();
            var automatic = new cc.Scale9Sprite(source);
            record.automaticValid = jsb.isObjectValid(automatic);
            record.automaticType = automatic instanceof ccui.Scale9Sprite;
            automatic.setPosition(10 + index * 100, 40);
            record.automaticPosition = automatic.getPosition();
            record.capInsets = automatic.getCapInsets();
            record.status = 'passed';
        } catch (error) {
            record.status = 'failed';
            record.error = String(error);
            record.stack = (error.stack || '').slice(0, 350);
        }
        result.cases.push(record);
        // Axmol log lines have a bounded buffer; keep each original input's
        // native and automatic constructor evidence in its own complete line.
        cc.log('SCALE9_FRAME_CASE ' + JSON.stringify(record));
    });
    var summary = {};
    Object.keys(result).forEach(function (key) { if (key !== 'cases') summary[key] = result[key]; });
    summary.caseCount = result.cases.length;
    summary.passedCount = result.cases.filter(function (item) { return item.status === 'passed'; }).length;
    cc.log('SCALE9_FRAME_PROBE ' + JSON.stringify(summary));
    return result;
}

// The original S9SpriteTest calls this deprecated API before moving/resizing
// the result. Validate the actual native texture, rect and insets on a GPU.
function axmolScale9BatchNodeProbe() {
    var batch = new cc.SpriteBatchNode('Images/blocks9.png');
    var texture = batch.getTexture();
    var result = {cases: []};
    [
        {name: 'full-insets', insets: cc.rect(0, 0, 96, 96), size: cc.size(96, 96)},
        {name: 'full-insets-scaled', insets: cc.rect(0, 0, 96, 96), size: cc.size(384, 192)},
        {name: 'center-insets-scaled', insets: cc.rect(32, 32, 32, 32), size: cc.size(384, 192)}
    ].forEach(function (value) {
        var record = {name: value.name};
        try {
            var sprite = new cc.Scale9Sprite();
            record.initialized = sprite.updateWithBatchNode(batch, cc.rect(0, 0, 96, 96), false, value.insets);
            record.textureIdentity = sprite.getTexture() === texture;
            record.textureRect = sprite.getTextureRect();
            record.capInsets = sprite.getCapInsets();
            sprite.setContentSize(value.size);
            record.contentSize = sprite.getContentSize();
            if (!record.initialized || !record.textureIdentity ||
                !cc.rectEqualToRect(record.textureRect, cc.rect(0, 0, 96, 96)) ||
                !cc.rectEqualToRect(record.capInsets, value.insets) ||
                !cc.sizeEqualToSize(record.contentSize, value.size))
                throw Error('Actual Scale9 texture/rect/capInsets/contentSize differs');
            record.status = 'passed';
        } catch (error) {
            record.status = 'failed';
            record.error = String(error);
            record.stack = (error.stack || '').slice(0, 300);
        }
        result.cases.push(record);
        cc.log('SCALE9_BATCH_CASE ' + JSON.stringify(record));
    });
    result.passedCount = result.cases.filter(function (value) { return value.status === 'passed'; }).length;
    cc.log('SCALE9_BATCH_PROBE ' + JSON.stringify({caseCount: result.cases.length, passedCount: result.passedCount}));
    return result;
}
