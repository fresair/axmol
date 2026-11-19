// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Supplemental GPU regression. The imported v3 Label tests stay unchanged.
(function(global) {
    var active=false;
    global.runLegacyAtlasSamplerGpuSmoke=function(options,completed) {
        if(active)throw Error('An atlas sampler regression is already running');
        if(!options || !options.physicalSize || typeof options.restorePolicy!=='number' ||
            typeof options.checkerPath!=='string' || typeof options.replacementCheckerPath!=='string' || !cc.director.getRunningScene())
            throw Error('Supply physicalSize, restorePolicy, both checker paths and a running baseline Scene');
        active=true;
        var physical=options.physicalSize,savedDesign=cc.view.getDesignResolutionSize();
        var savedProjection=cc.director.getProjection(),scene=null,listener=null,texture=null,oldSampler=null;
        var replacementTexture=null,replacementOldSampler=null,offsceneControl=null,selectedState=null,offsceneRetained=false;
        var pushed=false,finished=false,stage=-1,frames=0,stageFrames=0,transition=false,models=[];
        var stageReadback=null,readbackCalls=0;
        var phases=['alias','anti-alias','alias-again'],background=[17,23,31,255];
        // Fixture PNG rows from top: red/green, blue/white. LabelAtlas maps
        // top vertices to v=0, bottom vertices to v=1; readPixels is bottom-left.
        var source=[[[255,0,0,255],[0,255,0,255]],[[0,0,255,255],[255,255,255,255]]];
        var replacementSource=[[[0,255,255,255],[255,0,255,255]],[[255,255,0,255],[0,0,0,255]]];
        var result={passed:false,checks:[],snapshots:[],physicalSize:physical,graphicsRequired:true,
            sourceRowsTopFirst:source,replacementSourceRowsTopFirst:replacementSource,scale:16,itemSize:2,glyph:'A',firstGlyph:'A',
            scope:'LabelAtlas real AtlasNode rendering with original/replacement textures; off-scene explicit legacy ProgramState identity',
            notCovered:['custom shader visual rendering'],
            oracle:'physical pixel centers; UV=(pixelCenter-origin)/32, PNG v=1-y/32; point exact, bilinear rounded with <=2 byte quantization error'};
        function check(ok,detail,actual,expected) {
            result.checks.push({passed:!!ok,detail:detail,actual:actual,expected:expected});
        }
        function same(a,b) {return JSON.stringify(a)===JSON.stringify(b);}
        function captureStage() {
            var left=physical.width,bottom=physical.height,right=0,top=0;
            models.forEach(function(model) {
                left=Math.min(left,Math.floor(model.x)-1);bottom=Math.min(bottom,Math.floor(model.y)-1);
                right=Math.max(right,Math.floor(model.x)+33);top=Math.max(top,Math.floor(model.y)+33);
            });
            // Include both endpoint sample pixels. GL returns tightly packed
            // RGBA rows beginning at the rectangle's bottom-left corner.
            var rect={x:left,y:bottom,width:right-left+1,height:top-bottom+1};
            if(left<0||bottom<0||right>=physical.width||top>=physical.height)
                throw Error('Atlas readback rectangle exceeds the physical framebuffer');
            stageReadback={rect:rect,data:new Uint8Array(rect.width*rect.height*4)};
            gl.readPixels(rect.x,rect.y,rect.width,rect.height,gl.RGBA,gl.UNSIGNED_BYTE,stageReadback.data);
            ++readbackCalls;
            result.snapshots.push({event:'single-stage-readback',phase:phases[stage],frame:frames,
                rectangle:rect,byteLength:stageReadback.data.length,rowOrder:'bottom-to-top',format:'RGBA8'});
        }
        function pixel(x,y) {
            var rect=stageReadback.rect;
            if(x<rect.x||y<rect.y||x>=rect.x+rect.width||y>=rect.y+rect.height)
                throw Error('Requested atlas sample is outside the captured rectangle');
            var offset=((y-rect.y)*rect.width+x-rect.x)*4,data=stageReadback.data;
            return [data[offset],data[offset+1],data[offset+2],data[offset+3]];
        }
        function sampler(target) {
            gl.bindTexture(gl.TEXTURE_2D,(target||texture).getName());
            var values=[gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S),gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T)];
            gl.bindTexture(gl.TEXTURE_2D,null);return values;
        }
        function expectedAt(model,x,y,linear) {
            var source=model.palette,u=(x+.5-model.x)/32,v=1-(y+.5-model.y)/32;
            if(!linear)return source[Math.min(1,Math.max(0,Math.floor(v*2)))][Math.min(1,Math.max(0,Math.floor(u*2)))].slice();
            var tx=Math.max(0,Math.min(1,u*2-.5)),ty=Math.max(0,Math.min(1,v*2-.5));
            return source[0][0].map(function(unused,c) {
                return Math.round((source[0][0][c]*(1-tx)+source[0][1][c]*tx)*(1-ty)+
                    (source[1][0][c]*(1-tx)+source[1][1][c]*tx)*ty);
            });
        }
        function finish(error) {
            if(finished)return;finished=true;
            if(error)result.error=String(error.stack||error);
            try {
                if(listener)cc.eventManager.removeListener(listener);
                if(scene)scene.unscheduleAllCallbacks();
                // Attempt both shared sampler restorations before scene pop,
                // even if one native texture reports a restoration failure.
                [{name:'original',texture:texture,saved:oldSampler},
                 {name:'replacement',texture:replacementTexture,saved:replacementOldSampler}].forEach(function(item) {
                    if(!item.texture||!item.saved)return;
                    try {
                        item.texture.setTexParameters.apply(item.texture,item.saved);
                        var restored=sampler(item.texture);
                        if(item.name==='original')result.restoredSampler=restored;
                        else result.replacementRestoredSampler=restored;
                        check(same(restored,item.saved),item.name+' native texture sampler restored before scene pop',restored,item.saved);
                    } catch(restoreError) {
                        result.error=(result.error?result.error+'\n':'')+item.name+' sampler restore: '+String(restoreError.stack||restoreError);
                    }
                });
                gl.bindTexture(gl.TEXTURE_2D,null);
                if(offsceneRetained) {offsceneControl.release();offsceneRetained=false;}
                if(pushed)cc.director.popScene();
                cc.view.setDesignResolutionSize(savedDesign.width,savedDesign.height,options.restorePolicy);
                cc.director.setProjection(savedProjection);
            } catch(cleanupError) {
                result.error=(result.error?result.error+'\n':'')+'cleanup: '+String(cleanupError.stack||cleanupError);
            }
            stageReadback=null;result.readbackCalls=readbackCalls;
            result.drawnFrames=frames;result.failedChecks=result.checks.filter(function(item){return !item.passed;}).length;
            result.passed=!result.error&&!result.failedChecks;
            active=false;global.legacyAtlasSamplerGpuSmokeResult=result;
            // GpuCoverageSmoke persists complete facts; never print this object.
            if(typeof completed==='function')completed(result);
        }
        function beginStage(index) {
            stage=index;stageFrames=0;transition=false;
            [texture,replacementTexture].forEach(function(target) {
                if(index===1)target.setAntiAliasTexParameters();else target.setAliasTexParameters();
            });
            result.snapshots.push({event:'sampler-change-in-Scheduler',phase:phases[index],frame:frames,
                sampler:sampler(),replacementSampler:sampler(replacementTexture)});
            check(offsceneControl.getGLProgramState()===selectedState,
                'off-scene user-selected native ProgramState survives '+phases[index]+' sampler update',
                offsceneControl.getGLProgramState()===selectedState,true);
        }
        function observe(model) {
            var linear=stage===1,snapshot={phase:phases[stage],kind:model.kind,frame:frames,
                stageDrawnFrames:stageFrames,origin:[model.x,model.y],sourceRowsTopFirst:model.palette,boundary:[],quadrants:[],outside:[]};
            var exact=true,mixed=true,maxError=0;
            // Four by four consecutive interior pixels straddle both texel
            // boundaries. Origin .25 makes these unambiguous point samples.
            for(var row=14;row<18;++row)for(var col=14;col<18;++col) {
                var x=Math.floor(model.x)+col,y=Math.floor(model.y)+row;
                var actual=pixel(x,y),expected=expectedAt(model,x,y,linear);
                snapshot.boundary.push({xy:[x,y],actual:actual,expected:expected});
                exact=exact&&same(actual,expected);
                mixed=mixed&&actual.slice(0,3).every(function(c){return c>0&&c<255;});
                for(var c=0;c<4;++c)maxError=Math.max(maxError,Math.abs(actual[c]-expected[c]));
            }
            check(linear?maxError<=2:exact,model.kind+' '+phases[stage]+' 4x4 boundary follows requested real sampler',
                {maxChannelError:maxError,exact:exact},linear?'bilinear error <=2':'all 16 RGBA values exact');
            if(linear)check(mixed,model.kind+' anti-alias produces actual RGB mixtures at all 16 boundary pixels',mixed,true);
            var quadrantsOK=true;
            [[4,4],[27,4],[4,27],[27,27]].forEach(function(offset) {
                var x=Math.floor(model.x)+offset[0],y=Math.floor(model.y)+offset[1];
                var actual=pixel(x,y),expected=expectedAt(model,x,y,false);
                snapshot.quadrants.push({xy:[x,y],actual:actual,expected:expected});
                quadrantsOK=quadrantsOK&&same(actual,expected);
            });
            check(quadrantsOK,model.kind+' '+phases[stage]+' four fixture quadrants preserve orientation and scale',quadrantsOK,true);
            var outsideOK=true;
            [[-1,16],[33,16],[16,-1],[16,33]].forEach(function(offset) {
                var x=Math.floor(model.x)+offset[0],y=Math.floor(model.y)+offset[1],actual=pixel(x,y);
                snapshot.outside.push({xy:[x,y],actual:actual,expected:background});
                outsideOK=outsideOK&&same(actual,background);
            });
            check(outsideOK,model.kind+' '+phases[stage]+' geometry remains within its 32x32 glyph',outsideOK,true);
            result.snapshots.push(snapshot);
        }
        try {
            if(physical.width<200 || physical.height<200)throw Error('Probe needs a centered 200x200 physical region');
            cc.view.setDesignResolutionSize(physical.width,physical.height,0);
            cc.director.setProjection(0);
            scene=new cc.Scene();scene.addChild(new cc.LayerColor(cc.color.apply(cc,background),physical.width,physical.height));
            texture=cc.textureCache.addImage(options.checkerPath);
            if(!texture)throw Error('Native opaque 2x2 checker texture did not load');
            oldSampler=sampler();result.originalSampler=oldSampler.slice();
            replacementTexture=cc.textureCache.addImage(options.replacementCheckerPath);
            if(!replacementTexture)throw Error('Distinct native 2x2 replacement checker texture did not load');
            replacementOldSampler=sampler(replacementTexture);result.replacementOriginalSampler=replacementOldSampler.slice();
            check(replacementTexture!==texture,'replacement fixture has a distinct native Texture identity',replacementTexture!==texture,true);
            var Derived=cc.LabelAtlas.extend({ctor:function() {
                return this.initWithString('A',options.checkerPath,2,2,65);
            }});
            function add(kind,node,index) {
                if(!node)throw Error(kind+' did not create a native LabelAtlas');
                var x=Math.floor(physical.width/2)+(index%2===0?-64:32)+.25;
                var y=Math.floor(physical.height/2)+(index<2?-64:32)+.25;
                node.setAnchorPoint(0,0);node.setPosition(x,y);node.setScale(16);scene.addChild(node);
                var size=node.getContentSize();
                check(size.width===2&&size.height===2,kind+' has real 2x2 content size',size,{width:2,height:2});
                check(node.getTexture()===texture,kind+' references the same native checker texture',node.getTexture()===texture,true);
                models.push({kind:kind,node:node,x:x,y:y,palette:source});return node;
            }
            add('constructor',new cc.LabelAtlas('A',options.checkerPath,2,2,'A'),0);
            add('create',cc.LabelAtlas.create('A',options.checkerPath,2,2,'A'),1);
            add('extend-initWithString',new Derived(),2);
            var replaced=add('setTexture-distinct-palette',new cc.LabelAtlas('A',options.checkerPath,2,2,'A'),3);
            replaced.setTexture(replacementTexture);models[3].palette=replacementSource;
            check(replaced.getTexture()===replacementTexture,'setTexture binds the actual replacement native Texture',
                replaced.getTexture()===replacementTexture,true);
            // This initialized LabelAtlas is intentionally never added to the
            // scene. Verify only native state identity, without a custom draw
            // or shader compilation/archive dependency.
            if(!cc.shaderCache||typeof cc.shaderCache.getProgram!=='function'||!cc.GLProgramState||
                typeof cc.GLProgramState.create!=='function'||typeof replaced.setGLProgramState!=='function'||
                typeof replaced.getGLProgramState!=='function')
                throw Error('Unsupported API: explicit legacy ProgramState identity probe');
            var selectedProgram=cc.shaderCache.getProgram('ShaderPositionTextureColor');
            if(!selectedProgram)throw Error('Unsupported API: legacy ShaderPositionTextureColor program unavailable');
            selectedState=cc.GLProgramState.create(selectedProgram);
            if(!selectedState)throw Error('Unsupported API: native legacy ProgramState could not be created');
            offsceneControl=new cc.LabelAtlas('A',options.checkerPath,2,2,'A');
            // Constructor autoreleases once; the off-scene control needs an
            // explicit native lease across Scheduler frames.
            offsceneControl.retain();offsceneRetained=true;
            offsceneControl.setGLProgramState(selectedState);
            check(offsceneControl.getGLProgramState()===selectedState,'off-scene user-selected native ProgramState installed before setTexture',
                offsceneControl.getGLProgramState()===selectedState,true);
            offsceneControl.setTexture(replacementTexture);
            check(offsceneControl.getTexture()===replacementTexture,'off-scene state probe binds the actual replacement native Texture',
                offsceneControl.getTexture()===replacementTexture,true);
            check(offsceneControl.getGLProgramState()===selectedState,'off-scene setTexture preserves the user-selected native ProgramState identity',
                offsceneControl.getGLProgramState()===selectedState,true);
            listener=cc.eventManager.addCustomListener('director_after_draw',function() {
                try {
                    ++frames;if(stage<0||transition)return;
                    if(++stageFrames<3)return;
                    transition=true;
                    var expectedFilter=stage===1?gl.LINEAR:gl.NEAREST;
                    [{name:'original',texture:texture},{name:'replacement',texture:replacementTexture}].forEach(function(item) {
                        var actualSampler=sampler(item.texture);
                        check(actualSampler[0]===expectedFilter&&actualSampler[1]===expectedFilter,
                            phases[stage]+' '+item.name+' native texture min/mag match requested mode',actualSampler,[expectedFilter,expectedFilter,'preserved wraps']);
                    });
                    captureStage();
                    models.forEach(observe);
                    stageReadback=null;
                    if(stage===2)finish();
                    else scene.scheduleOnce(function(){beginStage(stage+1);},0);
                } catch(error) {finish(error);}
            });
            scene.scheduleOnce(function(){beginStage(0);},0);
            scene.scheduleOnce(function(){finish(Error('Atlas sampler GPU probe timed out'));},10);
            cc.director.pushScene(scene);pushed=true;
        } catch(error) {finish(error);}
        return result;
    };
})(this);
