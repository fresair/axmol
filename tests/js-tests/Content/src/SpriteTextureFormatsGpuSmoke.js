// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Additional GPU checks. Original Sprite AnimationCache assertions stay intact.
(function(global) {
    var active=false;
    global.runLegacySpriteTextureFormatsGpuSmoke=function(options,completed) {
        if(active)throw Error('Sprite texture-format probe is already running');
        active=true;
        var result={passed:false,checks:[],snapshots:[],drawnFrames:0,readbackCalls:0,nativeAnimateCompletions:0,
            scope:'Three initial PNG formats; native Animate, explicit texture/frame, AtlasNode, cloned state and live-node cache-purge transitions',
            notCovered:['graphics-device teardown/reload','C++/Lua graphics (native builtin identity only)'],
            oracle:'2x2 PNG numeric texels, real texture PMA flag, physical bottom-left pixel centers; max error 3 bytes',
            renderPolicy:{rawLanes:'ONE,ZERO and opacityModifyRGB=false',
                defaultLane:'untouched native blend/opacityModifyRGB; source-over on opaque [17,23,31] background'}};
        var physical=options.physicalSize,savedDesign=cc.view.getDesignResolutionSize();
        var projection=cc.director.getProjection(),scene=null,listener=null,pushed=false,finished=false;
        var textures={},frames={},savedSamplers={},models=[],controls=[],nativeReferences=[],nativeTypes={},modern=null,familyPrograms={};
        var plans=[],stage=-1,ready=false,draws=0;
        var sequence=['rgba','l','la','rgba','la','l','rgba'];
        var palettes={l:[[[15,15,15,255],[80,80,80,255]],[[160,160,160,255],[240,240,240,255]]],
            la:[[[15,15,15,255],[80,80,80,128]],[[160,160,160,0],[240,240,240,255]]],
            rgba:[[[30,80,170,255],[210,60,35,128]],[[50,190,90,0],[230,150,60,255]]]};
        function check(ok,detail,actual,expected) {
            result.checks.push({passed:!!ok,detail:detail,actual:actual,expected:expected});
        }
        function sampler(texture) {
            gl.bindTexture(gl.TEXTURE_2D,texture.getName());
            var values=[gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER),
                gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S),gl.getTexParameter(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T)];
            gl.bindTexture(gl.TEXTURE_2D,null);return values;
        }
        function style(model) {
            if(!model.defaultBlend)model.node.setOpacityModifyRGB(false);
            model.node.setColor(cc.color(model.tint[0],model.tint[1],model.tint[2]));
            model.node.setOpacity(model.opacity);
            if(!model.defaultBlend)model.node.setBlendFunc({src:1,dst:0});
        }
        function finish(error) {
            if(finished)return;finished=true;
            if(error)result.error=String(error.stack||error);
            if(!error) {
                check(result.readbackCalls===18,'all 18 after_draw stages have exactly one readPixels call',result.readbackCalls,18);
                check(result.nativeAnimateCompletions===17,'17 real native Animate actions reached their native CallFunc',
                    result.nativeAnimateCompletions,17);
            }
            try {
                if(listener)cc.eventManager.removeListener(listener);
                if(scene)scene.unscheduleAllCallbacks();
                models.forEach(function(model){model.node.stopAllActions();});
                Object.keys(textures).forEach(function(name){
                    textures[name].setTexParameters.apply(textures[name],savedSamplers[name]);
                    check(JSON.stringify(sampler(textures[name]))===JSON.stringify(savedSamplers[name]),
                        name+' original native sampler restored',sampler(textures[name]),savedSamplers[name]);
                });
                controls.forEach(function(node){node.release();});
                nativeReferences.forEach(function(node){node.release();});
                Object.keys(frames).forEach(function(name){frames[name].release();});
                if(pushed)cc.director.popScene();
                cc.view.setDesignResolutionSize(savedDesign.width,savedDesign.height,options.restorePolicy);
                cc.director.setProjection(projection);
            } catch(e) {result.error=(result.error||'')+' cleanup: '+String(e.stack||e);}
            result.failedChecks=result.checks.filter(function(item){return !item.passed;}).length;
            result.passed=!result.error&&!result.failedChecks;active=false;
            global.legacySpriteTextureFormatsGpuSmokeResult=result;
            completed(result);
        }
        function add(kind,node,format,index,tinted,defaultBlend) {
            var model={kind:kind,node:node,format:format,initialFormat:format,
                x:Math.floor(physical.width/2)-136+index*48+.25,y:Math.floor(physical.height/2)+.25,
                tint:tinted?[128,192,224]:[255,255,255],opacity:tinted?160:255,defaultBlend:!!defaultBlend};
            node.setAnchorPoint(0,0);node.setPosition(model.x,model.y);node.setScale(16);
            scene.addChild(node);models.push(model);style(model);return model;
        }
        function texels(format) {
            var pma=textures[format].hasPremultipliedAlpha();
            return palettes[format].map(function(row){return row.map(function(p){
                return [0,1,2].map(function(c){return pma?Math.floor(p[c]*(p[3]+1)/256):p[c];}).concat([p[3]]);
            });});
        }
        function expected(model,x,y,linear) {
            var source=texels(model.format),u=(x+.5-model.x)/32,v=1-(y+.5-model.y)/32,value;
            if(!linear)value=source[Math.min(1,Math.max(0,Math.floor(v*2)))][Math.min(1,Math.max(0,Math.floor(u*2)))];
            else {
                var tx=Math.max(0,Math.min(1,u*2-.5)),ty=Math.max(0,Math.min(1,v*2-.5));
                value=[0,1,2,3].map(function(c){return (source[0][0][c]*(1-tx)+source[0][1][c]*tx)*(1-ty)+
                    (source[1][0][c]*(1-tx)+source[1][1][c]*tx)*ty;});
            }
            if(!model.defaultBlend)
                return value.map(function(c,index){return Math.round(c*(index===3?model.opacity:model.tint[index])/255);});
            // Sprite's native Color32 conversion rounds the premultiplied vertex
            // tint to bytes. Texture PMA itself used floor(c*(a+1)/256) above.
            var pma=textures[model.format].hasPremultipliedAlpha(),alpha=value[3]*model.opacity/255;
            var background=[17,23,31],out=[];
            for(var channel=0;channel<3;++channel) {
                var vertex=pma?Math.round(model.tint[channel]*model.opacity/255):model.tint[channel];
                var source=value[channel]*vertex/255;
                out.push(Math.round(source*(pma?1:alpha/255)+background[channel]*(1-alpha/255)));
            }
            // Native TrianglesCommand applies the same factors to alpha: ONE
            // for PMA, SRC_ALPHA otherwise (not a separately assumed alpha rule).
            out.push(Math.round(alpha*(pma?1:alpha/255)+255-alpha));
            return out;
        }
        function observe() {
            var plan=plans[stage],x=Math.floor(models[0].x)-1,y=Math.floor(models[0].y)-1;
            var rect={x:x,y:y,width:models.length*48-12,height:35},data=new Uint8Array(rect.width*rect.height*4);
            gl.readPixels(x,y,rect.width,rect.height,gl.RGBA,gl.UNSIGNED_BYTE,data);++result.readbackCalls;
            var snapshot={stage:stage,label:plan.label,linear:plan.linear,postCachePurge:!!plan.postPurge,frame:result.drawnFrames,
                rectangle:rect,byteLength:data.length,rowOrder:'bottom-to-top',models:[],nativeAnimateCompleted:ready,
                rawRectanglePersisted:false,scope:'Every checked sample RGBA is persisted below'};
            models.forEach(function(model) {
                var actualProgram=model.node.getGLProgramState().getProgram();
                var facts={kind:model.kind,format:model.format,previousFormat:model.previousFormat||null,origin:[model.x,model.y],
                    pixelFormat:model.node.getTexture().getPixelFormat(),pma:model.node.getTexture().hasPremultipliedAlpha(),
                    programType:actualProgram.getProgramType(),familyProgramMatches:actualProgram===familyPrograms[model.format],
                    tint:model.node.getColor(),opacity:model.node.getOpacity(),defaultBlend:model.defaultBlend,
                    actualBlend:model.node.getBlendFunc(),opacityModifyRGB:model.node.isOpacityModifyRGB(),samples:[],maxError:0,
                    sampler:sampler(model.node.getTexture())};
                check(facts.familyProgramMatches,plan.label+' '+model.kind+' actual family Program identity',facts.familyProgramMatches,true);
                check(facts.opacityModifyRGB===(model.defaultBlend?facts.pma:false),
                    plan.label+' '+model.kind+' native opacityModifyRGB policy',facts.opacityModifyRGB,model.defaultBlend?facts.pma:false);
                check(model.node.getTexture()===textures[model.format],plan.label+' '+model.kind+' real native texture',
                    model.node.getTexture()===textures[model.format],true);
                check(facts.sampler[0]===(plan.linear?gl.LINEAR:gl.NEAREST)&&facts.sampler[1]===facts.sampler[0],
                    plan.label+' '+model.kind+' real sampler',facts.sampler,plan.linear?'LINEAR':'NEAREST');
                var offsets=[[4,4],[27,4],[4,27],[27,27]];
                if(!model.defaultBlend)
                    for(var row=14;row<18;++row)for(var col=14;col<18;++col)offsets.push([col,row]);
                offsets.forEach(function(offset) {
                    var px=Math.floor(model.x)+offset[0],py=Math.floor(model.y)+offset[1];
                    var at=((py-y)*rect.width+px-x)*4,actual=[data[at],data[at+1],data[at+2],data[at+3]];
                    var want=expected(model,px,py,plan.linear),error=0;
                    actual.forEach(function(c,i){error=Math.max(error,Math.abs(c-want[i]));});
                    facts.maxError=Math.max(facts.maxError,error);facts.samples.push({xy:[px,py],actual:actual,expected:want,maxError:error});
                });
                check(facts.maxError<=3,plan.label+' '+model.kind+' actual GPU format/tint/alpha pixels',facts.maxError,'<=3 bytes');
                snapshot.models.push(facts);
            });
            controls.forEach(function(node,index){check(node.getGLProgramState()===node.__formatExpectedState,
                plan.label+' offscene '+(index===0?'unknown custom':'autoUpdatePS=false')+' native state preserved',
                node.getGLProgramState()===node.__formatExpectedState,true);});
            var nativeFormat=plan.initial?'rgba':plan.format;
            var nativeType=gpuSpriteProgramType(modern);
            check(nativeType===nativeTypes[nativeFormat],plan.label+' native-created modern Sprite keeps its builtin format policy',
                nativeType,nativeTypes[nativeFormat]);
            snapshot.nativeControl={format:nativeFormat,actualProgramType:nativeType,expectedBuiltinProgramType:nativeTypes[nativeFormat]};
            result.snapshots.push(snapshot);
        }
        function begin(index) {
            stage=index;draws=0;ready=false;
            var plan=plans[index];
            if(plan.purge) {
                // Every displayed node is still on the old RGBA program here.
                // Do not invoke prepare on those live nodes after the purge.
                var oldPrograms=models.map(function(model){return model.node.getGLProgramState().getProgram();});
                gpuClearProgramCache();
                var survived=models.map(function(model,i){return model.node.getScene()===scene&&
                    model.node.getTexture()===textures.rgba&&model.node.getGLProgramState().getProgram()===oldPrograms[i];});
                check(survived.every(function(value){return value;}),'ordinary cache purge preserves live nodes and their old Programs',survived,true);
                var rebuilt={};
                ['l','la','rgba'].forEach(function(name){
                    // These fresh temporary nodes register only the rebuilt
                    // cache members, never the old Programs held by live nodes.
                    var temporary=new cc.Sprite(textures[name]);
                    rebuilt[name]=temporary.getGLProgramState().getProgram();
                });
                var unchanged=models.map(function(model,i){return model.node.getGLProgramState().getProgram()===oldPrograms[i];});
                check(unchanged.every(function(value){return value;}),'building new family references did not re-prepare existing nodes',unchanged,true);
                check(rebuilt.rgba!==oldPrograms[0],'cache rebuild creates a new RGBA Program while the old node Program remains alive',
                    rebuilt.rgba!==oldPrograms[0],true);
                familyPrograms=rebuilt;
                result.snapshots.push({event:'ordinary-ProgramManager-cache-purge',stage:stage,liveNodeProgramsSurvived:survived,
                    liveProgramsUnchangedAfterRebuild:unchanged,newRGBAProgramDiffersFromOld:rebuilt.rgba!==oldPrograms[0],
                    scope:'unloadAllPrograms only; GraphicsCore/device not destroyed or reset; subsequent draws use rebuilt family expectations'});
            }
            Object.keys(textures).forEach(function(name){
                if(plan.linear)textures[name].setAntiAliasTexParameters();else textures[name].setAliasTexParameters();
            });
            if(plan.initial) {ready=true;return;}
            var target=plan.format;
            models.forEach(function(model) {
                var previous=model.format;model.previousFormat=previous;model.format=target;
                if(model.kind==='native-Animate') {
                    var animation=new cc.Animation([frames[previous],frames[target]],.05,1);
                    model.node.runAction(cc.sequence(cc.animate(animation),cc.callFunc(function(){
                        style(model);ready=true;++result.nativeAnimateCompletions;
                        result.snapshots.push({event:'native-Animate-CallFunc-completion',stage:stage,
                            target:target,actualTextureMatches:model.node.getTexture()===textures[target],
                            runningActions:model.node.getNumberOfRunningActions()});
                    })));
                } else {
                    if(model.kind==='setSpriteFrame')model.node.setSpriteFrame(frames[target]);
                    else model.node.setTexture(textures[target]);
                    style(model);
                }
            });
            controls.forEach(function(node){gpuSetNativeSpriteTexture(node,textures[target]);});
            gpuSetNativeSpriteTexture(modern,textures[target]);
        }
        try {
            if(physical.width<320||physical.height<100)throw Error('Need 320x100 physical framebuffer');
            if(typeof gpuCreateNativeSprite!=='function'||typeof gpuSetNativeSpriteTexture!=='function'||
                typeof gpuSpriteProgramType!=='function'||typeof gpuSetSpriteAutoUpdatePS!=='function'||typeof gpuClearProgramCache!=='function')
                throw Error('This candidate probe requires five actual native GPU test helpers; use original cases for an older APK baseline');
            cc.view.setDesignResolutionSize(physical.width,physical.height,0);cc.director.setProjection(0);
            scene=new cc.Scene();scene.addChild(new cc.LayerColor(cc.color(17,23,31,255),physical.width,physical.height));
            ['l','la','rgba'].forEach(function(name){
                textures[name]=cc.textureCache.addImage('src/SpriteFormat-'+name+'.png');
                if(!textures[name])throw Error('Missing numerical '+name+' PNG fixture');
                savedSamplers[name]=sampler(textures[name]);
                frames[name]=new cc.SpriteFrame(textures[name],cc.rect(0,0,2,2));frames[name].retain();
                result.snapshots.push({event:'fixture-loaded',format:name,pixelFormat:textures[name].getPixelFormat(),
                    pma:textures[name].hasPremultipliedAlpha(),sourceRowsTopFirst:palettes[name]});
                var reference=gpuCreateNativeSprite(textures[name]);reference.retain();nativeReferences.push(reference);
                nativeTypes[name]=gpuSpriteProgramType(reference);if(name==='rgba')modern=reference;
            });
            add('setTexture-tint-opacity',new cc.Sprite(textures.rgba),'rgba',0,true);
            add('setSpriteFrame',new cc.Sprite(textures.l),'l',1,false);
            add('native-Animate',new cc.Sprite(textures.la),'la',2,false);
            models.forEach(function(model){familyPrograms[model.format]=model.node.getGLProgramState().getProgram();});
            check(familyPrograms.l!==familyPrograms.la&&familyPrograms.la!==familyPrograms.rgba&&familyPrograms.l!==familyPrograms.rgba,
                'three initial legacy formats select three distinct real family Programs',true,true);
            var clone=add('cloned-ProgramState',new cc.Sprite(textures.rgba),'rgba',3,false);
            var original=clone.node.getGLProgramState();
            var copied=cc.GLProgramState.create(clone.node.getShaderProgram());
            check(copied!==original&&copied.getProgram()===original.getProgram(),'actual native clone keeps the same Program',
                {distinct:copied!==original,sameProgram:copied.getProgram()===original.getProgram()},true);
            clone.node.setGLProgramState(copied);
            add('AtlasNode-native-setTexture',new cc.LabelAtlas('A','src/SpriteFormat-rgba.png',2,2,'A'),'rgba',4,false);
            add('default-blend-tint-opacity',new cc.Sprite(textures.la),'la',5,true,true);
            var unknown=new cc.Sprite(textures.rgba);unknown.retain();controls.push(unknown);
            unknown.__formatExpectedState=cc.GLProgramState.create(cc.shaderCache.getProgram('ShaderPositionTextureColor'));
            unknown.setGLProgramState(unknown.__formatExpectedState);
            var disabled=new cc.Sprite(textures.rgba);disabled.retain();controls.push(disabled);
            disabled.__formatExpectedState=disabled.getGLProgramState();gpuSetSpriteAutoUpdatePS(disabled,false);
            check(nativeTypes.l!==nativeTypes.la&&nativeTypes.la!==nativeTypes.rgba&&nativeTypes.l!==nativeTypes.rgba,
                'three native-created reference sprites select distinct real builtin format programs',nativeTypes,'three distinct program types');
            plans.push({label:'three-initial-formats-nearest',initial:true,linear:false});
            [false,true].forEach(function(linear){sequence.forEach(function(name,index){
                plans.push({label:(linear?'linear':'nearest')+'-'+index+'-'+name,linear:linear,format:name});
            });});
            ['l','la','rgba'].forEach(function(name,index){
                plans.push({label:'post-cache-purge-'+index+'-'+name,linear:false,format:name,postPurge:true,purge:index===0});
            });
            listener=cc.eventManager.addCustomListener('director_after_draw',function(){
                try {
                    ++result.drawnFrames;
                    if(stage<0||!ready||++draws<3)return;
                    observe();ready=false;
                    if(stage===plans.length-1)finish();
                    else scene.scheduleOnce(function(){begin(stage+1);},0);
                }catch(error){finish(error);}
            });
            scene.scheduleOnce(function(){begin(0);},0);
            scene.scheduleOnce(function(){finish(Error('Sprite format GPU probe timed out'));},20);
            cc.director.pushScene(scene);pushed=true;
        } catch(error){finish(error);}
        return result;
    };
})(this);
