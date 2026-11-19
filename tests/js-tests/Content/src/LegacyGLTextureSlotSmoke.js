// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Additional real GPU regression for the native built-in Texture program.
// Invoke from an actual Scheduler callback with a running baseline Scene.
(function(global) {
    var active=false;
    global.runLegacyGLTextureSlotGpuSmoke=function(options,completed) {
        if(active)throw new Error('A texture-slot GPU regression is already running');
        if(!options||!options.physicalSize||!(options.physicalSize.width>64)||
            !(options.physicalSize.height>64)||typeof options.restorePolicy!=='number')
            throw new Error('Supply actual physicalSize and restorePolicy');
        active=true;
        var physical=options.physicalSize,width=physical.width/2,height=physical.height/2;
        var result={passed:false,checks:[],physicalSize:physical,graphicsRequired:true,
            expectedGlSlots:{position:0,texCoord:2},nativeProgram:'ShaderPositionTexture'};
        var savedDesign=cc.view.getDesignResolutionSize(),savedProjection=cc.director.getProjection();
        var scene=null,node=null,listener=null,texture=null,buffers=[],pushed=false,finished=false;
        var drawCalls=0,drawError=null;
        function check(value,detail,actual,expected) {
            result.checks.push({passed:!!value,detail:detail,actual:actual,expected:expected});
        }
        function finish(error) {
            if(finished)return;finished=true;
            if(error)result.error=String(error.stack||error);
            try {
                if(listener)cc.eventManager.removeListener(listener);
                if(scene)scene.unscheduleAllCallbacks();
                gl.bindTexture(gl.TEXTURE_2D,null);gl.bindBuffer(gl.ARRAY_BUFFER,null);
                if(pushed)cc.director.popScene();
                cc.view.setDesignResolutionSize(savedDesign.width,savedDesign.height,options.restorePolicy);
                cc.director.setProjection(savedProjection);
                for(var i=0;i<buffers.length;++i)gl.deleteBuffer(buffers[i]);
                if(texture)gl.deleteTexture(texture);
            } catch(cleanupError) {
                result.error=(result.error?result.error+'\n':'')+'cleanup: '+String(cleanupError.stack||cleanupError);
            }
            result.drawCalls=drawCalls;
            result.failedChecks=result.checks.filter(function(item){return !item.passed;}).length;
            result.passed=!result.error&&!result.failedChecks;
            active=false;global.legacyGLTextureSlotGpuSmokeResult=result;
            cc.log('LEGACY_GL_TEXTURE_SLOT_GPU_RESULT '+JSON.stringify(result));
            if(typeof completed==='function')completed(result);
        }
        try {
            if(!cc.director.getRunningScene())throw new Error('An actual running baseline Scene is required');
            cc.view.setDesignResolutionSize(width,height,0);
            cc.director.setProjection(0);
            scene=new cc.Scene();
            scene.addChild(new cc.LayerColor(cc.color(0,0,0,255),width,height));
            var program=cc.shaderCache.getProgram('ShaderPositionTexture'),handle=program.getProgram();
            var positionSlot=gl.getAttribLocation(handle,'a_position');
            var textureSlot=gl.getAttribLocation(handle,'a_texCoord');
            check(positionSlot===0,'reflected POSITION exposes the original public GL slot',positionSlot,0);
            check(textureSlot===2,'reflected TEXCOORD0 exposes old GL slot 2 while RHI keeps its own location',textureSlot,2);
            texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);
            gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,2,2,0,gl.RGBA,gl.UNSIGNED_BYTE,
                new Uint8Array([0,255,255,255,0,255,255,255,0,255,255,255,0,255,255,255]));
            gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
            gl.bindTexture(gl.TEXTURE_2D,null);
            var positions=gl.createBuffer(),uv=gl.createBuffer();buffers.push(positions,uv);
            gl.bindBuffer(gl.ARRAY_BUFFER,positions);
            gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([
                width*.3,height*.3,width*.7,height*.3,width*.3,height*.7,width*.7,height*.7]),gl.STATIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER,uv);
            gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([0,0,1,0,0,1,1,1]),gl.STATIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER,null);
            node=new cc.GLNode();
            node.draw=function() {
                try {
                    ++drawCalls;program.use();program.setUniformsForBuiltins();
                    gl.bindTexture(gl.TEXTURE_2D,texture);
                    gl.disableVertexAttribArray(cc.VERTEX_ATTRIB_COLOR);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_POSITION);
                    gl.bindBuffer(gl.ARRAY_BUFFER,positions);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_POSITION,2,gl.FLOAT,false,0,0);
                    gl.enableVertexAttribArray(cc.VERTEX_ATTRIB_TEX_COORDS);
                    gl.bindBuffer(gl.ARRAY_BUFFER,uv);
                    gl.vertexAttribPointer(cc.VERTEX_ATTRIB_TEX_COORDS,2,gl.FLOAT,false,0,0);
                    gl.drawArrays(gl.TRIANGLE_STRIP,0,4);
                    gl.bindBuffer(gl.ARRAY_BUFFER,null);gl.bindTexture(gl.TEXTURE_2D,null);
                } catch(error) { drawError=error; }
            };
            scene.addChild(node,10);
            var frames=0;
            listener=cc.eventManager.addCustomListener('director_after_draw',function() {
                try {
                    if(++frames<3)return;
                    check(!drawError,'actual native GLNode draw has no argument/layout error',
                        drawError?String(drawError.stack||drawError):null,null);
                    check(drawCalls>=3,'actual native traversal submitted textured draws',drawCalls,'>=3');
                    var pixels=new Uint8Array(4);
                    gl.readPixels(physical.width*.5+.75,physical.height*.5+.75,1.75,1.75,
                        gl.RGBA,gl.UNSIGNED_BYTE,pixels);
                    var actual=Array.prototype.slice.call(pixels),expected=[0,255,255,255];
                    check(JSON.stringify(actual)===JSON.stringify(expected),
                        'actual uploaded cyan texture rendered through legacy slots and native RHI layout',actual,expected);
                    finish();
                } catch(error) { finish(error); }
            });
            scene.scheduleOnce(function(){finish(new Error('Actual textured draw timeout'));},5);
            cc.director.pushScene(scene);pushed=true;
        } catch(error) { finish(error); }
        return result;
    };
})(this);
