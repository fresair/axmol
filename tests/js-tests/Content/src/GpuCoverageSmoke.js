// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Additional GPU checks; the imported v3 project and its registry are unchanged.
(function (global) {
    var mode = "", result = null;
    function finish(value) {
        result = value;
        if (mode === "materialTexture" || mode === "labelAtlasSampler" || mode === "spriteTextureFormats") {
            var filename = mode === "materialTexture" ? "gpu-material-texture-audit.json" :
                mode === "spriteTextureFormats" ? "gpu-sprite-texture-formats.json" : "gpu-label-atlas-sampler.json";
            if (!cc.FileUtils.getInstance().writeStringToFile(JSON.stringify(value),
                    cc.FileUtils.getInstance().getWritablePath() + filename))
                throw Error("Cannot persist complete GPU coverage facts");
            cc.log("GPU_COVERAGE_FACTS_FILE " + filename);
        }
        (mode === "spriteTextureFormats" ? [] : value.checks || []).forEach(function (check) {
            var text = JSON.stringify(check);
            if (text.length > 850) text = JSON.stringify({passed: check.passed,
                detail: check.detail || check.description, actual_in_full_facts_file: true});
            cc.log("GPU_COVERAGE_CHECK " + text);
        });
        cc.log("GPU_COVERAGE_SUMMARY " + JSON.stringify({passed: value.passed,
            check_count: (value.checks || []).length, draw_calls: value.drawCalls,
            native_uniform_callbacks: value.nativeUniformCallbackCalls,
            native_resource_callbacks: value.nativeCallbacks, drawn_frames: value.drawnFrames,
            clear_frames: value.clearFrames, error: value.error || null}));
    }
    function loadOriginalProject(files) {
        var applicationRoot = files.getDefaultResourceRootPath();
        var previous = files.getSearchPaths(), projectRoot = applicationRoot + "legacy/js-tests/";
        files.setDefaultResourceRootPath(projectRoot);
        files.setSearchPaths([applicationRoot + "legacy-compat/", projectRoot + "res/resjs/",
            projectRoot + "res/", projectRoot + "src/", projectRoot,
            applicationRoot + "axslc/", applicationRoot].concat(previous));
        // The original main loads every project.json script and selects its
        // original canvas, resources and menu. The observer changes no game code.
        require("main.js");
    }
    global.jsGpuCoverageStart = function (requested) {
        mode = requested;
        var files = cc.FileUtils.getInstance();
        files.addSearchPath("legacy/js-tests/res", true);
        files.addSearchPath("legacy/js-tests/res/resjs", true);
        cc.log("GPU_COVERAGE_CONFIGURATION " + JSON.stringify({
            mode: mode, physical_size: AXMOL_GPU_PHYSICAL_SIZE,
            previous_policy: AXMOL_GPU_RESOLUTION_POLICY,
            scope: "Additional actual-GPU API regression; original v3 assertions remain separate."
        }));
        if (mode === "componentLose" || mode === "componentWin") {
            loadOriginalProject(files);
            require("src/ComponentGameplayProbe.js");
            var componentBaseline = new cc.Scene();
            cc.director.runScene(componentBaseline);
            componentBaseline.scheduleOnce(function () {
                axmolStartComponentGameplayProbe({mode: mode === "componentLose" ? "lose" : "win", timeout: 25});
            }, 0);
        } else if (mode === "legacyGL" || mode === "legacyGLTexture" || mode === "ttfConfig" ||
                   mode === "filterClear" || mode === "resource" || mode === "remoteResource" ||
                   mode === "materialTexture" || mode === "labelAtlasSampler" || mode === "spriteTextureFormats") {
            require(mode === "legacyGL" ? "src/LegacyGLGpuSmoke.js" :
                mode === "legacyGLTexture" ? "src/LegacyGLTextureSlotSmoke.js" :
                mode === "filterClear" ? "src/LegacyGLFilterClearSmoke.js" :
                mode === "remoteResource" ? "src/LegacyRemoteResourceGpuSmoke.js" :
                mode === "materialTexture" ? "src/LegacyMaterialTextureAudit.js" :
                mode === "labelAtlasSampler" ? "src/LegacyAtlasSamplerGpuSmoke.js" :
                mode === "spriteTextureFormats" ? "src/SpriteTextureFormatsGpuSmoke.js" :
                mode === "resource" ? "src/LegacyResourceGpuSmoke.js" : "src/LabelTTFConfigGpuSmoke.js");
            // Establish an actual running native scene before starting. A
            // synchronous validation failure must still get Scheduler ticks,
            // and the probe restores this explicitly selected valid policy.
            cc.view.setDesignResolutionSize(AXMOL_GPU_PHYSICAL_SIZE.width,
                AXMOL_GPU_PHYSICAL_SIZE.height, 0);
            var baseline = new cc.Scene();
            cc.director.runScene(baseline);
            baseline.scheduleOnce(function () {
                if (mode === "ttfConfig") runLegacyTTFConfigGpuSmoke(finish);
                else if (mode === "filterClear") runLegacyGLFilterClearGpuSmoke({
                    physicalSize: AXMOL_GPU_PHYSICAL_SIZE, restorePolicy: 0,
                    checkerPath: "src/LegacyGLSamplerChecker.png"}, finish);
                else if (mode === "resource") runLegacyResourceGpuSmoke("src/LegacyGLSamplerChecker.png", finish);
                else if (mode === "remoteResource") runLegacyRemoteResourceGpuSmoke("http://127.0.0.1:19093", finish);
                else if (mode === "materialTexture") runLegacyMaterialTextureAudit(finish);
                else if (mode === "labelAtlasSampler") runLegacyAtlasSamplerGpuSmoke({
                    physicalSize: AXMOL_GPU_PHYSICAL_SIZE, restorePolicy: 0,
                    checkerPath: "src/LegacyGLSamplerChecker.png",
                    replacementCheckerPath: "src/LegacyAtlasReplacementChecker.png"}, finish);
                else if (mode === "spriteTextureFormats") runLegacySpriteTextureFormatsGpuSmoke({
                    physicalSize: AXMOL_GPU_PHYSICAL_SIZE, restorePolicy: 0}, finish);
                else {
                    var start = mode === "legacyGL" ? runLegacyGLGpuSmoke : runLegacyGLTextureSlotGpuSmoke;
                    start({physicalSize: AXMOL_GPU_PHYSICAL_SIZE, restorePolicy: 0}, finish);
                }
            }, 0);
        } else if (mode === "renderTexture") {
            require("src/RenderTextureCoverageProbe.js");
            // A known canvas makes half/full target dimensions reproducible.
            cc.view.setDesignResolutionSize(960, 540, 0);
            axmolStartRenderTextureCoverageProbe();
        } else throw new Error("Unknown additional GPU coverage mode: " + mode);
    };
    global.jsGpuCoverageResult = function () {
        if (mode === "componentLose" || mode === "componentWin") {
            result = axmolTickComponentGameplayProbe();
            if (!result) return 1;
            return result.status === "PASS" ? 0 : 68;
        }
        if (mode === "renderTexture") {
            result = axmolTickRenderTextureCoverageProbe();
            if (!result) return 1;
            // Zero here records completed native image writes. The saved PNGs
            // still require the independent dimension/RGBA checker.
            return result.status === "FILES_WRITTEN_PIXELS_PENDING" ? 0 : 66;
        }
        if (!result) return 1;
        return result.passed ? 0 : 65;
    };
})(this);
