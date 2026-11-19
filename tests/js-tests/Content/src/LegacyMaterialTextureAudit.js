// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Test-only GPU-context diagnosis. Original MaterialSystemTest stays unchanged.
(function(global) {
    global.runLegacyMaterialTextureAudit=function(completed) {
        if(typeof legacyMaterialTextureFacts!=='function')
            throw Error('Register the test-only native material-texture-diagnostics.cpp helper');
        if(!cc.director.getRunningScene())throw Error('Invoke from the actual Scheduler with a running baseline Scene');
        var result={passed:false,scope:'actual native Material texture/layout/uniform facts; original visual comparison is separate',
            checks:[],snapshots:[],graphicsRequired:true};
        var scene=null,listener=null,pushed=false,finished=false,frames=0,models=[],rawModel=null;
        function check(ok,detail,actual) {result.checks.push({passed:!!ok,detail:detail,actual:actual});}
        function snapshot(label,material,sprite) {
            var value=legacyMaterialTextureFacts(material,sprite || null);
            result.snapshots.push({label:label,facts:value});
            return value;
        }
        function normal(facts) {
            var techniques=facts.material.techniques;
            for(var i=0;i<techniques.length;++i)
                if(techniques[i].name==='normal')return techniques[i].passes[0];
            throw Error('The real original Material normal technique was not parsed');
        }
        function usesBoss(pass) {
            return pass.textureBindings.some(function(binding) {
                return binding.textures.some(function(texture){return texture.matchesBoss;});
            });
        }
        function materialUsesBoss(facts) {
            return facts.material.techniques.some(function(technique) {
                return technique.passes.some(usesBoss);
            });
        }
        function finish(error) {
            if(finished)return;finished=true;
            if(error)result.error=String(error.stack||error);
            try {
                if(listener)cc.eventManager.removeListener(listener);
                if(scene)scene.unscheduleAllCallbacks();
                if(pushed)cc.director.popScene();
            } catch(cleanupError) {
                result.error=(result.error?result.error+'\n':'')+'cleanup: '+String(cleanupError.stack||cleanupError);
            }
            result.frames=frames;result.failedChecks=result.checks.filter(function(item){return !item.passed;}).length;
            result.passed=!result.error&&!result.failedChecks;
            global.legacyMaterialTextureAuditResult=result;
            cc.log('LEGACY_MATERIAL_TEXTURE_AUDIT_RESULT '+JSON.stringify(result));
            if(typeof completed==='function')completed(result);
        }
        try {
            scene=new cc.Scene();
            var size=cc.winSize;
            // The legacy typed-ctor installer sets the public facade's create
            // entry to that facade. Its native static factory remains on the
            // original constructor in the facade's prototype chain.
            var nativeMesh=Object.getPrototypeOf(jsb.Sprite3D);
            if(!nativeMesh || nativeMesh===jsb.Sprite3D || typeof nativeMesh.create!=='function')
                throw Error('The original native MeshRenderer filename factory is unavailable');
            // This raw GLProgram uses the exact same compiled shader pair and
            // native Program cache as the Material below. Old GL leaves an
            // unassigned custom sampler at unit zero, so the raw path must
            // still receive its mesh diffuse texture after Material isolation.
            var rawShader=new cc.GLProgram('Shaders3D/3d_position_tex.vert','Shaders3D/3d_color_tex.frag');
            var rawState=cc.GLProgramState.create(rawShader);
            var rawSprite=new jsb.Sprite3D('Sprite3DTest/boss1.obj');
            rawSprite.setTexture('Sprite3DTest/boss.png');
            rawSprite.setGLProgramState(rawState);
            rawSprite.setScale(3);rawSprite.setNormalizedPosition(cc.p(.5,.75));
            scene.addChild(rawSprite);
            rawModel={sprite:rawSprite,material:rawSprite.getMaterial(0)};
            var rawAttached=snapshot('raw GLProgram mesh default diffuse binding',rawModel.material,rawSprite);
            check(materialUsesBoss(rawAttached),'raw GLProgram still receives the actual mesh boss.png diffuse texture',rawAttached);
            // Distinguish the updated legacy constructor from the true native
            // filename factory, which also guesses a same-basename texture.
            ['legacy constructor','native filename factory'].forEach(function(kind,index) {
                var material=cc.Material.createWithFilename('Materials/3d_effects.material');
                if(!material)throw Error('Original native material factory failed');
                var before=snapshot(kind+' material before model attachment',material);
                check(usesBoss(normal(before)),kind+' material initially binds the declared boss.png',normal(before));
                check(material.getTechniqueByName('normal').getPassByIndex(0).getGLProgramState().getProgram()===rawState.getProgram(),
                    kind+' shares the actual cached native Program with the raw GLProgram control');
                var sprite=index===0?new jsb.Sprite3D('Sprite3DTest/boss1.obj'):
                    nativeMesh.create('Sprite3DTest/boss1.obj');
                if(!sprite)throw Error(kind+' model factory failed');
                sprite.setScale(6);sprite.setNormalizedPosition(cc.p(index===0?.3:.7,.5));
                scene.addChild(sprite);
                snapshot(kind+' model before explicit material',sprite.getMaterial(0),sprite);
                sprite.setMaterial(material);
                var attached=snapshot(kind+' after setMaterial',material,sprite);
                check(usesBoss(normal(attached)),kind+' setMaterial preserves the declared boss.png',normal(attached));
                var copy=material.clone();
                var cloned=snapshot(kind+' independently cloned material',copy);
                check(usesBoss(normal(cloned)),kind+' Material.clone preserves declared sampler texture',normal(cloned));
                models.push({kind:kind,sprite:sprite,material:material});
                var clonedSprite=new jsb.Sprite3D('Sprite3DTest/boss1.obj');
                clonedSprite.setScale(3);clonedSprite.setNormalizedPosition(cc.p(index===0?.3:.7,.25));
                clonedSprite.setMaterial(copy);
                scene.addChild(clonedSprite);
                var cloneAttached=snapshot(kind+' cloned material after actual mesh attachment',copy,clonedSprite);
                check(usesBoss(normal(cloneAttached)),kind+' cloned Pass preserves the declared texture when attached to a new mesh',normal(cloneAttached));
                // Native Mesh stores/replays this distinct diffuse image. An
                // explicit Material sampler must retain its independently
                // declared boss image, as on the old GL units 0 versus 4.
                clonedSprite.setTexture('Sprite3DTest/caustics.png');
                var replaced=snapshot(kind+' cloned material after mesh diffuse replacement',copy,clonedSprite);
                check(usesBoss(normal(replaced)),kind+' mesh diffuse replacement does not override the explicit material sampler',normal(replaced));
                models.push({kind:kind+' clone with different mesh diffuse',sprite:clonedSprite,material:copy});
            });
            listener=cc.eventManager.addCustomListener('director_after_draw',function() {
                try {
                    if(++frames<3)return;
                    var rawRendered=snapshot('raw GLProgram after actual mesh rendering',rawModel.material,rawModel.sprite);
                    check(materialUsesBoss(rawRendered),'raw GLProgram draw still has its mesh-provided diffuse texture',rawRendered);
                    models.forEach(function(model) {
                        var facts=snapshot(model.kind+' after native mesh rendering',model.material,model.sprite);
                        var pass=normal(facts);
                        check(usesBoss(pass),model.kind+' draw-time sampler still references boss.png',pass);
                        var color=pass.colorUniform.values;
                        check(color&&color.length===4&&color.every(function(channel){return channel===1;}),
                            model.kind+' actual native draw writes white u_color',color);
                        check(facts.meshes.length>0&&facts.meshes.every(function(mesh) {
                            var techniques=mesh.material.techniques;
                            for(var i=0;i<techniques.length;++i)if(techniques[i].name==='normal') {
                                var p=techniques[i].passes[0];
                                return p.nativeVertexBinding&&p.reflectedInputs.some(function(input){return input.semantic==='POSITION';})&&
                                    p.reflectedInputs.some(function(input){return input.semantic==='TEXCOORD';});
                            }
                            return false;
                        }),model.kind+' actual native mesh has POSITION/TEXCOORD shader inputs and a real binding',facts.meshes);
                    });
                    finish();
                } catch(error) {finish(error);}
            });
            scene.scheduleOnce(function(){finish(Error('Native Material audit timed out before actual rendering'));},5);
            cc.director.pushScene(scene);pushed=true;
        } catch(error) {finish(error);}
        return result;
    };
})(this);
