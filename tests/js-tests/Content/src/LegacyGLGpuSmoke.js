// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// An additional real-device regression. It does not edit original v3 cases.
// Supply the actual physical swapchain size, independently observed by native
// capture/platform diagnostics, and the app's current resolution policy.
(function(global) {
    var active = false;
    global.runLegacyGLGpuSmoke = function(options, completed) {
        if (active) throw new Error('A legacy GL GPU regression is already running');
        if (!options || !options.physicalSize || !(options.physicalSize.width > 64) ||
                !(options.physicalSize.height > 64) || typeof options.restorePolicy !== 'number')
            throw new Error('Supply actual physicalSize and restorePolicy');
        active = true;
        var result = {passed:false, checks:[], physicalSize:options.physicalSize,
            coordinateContract:'physical framebuffer, lower-left RGBA', graphicsRequired:true};
        var savedDesign = cc.view.getDesignResolutionSize();
        var savedProjection = cc.director.getProjection();
        var physical = options.physicalSize, width = physical.width / 2, height = physical.height / 2;
        var scene = null, listener = null, pushed = false, finished = false;
        var buffers = [], texture = null, oldSampler = null, drawCalls = 0, drawError = null;
        var uniformCalls = 0, threeD = null, threeDState = null;
        function expect(ok, detail) {
            result.checks.push({detail:detail, passed:!!ok});
            if (!ok) throw new Error(detail);
        }
        function observe(ok,detail,actual,expected) {
            result.checks.push({detail:detail,passed:!!ok,actual:actual,expected:expected});
        }
        function samplerValues() {
            return [gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T)];
        }
        function sampler(expected,detail) {
            var actual=samplerValues();
            observe(JSON.stringify(actual)===JSON.stringify(expected),detail,actual,expected);
        }
        function sample(x, y) {
            var value = new Uint8Array(4);
            // Fractional coordinates and dimensions exercise the real old
            // ToNumber -> GLint conversion, independently of generic integer
            // binding argument rules.
            gl.readPixels(x + 0.75, y + 0.75, 1.75, 1.75, gl.RGBA, gl.UNSIGNED_BYTE, value);
            return Array.prototype.slice.call(value);
        }
        function color(x, y, expected, detail) {
            var value = sample(Math.floor(x), Math.floor(y));
            result.checks.push({detail:detail, expected:expected, actual:value,
                passed:JSON.stringify(value) === JSON.stringify(expected)});
        }
        function finish(failure) {
            if (finished) return;
            finished = true;
            if (failure) result.error = String(failure.stack || failure);
            try {
                if (listener) cc.eventManager.removeListener(listener);
                if (scene) scene.unscheduleAllCallbacks();
                if (threeDState) threeDState.setUniformCallback('offset', null);
                gl.bindTexture(gl.TEXTURE_2D, null);
                gl.bindBuffer(gl.ARRAY_BUFFER, null);
                if (texture && oldSampler) texture.setTexParameters.apply(texture, oldSampler);
                if (pushed) cc.director.popScene();
                cc.view.setDesignResolutionSize(savedDesign.width, savedDesign.height, options.restorePolicy);
                cc.director.setProjection(savedProjection);
                for (var i = 0; i < buffers.length; ++i) gl.deleteBuffer(buffers[i]);
            } catch (error) {
                result.error = (result.error || '') + '\ncleanup: ' + String(error.stack || error);
            }
            result.drawCalls = drawCalls;
            result.nativeUniformCallbackCalls = uniformCalls;
            result.failedChecks=result.checks.filter(function(check){return !check.passed;}).length;
            if(result.failedChecks) result.error=(result.error ? result.error+'\n' : '')+
                result.failedChecks+' actual GPU contract checks failed';
            result.passed = !result.error;
            global.legacyGLGpuSmokeResult = result;
            active = false;
            cc.log('LEGACY_GL_GPU_RESULT ' + JSON.stringify(result));
            if (typeof completed === 'function') completed(result);
        }
        try {
            // Force a known 2:1 physical-to-design scale. Sampling design
            // coordinates by resampling the captured image would select other
            // quadrants and fail these independent physical-pixel assertions.
            cc.view.setDesignResolutionSize(width, height, 0); // EXACT_FIT
            cc.director.setProjection(0); // actual native orthographic camera
            scene = new cc.Scene();
            var colors = [[255,0,0],[0,255,0],[0,0,255],[255,255,255]];
            for (var i = 0; i < colors.length; ++i) {
                var rgb = colors[i], layer = new cc.LayerColor(cc.color(rgb[0],rgb[1],rgb[2],255),width/2,height/2);
                layer.setPosition((i % 2) * width/2, Math.floor(i/2) * height/2);
                scene.addChild(layer);
            }
            var program = cc.shaderCache.getProgram('ShaderPositionColor');
            var node = new cc.GLNode();
            var positions = gl.createBuffer(), tint = gl.createBuffer();
            buffers.push(positions, tint);
            gl.bindBuffer(gl.ARRAY_BUFFER, positions);
            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
                width*0.08,height*0.08, width*0.18,height*0.08,
                width*0.08,height*0.18, width*0.18,height*0.18]), gl.STATIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER, tint);
            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1]), gl.STATIC_DRAW);
            node.draw = function() {
                try {
                    ++drawCalls;
                    program.use();
                    program.setUniformsForBuiltins();
                    gl.bindTexture(gl.TEXTURE_2D, null);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_POSITION);
                    gl.bindBuffer(gl.ARRAY_BUFFER, positions);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_POSITION,2,gl.FLOAT,false,0,0);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_COLOR);
                    gl.bindBuffer(gl.ARRAY_BUFFER, tint);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_COLOR,4,gl.FLOAT,false,0,0);
                    gl.drawArrays(gl.TRIANGLE_STRIP,0,4);
                    gl.bindBuffer(gl.ARRAY_BUFFER,null);
                } catch (error) { drawError = error; }
            };
            scene.addChild(node, 10);

            texture = cc.textureCache.addImage('Sprite3DTest/caustics.png');
            expect(texture && texture.getName() === texture, 'Texture2D canonical opaque handle');
            gl.bindTexture(gl.TEXTURE_2D,texture.getName());
            oldSampler = samplerValues();
            var premultiplied=texture.hasPremultipliedAlpha(),textureSize=texture.getContentSize();
            texture.handleLoadedTexture(true);
            expect(texture.hasPremultipliedAlpha()===premultiplied&&texture.getName()===texture&&
                texture.getContentSize().width===textureSize.width&&texture.getContentSize().height===textureSize.height,
                'native handleLoadedTexture preserves the already uploaded image and its real premultiplication');
            texture.setTexParameters(cc.LINEAR,cc.LINEAR,cc.REPEAT,cc.REPEAT);
            gl.bindTexture(gl.TEXTURE_2D,null);gl.bindTexture(gl.TEXTURE_2D,texture.getName());
            sampler([gl.LINEAR,gl.LINEAR,gl.REPEAT,gl.REPEAT],
                'unchanged original Sprite53 cc.* sampler parameters reach the native sampler');
            texture.setTexParameters({minFilter:gl.NEAREST,magFilter:gl.LINEAR,
                wrapS:gl.CLAMP_TO_EDGE,wrapT:gl.MIRRORED_REPEAT});
            gl.bindTexture(gl.TEXTURE_2D,null);gl.bindTexture(gl.TEXTURE_2D,texture.getName());
            sampler([gl.NEAREST,gl.LINEAR,gl.CLAMP_TO_EDGE,gl.MIRRORED_REPEAT],
                'legacy object-form texture parameters reach the native sampler');
            texture.setTexParameters(gl.NEAREST,gl.LINEAR,gl.MIRRORED_REPEAT,gl.REPEAT);
            gl.bindTexture(gl.TEXTURE_2D,null);
            gl.bindTexture(gl.TEXTURE_2D,texture.getName());
            sampler([gl.NEAREST,gl.LINEAR,gl.MIRRORED_REPEAT,gl.REPEAT],
                'bindTexture preserves the actual native sampler after native setter');
            gl.bindTexture(gl.TEXTURE_2D,null);

            // The unchanged original cylinder model exercises native Mesh/VIB
            // and two actual texture bindings, without introducing model data.
            threeD = new jsb.Sprite3D('Sprite3DTest/cylinder.c3b');
            var shader = new cc.GLProgram('Sprite3DTest/cylinder.vert','Sprite3DTest/cylinder.frag');
            threeDState = cc.GLProgramState.create(shader);
            var mesh = threeD.getMesh(), offset = 0, count = mesh.getMeshVertexAttribCount();
            expect(count > 0, 'native imported cylinder vertex declaration');
            for (var index = 0; index < count; ++index) {
                var attribute = mesh.getMeshVertexAttribute(index);
                expect(attribute.size > 0 && attribute.attribSizeBytes > 0,
                    'native mesh attribute ' + index);
                threeDState.setVertexAttribPointer(cc.attributeNames[attribute.vertexAttrib],
                    attribute.size,attribute.type,gl.FALSE,mesh.getVertexSizeInBytes(),offset);
                offset += attribute.attribSizeBytes;
            }
            expect(offset === mesh.getVertexSizeInBytes(), 'real mesh stride agrees with its native attributes');
            threeDState.setUniformFloat('duration',0.375);
            threeDState.setUniformTexture('caustics',texture);
            threeDState.setUniformCallback('offset',function(program,description) {
                ++uniformCalls;
                program.setUniformLocationWith1f(description.location,0.125);
            });
            threeD.setGLProgramState(threeDState);
            threeD.setPosition(width*0.5,height*0.5);
            // Keep the real mesh near the center so it does not cover any of
            // the independent quadrant or imperative-draw sample points.
            threeD.setScale(0.1);
            scene.addChild(threeD);
            var frames = 0;
            listener = cc.eventManager.addCustomListener('director_after_draw',function() {
                try {
                    if (++frames < 3) return;
                    if(drawError) observe(false,'GLNode native drawing error',String(drawError.stack||drawError),null);
                    observe(drawCalls >= 3, 'GLNode override invoked by real native render traversal',drawCalls,'>=3');
                    color(physical.width*0.25,physical.height*0.25,[255,0,0,255],'physical lower-left red');
                    color(physical.width*0.75,physical.height*0.25,[0,255,0,255],'physical lower-right green');
                    color(physical.width*0.25,physical.height*0.75,[0,0,255,255],'physical upper-left blue');
                    color(physical.width*0.75,physical.height*0.75,[255,255,255,255],'physical upper-right white');
                    color(physical.width*0.125,physical.height*0.125,[255,0,255,255],
                        'actual imperative GLNode draw produced magenta pixels');
                    observe(uniformCalls >= 3, 'native 3D draw applied the JS ProgramState uniform callback',uniformCalls,'>=3');
                    var handle = threeD.getShaderProgram();
                    var actualOffset=gl.getUniform(handle,handle.getUniformLocationForName('offset'));
                    observe(actualOffset === 0.125,'uniform callback wrote the real native reflected buffer',actualOffset,0.125);
                    var actualDuration=gl.getUniform(handle,handle.getUniformLocationForName('duration'));
                    observe(actualDuration === 0.375,'typed uniform setter retained the independent native uniform',actualDuration,0.375);
                    finish();
                } catch (error) { finish(error); }
            });
            scene.scheduleOnce(function() { finish(new Error('Native render callback timeout')); },5);
            cc.director.pushScene(scene);
            pushed = true;
        } catch (error) { finish(error); }
        return result;
    };
})(this);
