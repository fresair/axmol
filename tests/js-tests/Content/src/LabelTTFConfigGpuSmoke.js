// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Supplemental real-GPU font check; the original Particle3D test stays intact.
(function(global){
    global.runLegacyTTFConfigGpuSmoke=function(done){
        var result={passed:false,checks:[],graphicsRequired:true},scene=null,listener=null,pushed=false;
        function expect(ok,message){result.checks.push({detail:message,passed:!!ok});if(!ok)throw Error(message);}
        function finish(error){
            if(listener)cc.eventManager.removeListener(listener);
            if(pushed)cc.director.popScene();
            result.error=error?String(error.stack||error):null;result.passed=!error;
            cc.log('LABEL_TTF_CONFIG_GPU_RESULT '+JSON.stringify(result));
            if(typeof done==='function')done(result);
        }
        try{
            var original={outlineSize:0,fontSize:15,glyphs:0,customGlyphs:'',fontFilePath:'fonts/tahoma.ttf'};
            var label=cc.Label.createWithTTF(original,'Particle count: 0');
            expect(label&&jsb.isObjectValid(label),'original Particle3D config creates real native Label');
            var native=label.getTTFConfig();
            expect(native.fontFilePath===original.fontFilePath&&native.fontSize===15&&native.glyphs===0&&
                native.outlineSize===0&&native.customGlyphs===''&&!native.distanceFieldEnable,'actual native original TTF fields');
            expect(label.getString()==='Particle count: 0','original text retained');
            var custom={fontFilePath:'fonts/tahoma.ttf',fontSize:21.9,outlineSize:1.9,glyphs:3,
                customGlyphs:'ABC',distanceFieldEnable:false};
            var second=cc.Label.createWithTTF(custom,'ABC',cc.TEXT_ALIGNMENT_CENTER,320);
            expect(second&&jsb.isObjectValid(second),'custom glyph config creates native FontAtlas');
            var read=second.getTTFConfig();
            expect(read.fontSize===21&&read.outlineSize===1&&read.glyphs===3&&read.customGlyphs==='ABC',
                'old numeric truncation and owned custom glyph string reach native config');
            custom.fontSize=18;custom.outlineSize=0;second.setTTFConfig(custom);
            expect(second.getTTFConfig().fontSize===18&&second.getTTFConfig().outlineSize===0,'setTTFConfig changes real native font');
            var third=cc.Label.createWithTTF('String overload','fonts/tahoma.ttf',17);
            expect(third&&third.getString()==='String overload','real string-based native TTF factory');
            scene=new cc.Scene();
            [label,second,third].forEach(function(item,index){item.setPosition(cc.winSize.width/2,cc.winSize.height/2+50-index*50);scene.addChild(item);});
            var frames=0;
            listener=cc.eventManager.addCustomListener(cc.Director.EVENT_AFTER_DRAW,function(){
                if(cc.director.getRunningScene()!==scene||++frames<3)return;
                try{
                    expect(label.getContentSize().width>0&&second.getContentSize().width>0&&third.getContentSize().width>0,
                        'all real font atlases produce glyph geometry across actual render frames');
                    result.frames=frames;finish(null);
                }catch(error){finish(error);}
            });
            cc.director.pushScene(scene);pushed=true;
        }catch(error){finish(error);}
    };
})(this);
