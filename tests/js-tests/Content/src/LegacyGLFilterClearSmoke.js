// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Additional real GPU coverage; original OpenGL/Sprite cases remain unchanged.
// Invoke from a real Scheduler callback with a running baseline Scene. Supply
// an independently measured physicalSize, current restorePolicy, and a resource
// path to the adjacent 2x2 sampler-checker.png fixture.
(function(global) {
    var active=false;
    global.runLegacyGLFilterClearGpuSmoke=function(options,completed) {
        if(active)throw Error('A sampler/clear regression is already running');
        if(!options || !options.physicalSize || typeof options.restorePolicy!=='number' ||
            typeof options.checkerPath!=='string' || !cc.director.getRunningScene())
            throw Error('Supply physicalSize, restorePolicy, checkerPath and a running baseline Scene');
        active=true;
        var physical=options.physicalSize,width=physical.width/2,height=physical.height/2;
        var savedDesign=cc.view.getDesignResolutionSize(),savedProjection=cc.director.getProjection();
        var result={passed:false,checks:[],physicalSize:physical,graphicsRequired:true};
        var scene=null,node=null,listener=null,pushed=false,finished=false,texture=null,buffers=[];
        var checkerFrames=0,clearFrames=0,drawCalls=0,phase='checker',drawError=null,switchScheduled=false;
        var spriteTexture=null,oldSampler=null;
        function check(ok,detail,actual,expected) {
            result.checks.push({passed:!!ok,detail:detail,actual:actual,expected:expected});
        }
        function pixel(x,y) {
            var data=new Uint8Array(4);
            gl.readPixels(x,y,1,1,gl.RGBA,gl.UNSIGNED_BYTE,data);
            return Array.prototype.slice.call(data);
        }
        function same(a,b) {return JSON.stringify(a)===JSON.stringify(b);}
        function finish(error) {
            if(finished)return;finished=true;
            if(error)result.error=String(error.stack||error);
            try {
                if(listener)cc.eventManager.removeListener(listener);
                if(scene)scene.unscheduleAllCallbacks();
                gl.bindTexture(gl.TEXTURE_2D,null);gl.bindBuffer(gl.ARRAY_BUFFER,null);
                if(spriteTexture && oldSampler)spriteTexture.setTexParameters.apply(spriteTexture,oldSampler);
                if(pushed)cc.director.popScene();
                cc.view.setDesignResolutionSize(savedDesign.width,savedDesign.height,options.restorePolicy);
                cc.director.setProjection(savedProjection);
                for(var i=0;i<buffers.length;++i)gl.deleteBuffer(buffers[i]);
                if(texture)gl.deleteTexture(texture);
            } catch(cleanupError) {
                result.error=(result.error?result.error+'\n':'')+'cleanup: '+String(cleanupError.stack||cleanupError);
            }
            result.drawCalls=drawCalls;result.clearFrames=clearFrames;
            result.failedChecks=result.checks.filter(function(item){return !item.passed;}).length;
            result.passed=!result.error && !result.failedChecks;
            active=false;global.legacyGLFilterClearGpuSmokeResult=result;
            cc.log('LEGACY_GL_FILTER_CLEAR_GPU_RESULT '+JSON.stringify(result));
            if(typeof completed==='function')completed(result);
        }
        try {
            cc.view.setDesignResolutionSize(width,height,0);
            cc.director.setProjection(0);
            scene=new cc.Scene();
            scene.addChild(new cc.LayerColor(cc.color(0,0,255,255),width,height));
            var program=cc.shaderCache.getProgram('ShaderPositionTexture');
            texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);
            gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,2,2,0,gl.RGBA,gl.UNSIGNED_BYTE,
                new Uint8Array([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255]));
            gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
            gl.bindTexture(gl.TEXTURE_2D,null);
            var positions=gl.createBuffer(),uv=gl.createBuffer();buffers.push(positions,uv);
            gl.bindBuffer(gl.ARRAY_BUFFER,positions);
            gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([
                width*.1,height*.2,width*.45,height*.2,width*.1,height*.8,width*.45,height*.8]),gl.STATIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER,uv);
            gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([0,0,1,0,0,1,1,1]),gl.STATIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER,null);
            spriteTexture=cc.textureCache.addImage(options.checkerPath);
            if(!spriteTexture)throw Error('Native checker texture failed to load');
            gl.bindTexture(gl.TEXTURE_2D,spriteTexture.getName());
            oldSampler=[gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S),gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T)];
            spriteTexture.setTexParameters(cc.NEAREST || gl.NEAREST,gl.NEAREST,gl.REPEAT,gl.REPEAT);
            gl.bindTexture(gl.TEXTURE_2D,null);
            var sprite=new cc.Sprite(spriteTexture,cc.rect(0,0,4,4));
            sprite.setAnchorPoint(0,0);sprite.setPosition(width*.6,height*.25);
            var scale=Math.min(width*.075,height*.1);sprite.setScale(scale);
            scene.addChild(sprite,5);
            node=new cc.GLNode();
            node.draw=function() {
                try {
                    ++drawCalls;
                    if(phase==='clear') {gl.clearColor(0,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT);return;}
                    program.use();program.setUniformsForBuiltins();gl.bindTexture(gl.TEXTURE_2D,texture);
                    gl.disableVertexAttribArray(cc.VERTEX_ATTRIB_COLOR);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_POSITION);gl.bindBuffer(gl.ARRAY_BUFFER,positions);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_POSITION,2,gl.FLOAT,false,0,0);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_TEX_COORDS);gl.bindBuffer(gl.ARRAY_BUFFER,uv);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_TEX_COORDS,2,gl.FLOAT,false,0,0);
                    gl.drawArrays(gl.TRIANGLE_STRIP,0,4);
                    gl.bindTexture(gl.TEXTURE_2D,null);gl.bindBuffer(gl.ARRAY_BUFFER,null);
                } catch(error) {drawError=error;}
            };
            scene.addChild(node,10);
            listener=cc.eventManager.addCustomListener('director_after_draw',function() {
                try {
                    if(drawError)throw drawError;
                    if(phase==='checker') {
                        if(++checkerFrames<3 || switchScheduled)return;
                        switchScheduled=true;
                        var locations=[[.1875,.35],[.3625,.35],[.1875,.65],[.3625,.65]];
                        var expected=[[255,0,0,255],[0,255,0,255],[0,0,255,255],[255,255,255,255]];
                        for(var i=0;i<locations.length;++i) {
                            var actual=pixel(physical.width*locations[i][0],physical.height*locations[i][1]);
                            check(same(actual,expected[i]),'contrasting native GL texels use NEAREST '+i,actual,expected[i]);
                        }
                        var samples=[];
                        for(var column=0;column<4;++column)samples.push(pixel(
                            2*(width*.6+(column+.5)*scale),2*(height*.25+.5*scale)));
                        check(same(samples[0],samples[2]) && same(samples[1],samples[3]) && !same(samples[0],samples[1]),
                            'real native Sprite repeats alternating checker texels across a 4x4 rectangle',samples,
                            'first equals third, second equals fourth, neighboring texels differ');
                        var allowed=[[255,0,0,255],[0,255,0,255],[0,0,255,255],[255,255,255,255]];
                        check(samples.every(function(sample){return allowed.some(function(color){return same(sample,color);});}),
                            'real native Sprite uses exact point sampling',samples,allowed);
                        // Change state in the next native Scheduler phase.
                        scene.scheduleOnce(function(){phase='clear';},0);
                    } else {
                        ++clearFrames;
                        var cleared=pixel(physical.width*.275,physical.height*.5);
                        check(same(cleared,[0,0,0,255]),'ordered color clear overrides earlier blue/checker draws, frame '+clearFrames,
                            cleared,[0,0,0,255]);
                        // Multiple consecutive frames exercise distinct acquired
                        // swapchain images rather than only physical image zero.
                        if(clearFrames>=6)finish();
                    }
                } catch(error) {finish(error);}
            });
            scene.scheduleOnce(function(){finish(Error('Actual sampler/clear render timeout'));},8);
            cc.director.pushScene(scene);pushed=true;
        } catch(error) {finish(error);}
        return result;
    };
})(this);
