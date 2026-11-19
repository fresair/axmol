// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Identical measurement script for the two complete native JSB test Apps.
// This is a supplemental controlled workload, not a full-menu performance score.
var JsbComparisonBenchmark = (function () {
    "use strict";
    var files, request, root, started = false;
    function requireFact(value, message) { if (!value) throw new Error(message); }
    function now() { return performance.now(); }
    function stats() {
        var result = __jsbComparisonProcessStats();
        requireFact(isFinite(result.monotonic_ms) && isFinite(result.user_ms) && isFinite(result.system_ms), "native process clock");
        return result;
    }
    function summary(values) {
        var ordered = values.slice().sort(function (a, b) { return a - b; });
        var total = 0;
        for (var i = 0; i < values.length; ++i) total += values[i];
        function rank(q) { return ordered[Math.ceil(ordered.length * q) - 1]; }
        return {count: values.length, min: ordered[0], max: ordered[ordered.length - 1],
            mean: total / values.length, p50: rank(0.5), p95: rank(0.95), p99: rank(0.99)};
    }
    function publish(report) {
        report.schema = 1;
        report.nonce = request.nonce;
        report.request = request;
        report.wall_finished_ms = Date.now();
        report.scope = "Identical supplemental native JSB workload inside each complete js-tests App; original menus remain separate.";
        report.measurement = {
            clock: "performance.now for JS body/intervals; identical native CLOCK_MONOTONIC/getrusage helper at boundaries",
            cpu: "Whole process user/system CPU; 100 percent corresponds to one logical CPU, not all device cores",
            rss: "VmRSS endpoint samples in KiB; -1 means unavailable; not PSS or exclusive memory",
            frame_intervals: "Include scheduling/rendering/display pacing; do not equal GPU execution time",
            export: "One result write after timing; no per-frame logging or file flush"};
        requireFact(files.writeStringToFile(JSON.stringify(report), root + "jsb-performance-result.json"), "result write");
        requireFact(files.writeStringToFile(JSON.stringify({nonce: request.nonce, status: report.status,
            complete: true, file: "jsb-performance-result.json"}), root + "jsb-performance-ready.json"), "ready write");
        cc.log("JSB_PERF_DONE " + request.nonce + " " + report.status + " " + request.mode);
    }
    function failed(error) {
        publish({status: "FAIL", error: String(error), stack: error && error.stack ? String(error.stack) : null});
    }
    function context(scene) {
        var win = cc.director.getWinSize(), frame = cc.view.getFrameSize();
        return {design: {width: win.width, height: win.height}, frame: {width: frame.width, height: frame.height},
            native_children: scene.getChildrenCount(), sys_os: cc.sys.os, sys_platform: cc.sys.platform,
            gpu_backend: "Requires host/native startup identity; legacy gl strings are not a Vulkan backend proof"};
    }
    function spriteScene() {
        return cc.Scene.extend({
            onEnter: function () {
                this._super();
                try {
                    this._frame = 0; this._nodes = []; this._bodies = []; this._intervals = [];
                    this._baseChildren = this.getChildrenCount();
                    var texture = null;
                    var candidates = ["res/Images/grossini.png", "Images/grossini.png", "legacy/js-tests/res/Images/grossini.png"];
                    for (var i = 0; i < candidates.length; ++i)
                        if (files.isFileExist(candidates[i])) { texture = candidates[i]; break; }
                    requireFact(texture !== null, "identical grossini texture missing");
                    this._texture = texture;
                    for (var n = 0; n < request.sprite_count; ++n) {
                        var node = new cc.Sprite(texture);
                        requireFact(cc.sys.isObjectValid(node) && node.getTexture(), "real native Sprite texture");
                        var x = (n * 37) % 800, y = (n * 23) % 450;
                        node.setScale(0.5); node.setPosition(x, y); this.addChild(node);
                        this._nodes.push({node: node, x: x, y: y});
                    }
                    this._context = context(this);
                    requireFact(this._context.design.width === 800 && this._context.design.height === 450, "matched design size");
                    requireFact(this.getChildrenCount() - this._baseChildren === request.sprite_count, "native sprite count");
                    this._last = now();
                    this.scheduleUpdate();
                } catch (error) { this.unscheduleUpdate(); failed(error); }
            },
            update: function () {
                try {
                    if (this._frame === request.warmup_frames) {
                        this._context = context(this);
                        requireFact(this._context.design.width === 800 && this._context.design.height === 450 &&
                            this._context.frame.width > 0 && this._context.frame.height > 0, "settled view before measurement");
                        this._processBefore = stats(); this._wallBegin = Date.now();
                    }
                    var begin = now();
                    for (var i = 0; i < this._nodes.length; ++i) {
                        var item = this._nodes[i];
                        item.x = (item.x + (i % 7 + 1) / 3) % 800;
                        item.node.setPosition(item.x, item.y);
                        item.node.setRotation(this._frame % 360);
                    }
                    var body = now() - begin;
                    if (this._frame >= request.warmup_frames) {
                        this._bodies.push(body); this._intervals.push(begin - this._last);
                    }
                    this._last = begin; ++this._frame;
                    if (this._bodies.length === request.samples) {
                        var processAfter = stats(), wallEnd = Date.now();
                        this.unscheduleUpdate();
                        var finalContext = context(this);
                        requireFact(JSON.stringify(finalContext.design) === JSON.stringify(this._context.design) &&
                            JSON.stringify(finalContext.frame) === JSON.stringify(this._context.frame), "stable measured viewport");
                        requireFact(this.getChildrenCount() - this._baseChildren === request.sprite_count, "final native sprite count");
                        var first = this._nodes[0], last = this._nodes[this._nodes.length - 1];
                        requireFact(Math.abs(first.node.getPositionX() - first.x) < 0.001 &&
                            Math.abs(last.node.getPositionX() - last.x) < 0.001, "real final native positions");
                        publish({status: "PASS", context: this._context, final_context: finalContext, texture: this._texture,
                            sprite_count: this._nodes.length, native_calls_per_update: this._nodes.length * 2,
                            simulated_step_seconds: 1 / 60, warmup_frames: request.warmup_frames,
                            samples: request.samples, wall_begin_ms: this._wallBegin, wall_end_ms: wallEnd,
                            process_before: this._processBefore, process_after: processAfter,
                            script_update_ms: summary(this._bodies), frame_interval_ms: summary(this._intervals),
                            raw_script_update_ms: this._bodies, raw_frame_interval_ms: this._intervals,
                            final_native_positions: [first.node.getPosition(), last.node.getPosition()]});
                    }
                } catch (error) { this.unscheduleUpdate(); failed(error); }
            }
        });
    }
    var operations = [
        {name: "pure_js", iterations: 250000, native_calls: 0, run: function (node, count) {
            var sum = 0; for (var i = 0; i < count; ++i) sum = (sum + (i & 255)) | 0; return sum;
        }, expected: 31866936},
        {name: "get_position_x", iterations: 100000, native_calls: 100000, run: function (node, count) {
            var sum = 0; for (var i = 0; i < count; ++i) sum += node.getPositionX(); return sum;
        }, expected: 700000},
        {name: "set_position_x", iterations: 100000, native_calls: 100001, run: function (node, count) {
            for (var i = 0; i < count; ++i) node.setPositionX(i & 1023); return node.getPositionX();
        }, expected: 671},
        {name: "get_position_object", iterations: 50000, native_calls: 50000, run: function (node, count) {
            var sum = 0; for (var i = 0; i < count; ++i) { var p = node.getPosition(); sum += p.x + p.y; } return sum;
        }, expected: 600000}
    ];
    function callScene() {
        return cc.Scene.extend({
            onEnter: function () {
                this._super();
                try {
                    this._node = new cc.Node(); this.addChild(this._node);
                    this._frame = 0; this._operation = 0; this._batch = 0; this._rows = [];
                    this._context = context(this); this.scheduleUpdate();
                } catch (error) { this.unscheduleUpdate(); failed(error); }
            },
            update: function () {
                try {
                    if (++this._frame <= request.warmup_frames) return;
                    if (this._operation === 0 && this._batch === 0) this._context = context(this);
                    var operation = operations[this._operation];
                    this._node.setPosition(7, 5);
                    var before = stats(), begin = now();
                    var checksum = operation.run(this._node, operation.iterations);
                    var elapsed = now() - begin, after = stats();
                    requireFact(checksum === operation.expected, "native operation checksum " + operation.name);
                    requireFact(isFinite(elapsed) && elapsed > 0, "positive timed batch");
                    if (this._batch >= 3) this._rows.push({operation: operation.name, batch: this._batch - 3,
                        iterations: operation.iterations, native_calls: operation.native_calls,
                        checksum: checksum, elapsed_ms: elapsed, process_before: before, process_after: after});
                    if (++this._batch === request.batch_samples + 3) { ++this._operation; this._batch = 0; }
                    if (this._operation === operations.length) {
                        this.unscheduleUpdate();
                        publish({status: "PASS", context: this._context, warmup_frames: request.warmup_frames,
                            warmup_batches_per_operation: 3, measured_batches_per_operation: request.batch_samples,
                            rows: this._rows, unit: "ms", mode_scope: "Calls use each engine's real Node JSB; native timing helper is identical and is outside each JS body timer."});
                    }
                } catch (error) { this.unscheduleUpdate(); failed(error); }
            }
        });
    }
    return {
        startIfRequested: function () {
            if (started) throw new Error("JSB benchmark already started");
            files = jsb.fileUtils; root = files.getWritablePath();
            if (!files.isFileExist(root + "jsb-performance-request.json")) return false;
            request = JSON.parse(files.getStringFromFile(root + "jsb-performance-request.json"));
            requireFact(request.schema === 1 && /^[A-Za-z0-9_-]{1,100}$/.test(request.nonce), "request identity");
            requireFact(request.mode === "sprites" || request.mode === "calls", "known workload");
            requireFact(request.warmup_frames === 120 && request.samples === 600 && request.batch_samples === 15, "fixed sampling recipe");
            requireFact(request.sprite_count === 500 || request.sprite_count === 2000, "fixed sprite workload");
            requireFact(typeof __jsbComparisonProcessStats === "function" && typeof performance.now === "function", "real shared timing helper");
            cc.director.setDisplayStats(false); cc.director.setAnimationInterval(1 / 60);
            cc.view.setDesignResolutionSize(800, 450, cc.ResolutionPolicy.SHOW_ALL);
            started = true;
            var Scene = request.mode === "sprites" ? spriteScene() : callScene();
            cc.director.runScene(new Scene());
            return true;
        }
    };
}());
