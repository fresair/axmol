// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Uses http-fixture.c on the device; this supplements, and does not replace,
// the original external-URL RemoteTextureTest.
(function (global) {
    global.runLegacyRemoteResourceGpuSmoke = function (origin, completed) {
        var scene = cc.director.getRunningScene();
        if (!scene) throw Error('Remote resource audit requires a running Scene');
        var result = {passed: false, checks: [], nativeCallbacks: 0, drawnFrames: 0, origin: origin};
        var complete = false, listener = null, sprite = null, loaded = null, seen = 0;
        var path = origin + '/checker.png';
        function check(ok, text, actual, expected) {
            result.checks.push({passed: !!ok, description: text, actual: actual, expected: expected});
            if (!ok) throw Error(text);
        }
        function finish(error) {
            if (complete) return;
            complete = true;
            if (error) result.error = String(error.stack || error);
            if (listener) cc.eventManager.removeListener(listener);
            if (sprite) sprite.removeFromParent();
            result.passed = !result.error && seen === 3 && result.drawnFrames >= 3;
            global.legacyRemoteResourceGpuResult = result;
            cc.log('LEGACY_REMOTE_RESOURCE_GPU_RESULT ' + JSON.stringify({passed: result.passed,
                checks: result.checks.length, nativeCallbacks: result.nativeCallbacks,
                drawnFrames: result.drawnFrames, origin: result.origin, error: result.error}));
            if (completed) completed(result);
        }
        function ready() {
            ++seen;
            ++result.nativeCallbacks;
            if (seen !== 3) return;
            var cachedCalls = 0;
            check(cc.textureCache.addImageAsync(path, function (texture) {
                check(texture === loaded, 'remote URL cache reuses actual uploaded texture');
                ++cachedCalls;
            }) === loaded && cachedCalls === 1, 'remote cached request returns texture synchronously');
            sprite = new cc.Sprite(loaded);
            check(sprite.getTexture() === loaded, 'native Sprite uses HTTP image texture');
            check(loaded.getPixelsWide() === 2 && loaded.getPixelsHigh() === 2,
                'HTTP fixture retains actual two-by-two texture dimensions');
            loaded.setAliasTexParameters();
            sprite.setScale(24);
            sprite.setPosition(cc.winSize.width / 2, cc.winSize.height / 2);
            scene.addChild(sprite);
            listener = cc.eventManager.addCustomListener('director_after_draw', function () {
                if (++result.drawnFrames < 3) return;
                try {
                    // The App's GPU probe selects physical-size design resolution.
                    // Sample the real four displayed texels, using native GL's
                    // physical framebuffer contract and bottom-left coordinates.
                    var x = Math.floor(cc.winSize.width / 2), y = Math.floor(cc.winSize.height / 2);
                    [[-12, -12, 0, 0, 255, 255], [12, -12, 255, 255, 255, 255],
                        [-12, 12, 255, 0, 0, 255], [12, 12, 0, 255, 0, 255]].forEach(function (entry, index) {
                        var bytes = new Uint8Array(4);
                        gl.readPixels(x + entry[0], y + entry[1], 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
                        var actual = Array.prototype.slice.call(bytes), expected = entry.slice(2);
                        check(JSON.stringify(actual) === JSON.stringify(expected),
                            'actual HTTP image framebuffer texel ' + index, actual, expected);
                    });
                    finish();
                } catch (error) { finish(error); }
            });
        }
        try {
            cc.textureCache.removeTextureForKey(path);
            var target = {token: 83};
            check(cc.textureCache.addImageAsync(path, function (texture) {
                try {
                    check(this.token === 83, 'HTTP success callback preserves target across GC');
                    check(texture instanceof cc.Texture2D, 'actual HTTP PNG decoded and uploaded');
                    check(texture === cc.textureCache.getTextureForKey(path), 'remote URL is native texture cache key');
                    loaded = texture;
                    ready();
                } catch (error) { finish(error); }
            }, target) === null, 'remote uncached request returns null');
            cc.textureCache.addImageAsync(origin + '/missing', function (texture) {
                try {
                    check(this.token === 83 && texture === null, 'HTTP 404 public callback retains target and null result');
                    ready();
                } catch (error) { finish(error); }
            }, target);
            cc.loader.loadImg(origin + '/also-missing', function (error, texture) {
                try {
                    check(!!error && !texture, 'loader failure callback distinguishes HTTP 404');
                    ready();
                } catch (failure) { finish(failure); }
            });
            target = null;
            jsb.garbageCollect();
            scene.scheduleOnce(function () { finish(Error('Remote resource GPU timeout')); }, 10);
        } catch (error) { finish(error); }
        return result;
    };
})(this);
