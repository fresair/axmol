/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Supplementary real-GPU RenderTexture API coverage. The unchanged native v3
// registry remains Save/Issue1464; this does not enable its browser-only cases.
// Start and tick from the real Scheduler/update phase after RenderView init.
var AxmolRenderTextureProbeResult = null;
(function (global) {
    var state = null;
    function check(value, message) { if (!value) throw Error('RenderTexture probe: ' + message); }
    var matrixNames = ['projection', 'view', 'view_projection'];
    function cameraMatrices() {
        var camera = state.scene.getDefaultCamera();
        check(camera === state.camera, 'the real Scene default Camera changed identity');
        var matrices = {projection: camera.getProjectionMatrix(), view: camera.getViewMatrix(),
            view_projection: camera.getViewProjectionMatrix()};
        matrixNames.forEach(function (name) {
            check(matrices[name].length === 16, 'native ' + name + ' matrix must contain 16 elements');
            matrices[name].forEach(function (value) {
                check(typeof value === 'number' && isFinite(value), 'native ' + name + ' matrix contains an invalid element');
            });
        });
        return matrices;
    }
    function logCameraMatrices(phase, matrices) {
        // A separate line per native matrix keeps all 16 values within the
        // native logger's 1024-byte limit. No rounded/hash-only comparison.
        matrixNames.forEach(function (name) {
            cc.log('RENDER_TEXTURE_CAMERA_MATRIX ' + JSON.stringify({phase: phase, frame: state.frames,
                matrix: name, values: matrices[name]}));
        });
    }
    function cameraSummary() {
        return {status: 'PASS', matrices: 3, elements_per_matrix: 16,
            snapshots: state.cameraChecks.snapshots, scalar_comparisons: state.cameraChecks.snapshots * 48,
            phases: state.cameraChecks.phases};
    }
    function removeCameraObserver() {
        if (state.cameraListener) {
            cc.eventManager.removeListener(state.cameraListener);
            state.cameraListener = null;
        }
    }
    function verifyCamera(phase) {
        try {
            var matrices = cameraMatrices();
            matrixNames.forEach(function (name) {
                for (var index = 0; index < 16; index++) {
                    check(matrices[name][index] === state.cameraBaseline[name][index],
                        phase + ' changed native Camera ' + name + '[' + index + '] from ' +
                        state.cameraBaseline[name][index] + ' to ' + matrices[name][index]);
                }
            });
            var count = state.cameraChecks.phases[phase] || 0;
            state.cameraChecks.phases[phase] = count + 1;
            state.cameraChecks.snapshots++;
            if (!count) logCameraMatrices(phase, matrices);
        } catch (failure) {
            removeCameraObserver();
            AxmolRenderTextureProbeResult = {status: 'CAMERA_MATRIX_CHANGED', frames: state.frames,
                phase: phase, error: String(failure), files: state.files};
            cc.log('RENDER_TEXTURE_COVERAGE_RESULT ' + JSON.stringify({status: 'CAMERA_MATRIX_CHANGED',
                frame: state.frames, phase: phase, error: String(failure)}));
            throw failure;
        }
    }
    function startCameraObserver() {
        state.camera = state.scene.getDefaultCamera();
        check(state.camera instanceof cc.Camera, 'native default Camera is not available');
        state.cameraBaseline = cameraMatrices();
        state.cameraChecks = {snapshots: 0, phases: {}};
        logCameraMatrices('baseline', state.cameraBaseline);
        state.cameraListener = cc.eventManager.addCustomListener('director_after_draw', function () {
            if (AxmolRenderTextureProbeResult || cc.director.getRunningScene() !== state.scene) return;
            // This callback only observes. All target/Scene construction and
            // navigation stays in the actual Scheduler phase.
            verifyCamera('auto:after-draw');
        });
        check(state.cameraListener, 'after-draw Camera observer was not registered');
    }
    function solid(node, x0, y0, x1, y1, color) {
        node.drawSolidRect(cc.p(x0, y0), cc.p(x1, y1), color);
    }
    function target(scene, width, height, displayX, displayY, displayScale) {
        var texture = new cc.RenderTexture(width, height);
        scene.addChild(texture);
        // Keep the target origin at zero for the autoDraw child-coordinate
        // baseline; position only its display Sprite in this diagnostic.
        texture.getSprite().setPosition(displayX, displayY);
        texture.getSprite().setScale(displayScale);
        return texture;
    }
    function manualPattern(name, texture, width, height) {
        var geometry = new cc.DrawNode();
        texture.addChild(geometry); // Native ownership lasts through command execution.
        solid(geometry, 0, 0, width, height, cc.color(0, 0, 255, 255));
        solid(geometry, 0, 0, width / 2, height / 2, cc.color(0, 255, 0, 255));
        solid(geometry, width / 2, height / 2, width, height, cc.color(255, 0, 0, 255));
        verifyCamera(name + ':before-begin');
        texture.begin();
        verifyCamera(name + ':after-begin');
        geometry.visit();
        verifyCamera(name + ':after-visit');
        texture.end();
        verifyCamera(name + ':after-end');
    }
    function requestSave(name, texture, expectations) {
        var filename = state.prefix + '-' + name + '.png';
        var value = {name: name, file: jsb.fileUtils.getWritablePath() + filename,
            queued_frame: state.frames, accepted: texture.saveToFile(filename, cc.IMAGE_FORMAT_PNG, true),
            expected_canvas_size: expectations.size, expected_samples_bottom_left: expectations.samples};
        check(value.accepted, 'native PNG save request rejected for ' + name);
        state.files.push(value);
    }
    function marker(x0, x1, color) {
        var width = state.size.width, height = state.size.height;
        state.autoGeometry.clear();
        solid(state.autoGeometry, width * x0, height * 0.25, width * x1, height * 0.75, color);
    }
    global.axmolStartRenderTextureCoverageProbe = function () {
        check(!state, 'probe already started');
        var size = cc.director.getWinSize();
        check(size.width > 0 && size.height > 0, 'graphics canvas is not initialized');
        var scene = new cc.Scene();
        state = {scene: scene, size: size, frames: 0, files: [],
            prefix: 'render-texture-probe-' + Date.now(), phase: 0};
        state.full = target(scene, size.width, size.height, size.width * 0.2, size.height * 0.5, 0.25);
        state.half = target(scene, size.width / 2, size.height / 2, size.width * 0.5, size.height * 0.5, 0.5);
        state.auto = target(scene, size.width, size.height, size.width * 0.8, size.height * 0.5, 0.25);
        state.autoGeometry = new cc.DrawNode();
        state.auto.addChild(state.autoGeometry);
        state.auto.setAutoDraw(true);
        state.auto.setClearFlags(0x4000);
        state.auto.setClearColor(cc.color(0, 0, 0, 0));
        cc.director.runScene(scene);
        return true;
    };
    global.axmolTickRenderTextureCoverageProbe = function () {
        if (!state || AxmolRenderTextureProbeResult) return AxmolRenderTextureProbeResult;
        if (cc.director.getRunningScene() !== state.scene || !state.scene.isRunning()) return null;
        var width = state.size.width, height = state.size.height;
        var frame = ++state.frames;
        if (!state.cameraBaseline) startCameraObserver();
        verifyCamera('auto:before-draw');
        if (frame === 1) {
            manualPattern('manual-full', state.full, width, height);
            manualPattern('manual-half', state.half, width / 2, height / 2);
            marker(0.1, 0.3, cc.color(255, 0, 0, 255));
        }
        if (frame === 5) requestSave('manual-full', state.full, {
            size: [width, height], samples: [[0.25, 0.25, 0, 255, 0, 255],
                [0.75, 0.75, 255, 0, 0, 255], [0.75, 0.25, 0, 0, 255, 255]]});
        if (frame === 10) requestSave('manual-half', state.half, {
            size: [width / 2, height / 2], samples: [[0.25, 0.25, 0, 255, 0, 255],
                [0.75, 0.75, 255, 0, 0, 255], [0.75, 0.25, 0, 0, 255, 255]]});
        if (frame === 15) requestSave('auto-red', state.auto, {
            size: [width, height], samples: [[0.2, 0.5, 255, 0, 0, 255], [0.7, 0.5, 0, 0, 0, 0]]});
        if (frame === 20) marker(0.6, 0.8, cc.color(0, 255, 0, 255));
        if (frame === 25) requestSave('auto-green-clear', state.auto, {
            size: [width, height], samples: [[0.2, 0.5, 0, 0, 0, 0], [0.7, 0.5, 0, 255, 0, 255]]});
        if (frame === 30) {
            state.auto.setClearFlags(0);
            marker(0.1, 0.3, cc.color(0, 0, 255, 255));
        }
        if (frame === 35) requestSave('auto-blue-no-clear', state.auto, {
            size: [width, height], samples: [[0.2, 0.5, 0, 0, 255, 255], [0.7, 0.5, 0, 255, 0, 255]]});
        if (frame >= 40) {
            var ready = state.files.length === 5;
            state.files.forEach(function (file) { file.written = jsb.fileUtils.isFileExist(file.file); ready = ready && file.written; });
            if (ready || frame >= 720) {
                check((state.cameraChecks.phases['auto:after-draw'] || 0) >= 35,
                    'at least 35 rendered autoDraw frames must preserve all Camera matrix elements');
                removeCameraObserver();
                AxmolRenderTextureProbeResult = {status: ready ? 'FILES_WRITTEN_PIXELS_PENDING' : 'FILE_WRITE_TIMEOUT',
                    frames: frame, canvas_size: state.size, files: state.files,
                    camera_matrices: state.cameraBaseline, camera_checks: cameraSummary(),
                    scope: 'Supplementary native GPU begin/end, half/full canvas mapping, PNG output and multi-frame autoDraw clear/no-clear. Every native default Camera projection/view/view-projection element stays exactly equal before/inside/after manual passes and across actual rendered autoDraw frames. Saved-image pixel checks remain required.'};
                state.files.forEach(function (file) { cc.log('RENDER_TEXTURE_COVERAGE_FILE ' + JSON.stringify(file)); });
                cc.log('RENDER_TEXTURE_CAMERA_RESULT ' + JSON.stringify(cameraSummary()));
                cc.log('RENDER_TEXTURE_COVERAGE_RESULT ' + JSON.stringify({status: AxmolRenderTextureProbeResult.status,
                    frames: frame, canvas_size: state.size, file_count: state.files.length}));
            }
        }
        return AxmolRenderTextureProbeResult;
    };
})(this);
