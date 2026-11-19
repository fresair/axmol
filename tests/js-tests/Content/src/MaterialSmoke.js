// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Run after jsb_boot.js with a real initialized graphics device. This is a
// focused assertion fixture; the original MaterialSystemTest source/resources
// remain unchanged and are exercised separately by the complete v3 suite.
var MaterialRegressionState = {
    started: false, frames: 0, blurCallbacks: 0, outlineCallbacks: 0,
    materials: [], temporaryFiles: [], completed: false, errors: []
};

(function (global) {
    var state = MaterialRegressionState;
    function expect(ok, text) { if (!ok) throw new Error("Material regression: " + text); }
    function keep(material) {
        expect(material, "native factory returned a material");
        material.retain();
        state.materials.push(material);
        return material;
    }
    function shader(properties, technique) {
        var group = properties.getNamespace(technique, false, false);
        expect(group, "Properties technique " + technique);
        var pass = group.getNamespace("0", false, false);
        expect(pass, "Properties pass 0");
        var source = pass.getNamespace("shader", true, false);
        expect(source, "Properties shader namespace");
        return source;
    }
    function stateFor(material, technique, passIndex) {
        var nativeTechnique = material.getTechniqueByName(technique);
        expect(nativeTechnique, "native technique " + technique);
        var pass = nativeTechnique.getPassByIndex(passIndex || 0);
        expect(pass, "native material pass");
        var program = pass.getGLProgramState();
        expect(program && program === pass.getProgramState(), "Pass alias preserves native ProgramState identity");
        return program;
    }
    function sprite(scene, program, x, y) {
        var value = new cc.Sprite("Images/grossini.png");
        value.setNormalizedPosition(cc.p(x, y));
        value.setGLProgramState(program);
        expect(value.getGLProgramState() === program, "Sprite retains the actual native program state");
        scene.addChild(value);
        return value;
    }
    function file(text, suffix) {
        var files = cc.FileUtils.getInstance();
        var path = files.getWritablePath() + "material-regression-" + suffix + ".material";
        expect(files.writeStringToFile(text, path), "write temporary material fixture");
        state.temporaryFiles.push(path);
        return path;
    }
    function materialText(name, vertex, fragment, definitions) {
        return "material " + name + "\n{\n technique sample\n {\n  pass 0\n  {\n   shader\n   {\n" +
            "    vertexShader = " + vertex + "\n    fragmentShader = " + fragment + "\n" +
            (definitions ? "    defines = " + definitions + "\n" : "") +
            "   }\n  }\n }\n}\n";
    }
    function archive(sources, suffix) {
        for (var i = 0; i < sources.length; i++) {
            var value = sources[i];
            if (value.defines === "" && value.source.slice(-suffix.length) === suffix) return value.archive;
        }
        throw new Error("Missing shader manifest entry " + suffix);
    }

    function assertions(scene) {
        var files = cc.FileUtils.getInstance();
        // These are the original, imported files and the exact original names.
        files.addSearchPath("legacy/js-tests/res/resjs", true);
        files.addSearchPath("legacy/js-tests/res", true);
        var properties = cc.Properties.createNonRefCounted("Materials/2d_effects.material#sample");
        var propertiesPath = files.fullPathForFilename("Materials/2d_effects.material");
        cc.log("MATERIAL_REGRESSION Properties URL path=" + propertiesPath +
            " relativeSize=" + files.getFileSize("Materials/2d_effects.material") +
            " resolvedSize=" + (propertiesPath ? files.getFileSize(propertiesPath) : "<missing>") +
            " result=" + String(properties) + " id=" + (properties ? String(properties.getId()) : "<null>"));
        expect(properties && properties.getId() === "sample", "original 2D Properties URL");
        var source = shader(properties, "blur");
        var oldVertex = source.getString("vertexShader"), oldFragment = source.getString("fragmentShader");
        var oldDefines = source.getString("defines");
        properties.rewind();
        expect(properties.getNextNamespace().getId() === "blur", "initial Properties iterator");
        var effects = keep(cc.Material.createWithProperties(properties));
        expect(effects.getTechniqueCount() === 4 && effects.getName() === "sample", "original four 2D native techniques");
        expect(source.getString("vertexShader") === oldVertex && source.getString("fragmentShader") === oldFragment &&
            source.getString("defines") === oldDefines, "factory leaves original shader paths and defines intact");
        expect(properties.getNextNamespace().getId() === "outline", "factory does not consume caller's Properties iterator");
        var second = keep(cc.Material.createWithProperties(properties));
        expect(second !== effects && second.getTechniqueCount() === 4, "repeated native factory creates a separate material");
        var names = ["blur", "outline", "noise", "edge_detect"];
        for (var i = 0; i < names.length; i++) sprite(scene, stateFor(effects, names[i]), 0.15 + i * 0.22, 0.75);

        var automatic = keep(cc.Material.createWithFilename("Materials/auto_binding_test.material#sample"));
        expect(automatic.getTechniqueCount() === 4, "original auto-binding material");
        var blur = stateFor(automatic, "blur"), outline = stateFor(automatic, "outline");
        blur.setUniformCallback("blurRadius", function (program, uniform) {
            expect(this === blur && program && uniform && uniform.location, "blur callback receiver and native location");
            state.blurCallbacks++;
            program.setUniformLocationWith1f(uniform.location, 2.0);
        });
        outline.setUniformCallback("u_outlineColor", function (program, uniform) {
            expect(this === outline && program && uniform && uniform.location, "outline callback receiver and native location");
            state.outlineCallbacks++;
            program.setUniformLocationWith3f(uniform.location, 0.2, 0.4, 0.8);
        });
        sprite(scene, blur, 0.35, 0.45);
        sprite(scene, outline, 0.65, 0.45);
        state.blur = blur;
        state.outline = outline;

        var threeD = keep(cc.Material.createWithFilename("Materials/3d_effects.material"));
        expect(threeD.getTechniqueCount() === 3 && threeD.getName() === "spaceship", "original 3D native material");
        expect(threeD.getTechniqueByName("outline").getPassCount() === 3, "original three-pass 3D outline");
        var duplicate = keep(threeD.clone());
        expect(duplicate !== threeD && duplicate.getTechniqueByName("normal") !== threeD.getTechniqueByName("normal"),
            "native Material clone contains independent techniques");
        expect(stateFor(duplicate, "normal") !== stateFor(threeD, "normal"), "native Material clone contains independent program states");
        threeD.setTechnique("lit");
        duplicate.setTechnique("outline");
        expect(threeD.getTechnique().getName() === "lit" && duplicate.getTechnique().getName() === "outline",
            "cloned materials select techniques independently");
        var firstModel = new jsb.Sprite3D("Sprite3DTest/boss1.obj");
        firstModel.setMaterial(threeD);
        firstModel.setScale(2);
        firstModel.setNormalizedPosition(cc.p(0.3, 0.15));
        scene.addChild(firstModel);
        var clonedModel = new jsb.Sprite3D("Sprite3DTest/boss1.obj");
        clonedModel.setMaterial(duplicate);
        clonedModel.setScale(2);
        clonedModel.setNormalizedPosition(cc.p(0.7, 0.15));
        scene.addChild(clonedModel);
        scene.addChild(new jsb.AmbientLight(cc.color.RED));
        scene.addChild(new jsb.DirectionLight(cc.math.vec3(-1, 1, 0), cc.color.GREEN));

        var variablePath = file("${vertex} = Shaders/example_Simple.vsh\n" +
            materialText("variables", "${vertex}", "Shaders/example_Outline.fsh", ""), "variables");
        var variableProperties = cc.Properties.createNonRefCounted(variablePath + "#variables");
        var variableSource = shader(variableProperties, "sample");
        keep(cc.Material.createWithProperties(variableProperties));
        variableProperties.setVariable("vertex", "Shaders/missing-regression.vsh");
        expect(variableSource.getString("vertexShader") === "Shaders/missing-regression.vsh",
            "factory preserves raw ${variable} expression in the original Properties");
        var failed = false;
        try { cc.Material.createWithProperties(variableProperties); } catch (error) { failed = true; }
        expect(failed, "missing shader is a JavaScript error before native shader loading");
        variableProperties.setVariable("vertex", "Shaders/example_Simple.vsh");
        keep(cc.Material.createWithProperties(variableProperties));
        expect(variableSource.getString("vertexShader") === "Shaders/example_Simple.vsh", "Properties remain reusable after factory error");
        var unknownPath = file(materialText("unknown", oldVertex, oldFragment, "UNRECOGNIZED_MATERIAL_VARIANT 1"), "unknown");
        failed = false;
        try { cc.Material.createWithFilename(unknownPath); } catch (error) { failed = true; }
        expect(failed, "unarchived source/defines pair reports an explicit error");
        expect(cc.Material.createWithFilename(files.getWritablePath() + "missing-regression.material") === null,
            "missing material preserves native null factory semantics");
        failed = false;
        try { cc.Material.createWithProperties({}); } catch (error) { failed = true; }
        expect(failed, "factory rejects a nonnative Properties value");
        failed = false;
        try { effects.getTechniqueByName("blur").getPassByIndex(0).getGLProgramState.call({}); } catch (error) { failed = true; }
        expect(failed, "Pass alias rejects a nonnative receiver");

        var manifest = JSON.parse(files.getStringFromFile("script/legacy-gl/shaders/manifest.json"));
        var nativePath = file(materialText("native", archive(manifest.sources, "Shaders/example_Simple.vsh"),
            archive(manifest.sources, "Shaders/example_Outline.fsh"), ""), "archives");
        var nativeMaterial = keep(cc.Material.createWithFilename(nativePath));
        expect(nativeMaterial.getTechniqueCount() === 1, "modern native shader archives retain their native path");
        sprite(scene, stateFor(nativeMaterial, "sample"), 0.5, 0.25);
        cc.log("MATERIAL_REGRESSION synchronous native factories/properties/clone/validation passed");
    }
    var Scene = cc.Scene.extend({
        onEnter: function () {
            this._super();
            this.scheduleUpdate();
        },
        update: function (dt) {
            if (dt >= 0) state.frames++;
        }
    });
    global.jsMaterialSmokeStart = function () {
        try {
            expect(!state.started, "fixture starts once");
            state.started = true;
            var scene = new Scene();
            assertions(scene);
            cc.director.runScene(scene);
            return 0;
        } catch (error) {
            state.errors.push(String(error) + (error.stack ? "\n" + String(error.stack) : ""));
            cc.log(state.errors[state.errors.length - 1]);
            return 60;
        }
    };
    global.jsMaterialSmokeResult = function () {
        if (state.errors.length) return 60;
        if (!state.started) return 61;
        if (state.frames < 4 || state.blurCallbacks < 3 || state.outlineCallbacks < 3) {
            if (state.frames < 180) return 1;
            cc.log("MATERIAL_REGRESSION draw timeout frames=" + state.frames + " blur=" + state.blurCallbacks +
                " outline=" + state.outlineCallbacks);
            return 62;
        }
        if (!state.completed) {
            state.completed = true;
            state.blur.setUniformCallback("blurRadius", null);
            state.outline.setUniformCallback("u_outlineColor", null);
            for (var i = 0; i < state.materials.length; i++) state.materials[i].release();
            var files = cc.FileUtils.getInstance();
            for (i = 0; i < state.temporaryFiles.length; i++) files.removeFile(state.temporaryFiles[i]);
            cc.log("MATERIAL_REGRESSION PASS native 2D/3D materials, exact variants, clone, Properties preservation, " +
                "modern archives and draw-time callbacks; frames=" + state.frames + " blur=" + state.blurCallbacks +
                " outline=" + state.outlineCallbacks);
        }
        return 0;
    };
}(this));
