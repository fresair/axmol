/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Legacy APIs expressed in terms of the current generated Axmol bindings. */
(function(global){
    require('script/jsb_keycodes.js');
    function replaceFactory(namespace,name,factory){
        var Native=namespace[name];if(!Native)return;
        function Legacy(){return factory.apply(null,arguments);}
        Object.setPrototypeOf(Legacy,Native);Legacy.prototype=Native.prototype;Legacy.prototype.constructor=Legacy;
        Legacy.extend=Native.extend;Legacy.create=Legacy;namespace[name]=Legacy;
        namespace[name.charAt(0).toLowerCase()+name.slice(1)]=Legacy;
    }
    function withTarget(fn,target){return typeof fn==='function' ? function(){return fn.apply(target,arguments);} : fn;}
    var nativeCallFunc=cc.callFunc;
    cc.callFunc=function(fn,target,data){
        if(typeof fn!=='function')throw new TypeError('callFunc requires a function');
        return nativeCallFunc(function(node){return fn.call(target,node,data);});
    };
    if(cc.CallFunc)cc.CallFunc.create=cc.callFunc;
    if(jsb.AssetsManagerEx){
        jsb.AssetsManagerLegacy=jsb.AssetsManager;
        var AssetManager=jsb.AssetsManagerEx;
        jsb.AssetsManager=function(manifest,storage,compare){
            var manager=AssetManager.create(manifest,storage);
            if(compare)manager.setVersionCompareHandle(compare);
            return manager;
        };
        Object.setPrototypeOf(jsb.AssetsManager,AssetManager);jsb.AssetsManager.prototype=AssetManager.prototype;
        jsb.AssetsManager.create=jsb.AssetsManager;jsb.AssetsManager.extend=AssetManager.extend;
        jsb.EventAssetsManager=jsb.EventAssetsManagerEx;
        jsb.EventListenerAssetsManager=jsb.EventListenerAssetsManagerEx;
        Object.keys(jsb.EventAssetsManager.EventCode).forEach(function(key){jsb.EventAssetsManager[key]=jsb.EventAssetsManager.EventCode[key];});
    }
    if(global.sp&&sp.SkeletonAnimation&&!sp.SkeletonRenderer)sp.SkeletonRenderer=sp.SkeletonAnimation;
    cc.view.isRetinaEnabled=function(){return cc.sys.os===cc.sys.OS_IOS||cc.sys.os===cc.sys.OS_OSX;};
    cc.view.isAutoFullScreenEnabled=function(){return true;};
    if(cc.Menu){var NativeMenu=cc.Menu;replaceFactory(cc,'Menu',function(){
        var items=arguments.length===1&&Array.isArray(arguments[0])?arguments[0]:Array.prototype.slice.call(arguments);
        return NativeMenu.createWithArray(items.filter(function(item){return item!=null;}));
    });}
    ['MenuItemImage','MenuItemSprite','MenuItemLabel','MenuItemFont','MenuItemAtlasFont'].forEach(function(name){
        var Native=cc[name];if(!Native)return;
        replaceFactory(cc,name,function(){
            var args=Array.prototype.slice.call(arguments),callback=-1;
            for(var i=0;i<args.length;i++)if(typeof args[i]==='function'){callback=i;break;}
            if(callback>=0){args[callback]=withTarget(args[callback],args[callback+1]);args.length=callback+1;}
            return Native.create.apply(Native,args);
        });
    });
    if(cc.MenuItem){var setMenuCallback=cc.MenuItem.prototype.setCallback;
        cc.MenuItem.prototype.setCallback=function(fn,target){return setMenuCallback.call(this,withTarget(fn,target));};
    }
    if(cc.MenuItemToggle){var NativeToggle=cc.MenuItemToggle;replaceFactory(cc,'MenuItemToggle',function(){
        var items=Array.prototype.slice.call(arguments),target=null,fn=function(){};
        for(var i=0;i<items.length;i++)if(typeof items[i]==='function'){
            fn=items[i];target=items[i+1];items.length=i;break;
        }
        if(items.length===1&&Array.isArray(items[0]))items=items[0];
        return NativeToggle.createWithCallback(withTarget(fn,target),items.filter(function(item){return item!=null;}));
    });}
    if(cc.Node){var np=cc.Node.prototype;
        [cc,global.ccui,global.ccs,global.sp].forEach(function(namespace){
            if(!namespace)return;
            Object.getOwnPropertyNames(namespace).forEach(function(name){var Type=namespace[name];
                if(typeof Type!=='function'||!Type.prototype||(Type!==cc.Node&&!np.isPrototypeOf(Type.prototype)))return;
                ['setAnchorPoint','setContentSize','setNormalizedPosition'].forEach(function(method){
                    if(!Object.prototype.hasOwnProperty.call(Type.prototype,method))return;
                    var native=Type.prototype[method];if(typeof native!=='function')return;
                    Type.prototype[method]=function(x,y){return arguments.length===2?native.call(this,{x:x,y:y}):native.apply(this,arguments);};
                });
            });
        });
        np.getAnchorPointX=function(){return this.getAnchorPoint().x;};np.getAnchorPointY=function(){return this.getAnchorPoint().y;};
        np.setAnchorPointX=function(x){var p=this.getAnchorPoint();this.setAnchorPoint(x,p.y);};
        np.setAnchorPointY=function(y){var p=this.getAnchorPoint();this.setAnchorPoint(p.x,y);};
        cc.defineGetterSetter(np,'anchorX',np.getAnchorPointX,np.setAnchorPointX);cc.defineGetterSetter(np,'anchorY',np.getAnchorPointY,np.setAnchorPointY);
        np.setLocalZOrder=np.setLocalZOrder||np.setZOrder;
        if(np.setIgnoreAnchorPointForPosition&&!np.ignoreAnchorPointForPosition)np.ignoreAnchorPointForPosition=np.setIgnoreAnchorPointForPosition;
        var contentSize=np.getContentSize;
        np.getContentSize=function(){var p=contentSize.call(this);return {width:p.width===undefined?p.x:p.width,height:p.height===undefined?p.y:p.height};};
    }
    if(cc.EventDispatcher){var ep=cc.EventDispatcher.prototype;
        ep.removeListener=ep.removeEventListener;
        ep.removeAllListeners=ep.removeAllEventListeners;
        ep.removeCustomListeners=ep.removeCustomEventListeners;
        ep.pauseTarget=ep.pauseEventListenersForTarget;ep.resumeTarget=ep.resumeEventListenersForTarget;
        ep.removeListeners=function(target,recursive){
            if(typeof target==='number')return this._removeLegacyListeners(target);
            return this.removeEventListenersForTarget(target,!!recursive);
        };
    }
    if(cc.EventCustom){
        var custom=cc.EventCustom.prototype,nativeUserData=custom.getUserData;
        custom.setUserData=function(data){
            if(!jsb.isObjectValid(this))throw new Error('Invalid native custom event');
            this._userData=data;
        };
        custom.getUserData=function(){
            // Native dispatchCustomEvent also exposes borrowed, scoped data.
            var data=nativeUserData.call(this);
            return Object.prototype.hasOwnProperty.call(this,'_userData')?this._userData:data;
        };
    }
    if(cc.PointerEvent){
        cc.Touch=cc.EventMouse=cc.PointerEvent;
        var pointer=cc.PointerEvent.prototype,nativeGetButton=pointer.getButton;
        pointer.getButton=function(){
            var button=nativeGetButton.call(this);
            if(button<0&&this.getPointerType()===0&&this.getPhase()===2){
                var mask=this.getPressedButtons()>>>0;
                if(mask){button=0;while((mask&1)===0){mask>>>=1;++button;}}
            }
            return button;
        };
        pointer.getMouseButton=pointer.getButton;
        pointer.getCursorX=function(){return this.getLocation().x;};
        pointer.getCursorY=function(){return this.getLocation().y;};
        pointer.getDeltaX=function(){return this.getDelta().x;};
        pointer.getDeltaY=function(){return this.getDelta().y;};
        cc.EventMouse.BUTTON_LEFT=0;cc.EventMouse.BUTTON_RIGHT=1;cc.EventMouse.BUTTON_MIDDLE=2;
    }
    if(global.ccui){
        Object.getOwnPropertyNames(ccui).forEach(function(name){var Type=ccui[name];if(!Type||!Type.prototype)return;
            ['addTouchEventListener','addClickEventListener','addEventListener'].forEach(function(method){
                var native=Type.prototype[method];if(typeof native!=='function'||!Object.prototype.hasOwnProperty.call(Type.prototype,method))return;
                Type.prototype[method]=function(fn,target){return native.call(this,withTarget(fn,target));};
            });
        });
        if(ccui.Widget){ccui.Widget.LOCAL_TEXTURE=0;ccui.Widget.PLIST_TEXTURE=1;
            ccui.Widget.TOUCH_BEGAN=0;ccui.Widget.TOUCH_MOVED=1;ccui.Widget.TOUCH_ENDED=2;ccui.Widget.TOUCH_CANCELED=3;
        }
    }
    if(cc.AudioEngine){var audio=cc.AudioEngine,music=-1,musicFile='',musicLoop=false,musicVolume=1,effectsVolume=1,effects={};
        cc.audioEngine={
            playMusic:function(file,loop){this.stopMusic();musicFile=file;musicLoop=!!loop;music=audio.play2d(file,musicLoop,musicVolume);return music;},
            // The v3 Android and Apple backends ignore releaseData and retain the music for rewind.
            stopMusic:function(release){if(music>=0)audio.stop(music);music=-1;},
            pauseMusic:function(){if(music>=0)audio.pause(music);},resumeMusic:function(){if(music>=0)audio.resume(music);},
            rewindMusic:function(){var state=music>=0?audio.getState(music):-1;
                if(state<0){if(musicFile)music=audio.play2d(musicFile,musicLoop,musicVolume);}
                else{audio.setCurrentTime(music,0);if(state===2)audio.resume(music);}},
            isMusicPlaying:function(){return music>=0&&audio.getState(music)===1;},
            getMusicVolume:function(){return musicVolume;},setMusicVolume:function(v){musicVolume=cc.clampf(v,0,1);if(music>=0)audio.setVolume(music,musicVolume);},
            playEffect:function(file,loop,pitch,pan,gain){var id=audio.play2d(file,!!loop,effectsVolume*(gain===undefined?1:gain));
                if(id>=0){effects[id]=true;if(pitch!==undefined)audio.setPitch(id,pitch);if(pan!==undefined)audio.setPan(id,pan);audio.setFinishCallback(id,function(){delete effects[id];});}return id;},
            stopEffect:function(id){audio.stop(id);delete effects[id];},pauseEffect:function(id){audio.pause(id);},resumeEffect:function(id){audio.resume(id);},
            stopAllEffects:function(){Object.keys(effects).forEach(function(id){audio.stop(+id);});effects={};},
            pauseAllEffects:function(){Object.keys(effects).forEach(function(id){audio.pause(+id);});},
            resumeAllEffects:function(){Object.keys(effects).forEach(function(id){audio.resume(+id);});},
            getEffectsVolume:function(){return effectsVolume;},setEffectsVolume:function(v){effectsVolume=cc.clampf(v,0,1);Object.keys(effects).forEach(function(id){audio.setVolume(+id,effectsVolume);});},
            preloadEffect:function(file,cb){audio.preload(file,cb||function(){});},unloadEffect:function(file){audio.uncache(file);},
            end:function(){audio.end();music=-1;musicFile='';musicLoop=false;effects={};},willPlayMusic:function(){return true;}
        };
    }
})(this);
