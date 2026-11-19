// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Requires a running native scene and a unique local image fixture that may
// safely be evicted from TextureCache. No loader, callback or texture is mocked.
(function (global) {
    global.runLegacyResourceGpuSmoke = function (imagePath, completed) {
        var scene = cc.director.getRunningScene();
        if (!scene) throw Error('Resource GPU test requires a running Scene');
        var result = {passed: false, checks: [], nativeCallbacks: 0, drawnFrames: 0};
        var finished = false, listener = null, sprite = null, textures = [];
        function check(value, description) {
            result.checks.push({passed: !!value, description: description});
            if (!value) throw Error(description);
        }
        function finish(error) {
            if (finished) return;
            finished = true;
            if (error) result.error = String(error.stack || error);
            if (listener) cc.eventManager.removeListener(listener);
            if (sprite) sprite.removeFromParent();
            result.passed = !result.error && result.checks.every(function (item) { return item.passed; });
            global.legacyResourceGpuResult = result;
            cc.log('LEGACY_RESOURCE_GPU_RESULT ' + JSON.stringify(result));
            if (completed) completed(result);
        }
        function verifyCachedTexture(texture) {
            var target = {token: 73}, calls = 0;
            function cached(actual) {
                check(this === target && this.token === 73, 'cached callback preserves explicit target');
                check(actual === texture, 'cached callback preserves canonical native texture identity');
                ++calls;
            }
            check(cc.textureCache.addImageAsync(imagePath, cached, target) === texture,
                'public async returns actual immediately cached texture');
            check(calls === 1, 'cached callback completes before return');
            check(cc.textureCache.addImage(imagePath, cached, target) === texture,
                'overloaded addImage returns same cached native texture');
            check(calls === 2, 'overloaded cached callback completes once');
            check(cc.textureCache.addImageAsync(imagePath) === texture, 'cached optional callback is allowed');
            var nativeCalls = 0;
            check(cc.textureCache._addImageAsync(imagePath, function (actual) {
                check(actual === texture, 'raw native async entry returns same real texture');
                ++nativeCalls;
            }) === undefined && nativeCalls === 1, 'raw native entry keeps native return contract');
            sprite = new cc.Sprite(texture);
            check(sprite.getTexture() === texture, 'native Sprite accepts asynchronously uploaded texture');
            sprite.setPosition(cc.winSize.width / 2, cc.winSize.height / 2);
            sprite.setScale(24);
            scene.addChild(sprite);
            listener = cc.eventManager.addCustomListener('director_after_draw', function () {
                if (++result.drawnFrames >= 3) finish();
            });
        }
        try {
            cc.textureCache.removeTextureForKey(imagePath);
            check(!cc.textureCache.getTextureForKey(imagePath), 'test starts without cached fixture');
            var target = {token: 59};
            function loaded(texture) {
                try {
                    check(this.token === 59, 'native queued completion roots callback target across GC');
                    check(texture instanceof cc.Texture2D, 'actual native image decoded and uploaded');
                    check(texture === cc.textureCache.getTextureForKey(imagePath), 'native cache owns canonical texture');
                    textures.push(texture);
                    ++result.nativeCallbacks;
                    if (textures.length === 2) {
                        check(textures[0] === textures[1], 'duplicate queued native requests deduplicate texture');
                        verifyCachedTexture(texture);
                    }
                } catch (error) { finish(error); }
            }
            check(cc.textureCache.addImageAsync(imagePath, loaded, target) === null,
                'uncached async request returns null until completion');
            check(cc.textureCache.addImage(imagePath, loaded, target) === null,
                'uncached addImage callback overload also returns null');
            target = null;
            jsb.garbageCollect();
            scene.scheduleOnce(function () { if (!finished) finish(Error('Native resource GPU timeout')); }, 8);
        } catch (error) { finish(error); }
        return result;
    };
})(this);
