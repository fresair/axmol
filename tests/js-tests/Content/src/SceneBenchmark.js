// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Load in a rendered JS project and run new AxmolBenchmarkScene().
var AxmolBenchmarkResult = null;
var AxmolBenchmarkScene = cc.Scene.extend({
    onEnter: function () {
        this._super();
        this._sprites = [];
        this._samples = [];
        this._intervals = [];
        this._frame = 0;
        this._lastFrame = performance.now();
        var size = cc.director.getWinSize();
        this._size = size;
        this._texture = typeof AXMOL_BENCHMARK_TEXTURE === "string" ? AXMOL_BENCHMARK_TEXTURE : "res/HelloWorld.png";
        if (!jsb.fileUtils.isFileExist(this._texture))
            throw new Error("Scene benchmark texture is missing: " + this._texture);
        for (var i = 0; i < 500; ++i) {
            var sprite = new cc.Sprite(this._texture);
            if (!jsb.isObjectValid(sprite)) throw new Error("Scene benchmark texture could not be loaded: " + this._texture);
            sprite.setScale(0.05);
            sprite.setPosition((i * 37) % size.width, (i * 23) % size.height);
            this.addChild(sprite);
            this._sprites.push({node: sprite, x: (i * 37) % size.width, y: (i * 23) % size.height});
        }
        this.scheduleUpdate();
    },
    update: function (dt) {
        var frameTime = performance.now(), started = frameTime;
        for (var i = 0; i < this._sprites.length; ++i) {
            var sprite = this._sprites[i];
            sprite.x = (sprite.x + (i % 7 + 1) * 20 * dt) % this._size.width;
            sprite.node.setPosition(sprite.x, sprite.y);
            sprite.node.setRotation(this._frame % 360);
        }
        var elapsed = performance.now() - started;
        if (this._frame >= 60) {
            this._samples.push(elapsed);
            this._intervals.push(frameTime - this._lastFrame);
        }
        this._lastFrame = frameTime;
        ++this._frame;
        if (this._samples.length === 600) {
            function statistics(values) {
                var sorted = values.slice().sort(function (a, b) { return a - b; });
                return {p50: sorted[Math.ceil(sorted.length * .5) - 1], p95: sorted[Math.ceil(sorted.length * .95) - 1],
                    min: sorted[0], max: sorted[sorted.length - 1]};
            }
            AxmolBenchmarkResult = {sprite_count: 500, warmup_frames: 60, samples: 600,
                texture: this._texture, canvas_size: {width: this._size.width, height: this._size.height},
                unit: "ms", script_update: statistics(this._samples), frame_interval: statistics(this._intervals)};
            cc.log("AXMOL_SCENE_BENCHMARK " + JSON.stringify(AxmolBenchmarkResult));
            this.unscheduleUpdate();
        }
    }
});
