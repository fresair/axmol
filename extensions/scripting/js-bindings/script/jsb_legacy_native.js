/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Native API changes needed by the unmodified cocos2d-x/v3 test project. */
(function (global) {
    var cc = global.cc, jsb = global.jsb;
    function ctor(name, fn) {
        var Type = cc[name];
        if (!Type) return;
        Type.prototype.ctor = fn;
        Object.defineProperty(Type.prototype, '__legacyCtor', {value:true, configurable:true});
        if (typeof Type.create === 'function') Type.create = Type;
    }
    function initWith(name, method) {
        ctor(name, function () { return this[method].apply(this, arguments); });
    }
    cc.extend(cc.sys, {WIN32:0, LINUX:1, MACOS:2, ANDROID:3, IPHONE:4, IPAD:5});
    var platforms = {'Windows':0, 'Linux':1, 'OS X':2, 'Android':3, 'iOS':4};
    cc.sys.platform = platforms[cc.sys.os];
    var languages={ENGLISH:'en',CHINESE:'zh',FRENCH:'fr',ITALIAN:'it',GERMAN:'de',SPANISH:'es',DUTCH:'du',
        RUSSIAN:'ru',KOREAN:'ko',JAPANESE:'ja',HUNGARIAN:'hu',PORTUGUESE:'pt',ARABIC:'ar',NORWEGIAN:'no',
        POLISH:'pl',TURKISH:'tr',UKRAINIAN:'uk',ROMANIAN:'ro',BULGARIAN:'bg',BELARUSIAN:'be',UNKNOWN:'unknown'};
    Object.keys(languages).forEach(function (name) { cc.sys['LANGUAGE_'+name]=languages[name]; });
    ['WIN32','LINUX','MACOS','ANDROID','IPHONE','IPAD'].forEach(function (name) {
        cc['PLATFORM_' + name] = cc.sys[name];
    });
    cc.PI = Math.PI; cc.PI2 = Math.PI * 2; cc.RAD = Math.PI / 180; cc.DEG = 180 / Math.PI;
    cc.FLT_MAX = 3.402823466e38; cc.UINT_MAX = 0xffffffff; cc.INVALID_INDEX = -1;
    cc.lerp = function (a, b, t) { return a + (b - a) * t; };
    cc.rand = function () { return Math.random() * 0xffffff; };
    cc.random0To1 = Math.random;
    cc.randomMinus1To1 = function () { return (Math.random() - 0.5) * 2; };
    if (cc.AudioEngine) {
        jsb.AudioEngine = cc.AudioEngine;
        jsb.AudioProfile = cc.AudioProfile;
        // AudioEngine's native constants are static constexpr fields.
        cc.AudioEngine.INVALID_AUDIO_ID = -1;
        cc.AudioEngine.TIME_UNKNOWN = -1;
    }
    ['MenuItem','MenuItemSprite','MenuItemToggle'].forEach(function (name) {
        var Type=cc[name];if(!Type)return;
        var native=Type.prototype.setEnabled;
        Type.prototype.setEnabled=function (value) { return native.call(this,!!value); };
        cc.defineGetterSetter(Type.prototype,'enabled',Type.prototype.isEnabled,Type.prototype.setEnabled);
    });
    cc._RENDER_TYPE_CANVAS = 0; cc._RENDER_TYPE_WEBGL = 1; cc._renderType = cc.game.RENDER_TYPE_OPENGL;
    cc.extend(cc,{ZERO:0,ONE:1,SRC_COLOR:0x300,ONE_MINUS_SRC_COLOR:0x301,SRC_ALPHA:0x302,
        ONE_MINUS_SRC_ALPHA:0x303,DST_ALPHA:0x304,ONE_MINUS_DST_ALPHA:0x305,DST_COLOR:0x306,
        ONE_MINUS_DST_COLOR:0x307,SRC_ALPHA_SATURATE:0x308,CONSTANT_COLOR:0x8001,
        ONE_MINUS_CONSTANT_COLOR:0x8002,CONSTANT_ALPHA:0x8003,ONE_MINUS_CONSTANT_ALPHA:0x8004});
    cc.BlendFunc=function (src,dst) { this.src=src;this.dst=dst; };
    [['DISABLE',cc.ONE,cc.ZERO],['ALPHA_PREMULTIPLIED',cc.ONE,cc.ONE_MINUS_SRC_ALPHA],
        ['ALPHA_NON_PREMULTIPLIED',cc.SRC_ALPHA,cc.ONE_MINUS_SRC_ALPHA],['ADDITIVE',cc.SRC_ALPHA,cc.ONE]]
        .forEach(function (entry) { Object.defineProperty(cc.BlendFunc,entry[0],{get:function () {
            return new cc.BlendFunc(entry[1],entry[2]); }}); });

    // v3 jsb_create_apis Layer._ctor calls native Layer.init without forwarding
    // the gradient arguments used by its shared BaseTestLayer mixin.
    ctor('Layer', function () { return this.initLayer(); });
    // v3's Sprite3D _ctor initializes the model on the already allocated
    // native receiver. A derived Player calls _super(file), not Node.init(file).
    ctor('MeshRenderer', function (model, texture) {
        if(model === undefined)return this.init();
        if(!this.initWithFile(model))throw new Error('Native MeshRenderer initialization failed');
        var box=this.getBoundingBox();
        this.setContentSize(cc.size(box.width,box.height));
        if(texture !== undefined)this.setTexture(texture);
        return true;
    });
    var reorderChild = cc.Node.prototype.reorderChild;
    cc.Node.prototype.reorderChild = function (child, order) {
        if(arguments.length!==2)return reorderChild.apply(this,arguments);
        var value=Number(order);
        if(!isFinite(value))throw new TypeError('reorderChild expects a finite z-order');
        value=value<0?Math.ceil(value):Math.floor(value);
        if(value < -2147483648 || value > 2147483647)throw new RangeError('reorderChild z-order exceeds int32');
        return reorderChild.call(this,child,value);
    };
    ctor('LayerColor', function (color, width, height) {
        var size = cc.winSize;
        return this.initWithColor(color || cc.color(0,0,0,255),
            width === undefined ? size.width : width, height === undefined ? size.height : height);
    });
    // v3 exports LayerColor::initWithColor as init (one or three arguments).
    // Derived layers call this.init(color) after their base constructor.
    cc.LayerColor.prototype.init = cc.LayerColor.prototype.initWithColor;
    ctor('LayerGradient', function (start, end, vector) {
        return this.initWithColor(start || cc.color(0,0,0,255), end || cc.color(0,0,0,255), vector || cc.p(0,-1));
    });
    (function () {
        var p = cc.LayerGradient.prototype;
        var getOpacity = cc.Node.prototype.getOpacity;
        var setOpacity = cc.Node.prototype.setOpacity;
        var setStartOpacity = p.setStartOpacity;
        var nativeInit = p.initWithColor;
        p.initWithColor = function () {
            var ok = nativeInit.apply(this, arguments);
            if (ok) {
                // v3 stores both endpoint alpha values independently and starts
                // Node opacity at255. Modern native init puts start.a in Node.
                var alpha = getOpacity.call(this);
                setOpacity.call(this, 255);
                setStartOpacity.call(this, alpha);
            }
            return ok;
        };
        // new/create/lowercase/extend already share the typed ctor above;
        // wrapping create again would restore start alpha a second time.
        ['Start', 'End'].forEach(function (which) {
            var get = 'get' + which + 'Color', set = 'set' + which + 'Color';
            var nativeGet = p[get], nativeSet = p[set];
            var opacity = which === 'Start' ? getOpacity : p.getEndOpacity;
            p[get] = function () {
                var color = nativeGet.apply(this, arguments);
                return {r: color.r, g: color.g, b: color.b};
            };
            p[set] = function (color) {
                if (arguments.length !== 1) return nativeSet.apply(this, arguments);
                // These old setters accept RGB; an incidental input alpha must
                // not change Node opacity or the independent end opacity.
                var alpha = opacity.call(this);
                return nativeSet.call(this, {r: color.r, g: color.g, b: color.b, a: alpha});
            };
        });
    })();
    ctor('Sprite', function (source, rect) {
        if (source === undefined) return this.init();
        if (typeof source === 'string') {
            if (source.charAt(0) === '#') return this.initWithSpriteFrameName(source.slice(1));
            return rect ? this.initWithFile(source, rect) : this.initWithFile(source);
        }
        if (source instanceof cc.Texture2D) return rect ? this.initWithTexture(source, rect) : this.initWithTexture(source);
        if (source instanceof cc.SpriteFrame) return this.initWithSpriteFrame(source);
        throw new TypeError('Sprite expects a filename, sprite frame or texture');
    });
    ctor('LayerMultiplex',function () {
        if(!arguments.length)return this.init();
        var layers=arguments.length===1&&Array.isArray(arguments[0])?arguments[0]:Array.prototype.slice.call(arguments);
        if(!layers.length)return this.init();
        if(layers.some(function (layer) { return !(layer instanceof cc.Node); }))
            throw new TypeError('LayerMultiplex expects native layers');
        return this.initWithArray(layers);
    });
    if(cc.LayerMultiplex) {
        // Its variadic native factories are excluded from generated bindings,
        // so installClass had no factory through which to run the typed ctor.
        var NativeMultiplex=cc.LayerMultiplex;
        var Multiplex=function () {
            var value=NativeMultiplex.__allocate();
            if(this instanceof Multiplex && this.constructor!==Multiplex)
                value=jsb._associateObjectWithNative(this,value);
            if(value.ctor.apply(value,arguments)===false)
                throw new Error('Native LayerMultiplex initialization failed');
            return value;
        };
        Object.setPrototypeOf(Multiplex,NativeMultiplex);
        Multiplex.prototype=NativeMultiplex.prototype;
        Multiplex.prototype.constructor=Multiplex;
        Multiplex.create=Multiplex.createWithArray=Multiplex;
        cc.LayerMultiplex=cc.layerMultiplex=Multiplex;
    }
    ctor('SpriteBatchNode',function (source,capacity) {
        capacity=capacity===undefined?29:capacity;
        return typeof source==='string'?this.initWithFile(source,capacity):this.initWithTexture(source,capacity);
    });
    ctor('SpriteFrame',function (source,rect,rotated,offset,size) {
        var method=typeof source==='string'?'initWithTextureFilename':'initWithTexture';
        return size===undefined?this[method](source,rect):this[method](source,rect,!!rotated,offset,size);
    });
    ctor('Animation',function (frames,delay,loops) {
        if(frames===undefined)return this.init();
        delay=delay===undefined?0:delay;loops=loops===undefined?1:loops;
        return !frames.length||frames[0] instanceof cc.SpriteFrame ? this.initWithSpriteFrames(frames,delay,loops) :
            this.initWithAnimationFrames(frames,delay,loops);
    });
    ctor('AnimationFrame',function (frame,delay,userInfo) {
        return delay===undefined?true:this.initWithSpriteFrame(frame,delay,userInfo||{});
    });
    ctor('ParticleSystemQuad',function (source) {
        if(source===undefined||typeof source==='number')return this.initWithTotalParticles(source||100);
        return typeof source==='string'?this.initWithFile(source):this.initWithDictionary(source);
    });
    if(cc.ParticleSystemQuad) {
        cc.ParticleSystem=cc.ParticleSystemQuad;
        cc.particleSystem=cc.ParticleSystem;
    }
    ctor('ParticleBatchNode',function (source,capacity) {
        capacity=capacity===undefined?500:capacity;
        return typeof source==='string'?this.initWithFile(source,capacity):this.initWithTexture(source,capacity);
    });
    ['MoveBy','MoveTo'].forEach(function (name) {
        ctor(name, function (duration, position, y) {
            return this.initWithDuration(duration, typeof position === 'number' ? cc.p(position,y) : position);
        });
    });
    ['JumpBy','JumpTo'].forEach(function (name) {
        ctor(name, function (duration, position, y, height, jumps) {
            return typeof position === 'number' ? this.initWithDuration(duration,cc.p(position,y),height,jumps) :
                this.initWithDuration(duration,position,y,height);
        });
    });
    ['ActionInterval','RotateTo','RotateBy','SkewTo','SkewBy','BezierTo','BezierBy','ScaleTo','ScaleBy',
        'Blink','FadeTo','FadeIn','FadeOut','TintTo','TintBy','DelayTime','ProgressTo','ProgressFromTo',
        'GridAction','Grid3DAction','TiledGrid3DAction','PageTurn3D','FadeOutTRTiles','FadeOutBLTiles',
        'FadeOutUpTiles','FadeOutDownTiles','Twirl','Waves','Liquid','Shaky3D','Ripple3D','Lens3D',
        'Waves3D','SplitCols','SplitRows','JumpTiles3D','WavesTiles3D','TurnOffTiles','ShakyTiles3D',
        'ShatteredTiles3D','ShuffleTiles','ActionTween','OrbitCamera','CardinalSplineTo',
        'CardinalSplineBy','CatmullRomTo','CatmullRomBy'].forEach(function (name) { initWith(name,'initWithDuration'); });
    ctor('RotateTo',function (duration,angle,y) {
        if(typeof angle==='object')return this.initWithDuration(duration,angle);
        return this.initWithDuration(duration,angle,y===undefined?angle:y);
    });
    ['ActionEase','EaseRateAction','EaseIn','EaseOut','EaseInOut','EaseExponentialIn','EaseExponentialOut',
        'EaseExponentialInOut','EaseSineIn','EaseSineOut','EaseSineInOut','EaseBounce','EaseBounceIn',
        'EaseBounceOut','EaseBounceInOut','EaseBackIn','EaseBackOut','EaseBackInOut','EaseElastic',
        'EaseElasticIn','EaseElasticOut','EaseElasticInOut','Repeat','RepeatForever','ReverseTime','Speed']
        .forEach(function (name) { initWith(name,'initWithAction'); });
    initWith('Animate','initWithAnimation'); initWith('TargetedAction','initWithTarget');
    ctor('FadeIn',function (duration) { return this.initWithDuration(duration,255); });
    ctor('FadeOut',function (duration) { return this.initWithDuration(duration,0); });
    ctor('TintTo',function (duration,r,g,b) {
        return this.initWithDuration(duration,typeof r==='object'?r:cc.color(r,g,b));
    });
    initWith('Follow','initWithTarget'); initWith('ReuseGrid','initWithTimes');
    initWith('FlipX','initWithFlipX'); initWith('FlipY','initWithFlipY');
    ctor('Place',function (point,y) { return this.initWithPosition(typeof point==='number'?cc.p(point,y):point); });
    ctor('RemoveSelf',function (cleanup) { return this.init(cleanup === undefined ? true : cleanup); });
    ['Sequence','Spawn'].forEach(function (name) {
        ctor(name,function () {
            var actions=arguments.length===1&&Array.isArray(arguments[0])?arguments[0]:Array.prototype.slice.call(arguments);
            actions=actions.filter(function (action) { return action != null; });
            if (!actions.length) throw new TypeError(name+' requires at least one action');
            if(actions.length===1) actions.push(cc.delayTime(0));
            var first=actions[0];
            for(var i=1;i<actions.length-1;i++) first=cc[name].create(first,actions[i]);
            return this.initWithTwoActions(first,actions[actions.length-1]);
        });
    });
    if (cc.CallFunc) {
        var nativeCallFuncInit = cc.CallFunc.prototype.initWithFunction;
        cc.CallFunc.prototype.initWithFunction = function (fn,receiver,data) {
            if (arguments.length < 1 || arguments.length > 3 || typeof fn !== 'function')
                throw new TypeError('CallFunc.initWithFunction requires a function, optional receiver and data');
            // The native CallFuncN owns this rooted closure. Its argument is
            // the real target of each running original/clone/reverse action.
            return nativeCallFuncInit.call(this,function (node) { return fn.call(receiver,node,data); });
        };
        ctor('CallFunc',function (fn,receiver,data) {
            // v3 permits an uninitialized new CallFunc() followed by init.
            if (fn !== undefined) return this.initWithFunction.apply(this,arguments);
        });
        // ctor() normally redirects create to the constructor. Keep the
        // original legacy factory alias and the existing cc.callFunc identity.
        cc.CallFunc.create = cc.callFunc;
    }
    if (global.ccui) {
        [['Button','init'],['ImageView','init'],['ScrollView','init'],['Layout','init']].forEach(function (entry) {
            var Type=ccui[entry[0]];
            if(Type) Type.prototype.ctor=function () { return this[entry[1]].apply(this,arguments); };
        });
    }
    ctor('LabelAtlas',function (text,file,width,height,start) {
        if(start!==undefined)return this.initWithString(text,file,width,height,typeof start==='string'?start.charCodeAt(0):start);
        return this.initWithString(text,file);
    });
    function sizeResult(Type,name) {
        if(!Type||!Type.prototype||!Object.prototype.hasOwnProperty.call(Type.prototype,name))return;
        var native=Type.prototype[name];
        if(typeof native!=='function'||native.__legacySizeResult)return;
        var adapted=function () {
            var result=native.apply(this,arguments);
            return {width:result.width===undefined?result.x:result.width,height:result.height===undefined?result.y:result.height};
        };
        adapted.__legacySizeResult=true;Type.prototype[name]=adapted;
    }
    function colorResult(Type,name) {
        if(!Type||!Type.prototype||!cc.Node||
            (Type.prototype!==cc.Node.prototype&&!cc.Node.prototype.isPrototypeOf(Type.prototype))||
            !Object.prototype.hasOwnProperty.call(Type.prototype,name))return;
        var native=Type.prototype[name];
        if(typeof native!=='function'||native.__legacyRGBResult)return;
        var adapted=name==='setColor'?function (color) {
            // v3 exposes RGB and opacity separately, while Axmol stores RGBA.
            var opacity=this.getOpacity();
            var result=native.call(this,{r:color.r,g:color.g,b:color.b,a:opacity});
            this.setOpacity(opacity);
            return result;
        }:function () { var color=native.apply(this,arguments);return {r:color.r,g:color.g,b:color.b}; };
        adapted.__legacyRGBResult=true;Type.prototype[name]=adapted;
        if(name==='getColor')cc.defineGetterSetter(Type.prototype,'color',adapted,Type.prototype.setColor);
        if(name==='setColor')cc.defineGetterSetter(Type.prototype,'color',Type.prototype.getColor,adapted);
    }
    [cc,global.ccui,global.ccs].forEach(function (namespace) {
        if(!namespace)return;
        // JS_InitClass exports native constructors as non-enumerable properties.
        Object.getOwnPropertyNames(namespace).forEach(function (name) {
            var Type=namespace[name],p=Type&&Type.prototype;
            if(p&&Object.prototype.hasOwnProperty.call(p,'setBlendFunc')&&typeof p.setBlendFunc==='function'&&!p.setBlendFunc.__legacyBlendArgs) {
                var native=p.setBlendFunc;
                p._setBlendFunc=native;
                p.setBlendFunc=function (src,dst) { return dst===undefined?native.call(this,src):native.call(this,{src:src,dst:dst}); };
                p.setBlendFunc.__legacyBlendArgs=true;
            }
            ['getContentSize','getVisibleSize','getDimensions','getMapSize','getTileSize','getLayerSize','getViewSize']
                .forEach(function (method) { sizeResult(namespace[name],method); });
            ['setColor','getColor','getDisplayedColor'].forEach(function (method) { colorResult(namespace[name],method); });
        });
    });
    if(cc.Node) {
        var np=cc.Node.prototype;
        np._getWidth=function () { return this.getContentSize().width; };
        np._getHeight=function () { return this.getContentSize().height; };
        np._setWidth=function (width) { return this.setContentSize(width,this.getContentSize().height); };
        np._setHeight=function (height) { return this.setContentSize(this.getContentSize().width,height); };
        np._getAnchorX=np.getAnchorPointX;np._getAnchorY=np.getAnchorPointY;
        np._setAnchorX=np.setAnchorPointX;np._setAnchorY=np.setAnchorPointY;
        if(!np.getVertexZ)np.getVertexZ=np.getPositionZ;
        if(!np.setVertexZ)np.setVertexZ=np.setPositionZ;
        np.getRotationX=np.getRotationSkewX;np.setRotationX=np.setRotationSkewX;
        np.getRotationY=np.getRotationSkewY;np.setRotationY=np.setRotationSkewY;
        // v3's unsuffixed JS methods return AffineTransform, while *3D returns Mat4.
        // Preserve each generated native Mat4 entry before restoring the legacy names.
        ['NodeToParent','ParentToNode','NodeToWorld','WorldToNode'].forEach(function (direction) {
            var name='get'+direction+'Transform';
            np[name+'3D']=np[name];
            np[name]=np['get'+direction+'AffineTransform'];
            np[direction.charAt(0).toLowerCase()+direction.slice(1)+'Transform']=function () {
                return np[name].apply(this,arguments);
            };
        });
        np.setUserData=function (data) { this.userData=data; };
        np.getUserData=function () { return this.userData; };
    }
    if(cc.Label) {
        var lp=cc.Label.prototype;
        if(!lp.getFontName)lp.getFontName=lp.getSystemFontName;
        if(!lp.setFontName)lp.setFontName=lp.setSystemFontName;
        if(!lp.getFontSize)lp.getFontSize=lp.getSystemFontSize;
        if(!lp.setFontSize)lp.setFontSize=lp.setSystemFontSize;
        var dimensions=lp.setDimensions;
        lp.setDimensions=function (width,height) { return typeof width==='object'?dimensions.call(this,width.width,width.height):dimensions.call(this,width,height); };
        lp._getBoundingWidth=function () { return this.getDimensions().width; };
        lp._getBoundingHeight=function () { return this.getDimensions().height; };
        lp._setBoundingWidth=function (width) { this.setDimensions(width,this.getDimensions().height); };
        lp._setBoundingHeight=function (height) { this.setDimensions(this.getDimensions().width,height); };
        lp._getFont=function () { return this.getFontSize()+"px '"+this.getFontName()+"'"; };
        lp._setFont=function (font) {
            var parts=/^(\d+)px\s+['"]?([\w\s\d]+)['"]?$/.exec(font);
            if(parts){this.setFontSize(+parts[1]);this.setFontName(parts[2]);}
        };
    }
    if(cc.Sprite) {
        cc.Sprite.prototype.getDirty=cc.Sprite.prototype.isDirty;
        cc.Sprite.prototype._getOffsetX=function () { return this.getOffsetPosition().x; };
        cc.Sprite.prototype._getOffsetY=function () { return this.getOffsetPosition().y; };
    }
    if(cc.ClippingNode)cc.ClippingNode.prototype.getInverted=cc.ClippingNode.prototype.isInverted;
    if(cc.Menu)cc.Menu.prototype.getEnabled=cc.Menu.prototype.isEnabled;
    if(cc.ParticleSystem)cc.ParticleSystem.prototype.getAutoRemoveOnFinish=cc.ParticleSystem.prototype.isAutoRemoveOnFinish;
    if(cc.Texture2D) {
        var texture=cc.Texture2D.prototype;
        texture.getPixelsWide=texture.getWidth;texture.getPixelsHigh=texture.getHeight;
        texture._getWidth=function () { return this.getContentSize().width; };
        texture._getHeight=function () { return this.getContentSize().height; };
    }
    if(cc.MenuItemFont) {
        var fontItem=cc.MenuItemFont.prototype;
        fontItem.fontSize=fontItem.getFontSizeObj;fontItem.setFontSize=fontItem.setFontSizeObj;
        fontItem.fontName=fontItem.getFontNameObj;fontItem.setFontName=fontItem.setFontNameObj;
    }

    // These are the same native classes under Axmol's current names.
    var aliases={Sprite3D:'MeshRenderer',Sprite3DCache:'MeshRendererCache',Animation3D:'Animation3D',
        Animate3D:'Animate3D',AttachNode:'AttachNode',BillBoard:'BillBoard',Bundle3D:'Bundle3D',
        Mesh:'Mesh',MeshVertexData:'MeshVertexData',Skin:'MeshSkin',Skeleton3D:'Skeleton3D',Bone3D:'Bone3D',
        Terrain:'Terrain',Skybox:'Skybox',AmbientLight:'AmbientLight',DirectionLight:'DirectionLight',
        PointLight:'PointLight',SpotLight:'SpotLight',NavMesh:'NavMesh',NavMeshAgent:'NavMeshAgent',
        NavMeshObstacle:'NavMeshObstacle',ParticleSystem3D:'ParticleSystem3D',PUParticleSystem3D:'PUParticleSystem3D'};
    Object.keys(aliases).forEach(function (name) { if(cc[aliases[name]])jsb[name]=cc[aliases[name]]; });
    if(jsb.Sprite3D && jsb._loadMeshAsync) {
        jsb.Sprite3D.createAsync=function (model,texture,callback,target,data) {
            if(typeof texture==='function'){data=target;target=callback;callback=texture;texture='';}
            if(typeof callback!=='function')throw new TypeError('Sprite3D.createAsync requires a callback');
            jsb._loadMeshAsync(model,texture||'',function (mesh) { return callback.call(target,mesh,data); });
        };
        jsb.sprite3DCache={removeAllSprite3DData:jsb._clearMeshDataCache};
        jsb.Sprite3DCache={getInstance:function () { return jsb.sprite3DCache; }};
        if(jsb._stopAsyncTasks) {
            var asyncPool={stopTasks:function (type) { return jsb._stopAsyncTasks(type); }};
            cc.AsyncTaskPool={TaskType:{TASK_IO:0,TASK_NETWORK:1,TASK_OTHER:2,TASK_MAX_TYPE:3},
                getInstance:function () { return asyncPool; }};
        }
    }
    if(jsb.Terrain) {
        jsb.Terrain.CrackFixedType={SKIRT:0,INCREASE_LOWER:1};
        jsb.Terrain.DetailMap=function (file,size) { this.file=file;this.size=size===undefined?35:size; };
        jsb.Terrain.detailMap=function (file,size) { return new jsb.Terrain.DetailMap(file,size); };
        jsb.Terrain.TerrainData=function (heightMap,alphaMap,detailMap,chunkSize,mapHeight,mapScale) {
            this.heightMap=heightMap;this.alphaMap=alphaMap;this.detailMap=detailMap;
            this.chunkSize=chunkSize===undefined?cc.size(32,32):chunkSize;
            this.mapHeight=mapHeight===undefined?2:mapHeight;this.mapScale=mapScale===undefined?0.1:mapScale;
        };
        jsb.Terrain.terrainData=function (heightMap,alphaMap,detailMap,chunkSize,mapHeight,mapScale) {
            return new jsb.Terrain.TerrainData(heightMap,alphaMap,detailMap,chunkSize,mapHeight,mapScale);
        };
    }
    if (cc.Camera) {
        cc.Camera.Mode={DEFAULT:0,PERSPECTIVE:1,ORTHOGRAPHIC:2};
        cc.Camera.createPerspective=function (fov,aspect,near,far) {
            var camera=cc.Camera.create();
            if(!camera.configurePerspective(fov,aspect,near,far))throw Error('Invalid perspective projection');
            return camera;
        };
        cc.Camera.createOrthographic=function (width,height,near,far) {
            var camera=cc.Camera.create();
            if(!camera.configureOrthographic(width,height,near,far))throw Error('Invalid orthographic projection');
            return camera;
        };
    }
    cc.visibleRect={init:function () {
        var origin=cc.director.getVisibleOrigin(),size=cc.director.getVisibleSize();
        var x=origin.x,y=origin.y,w=size.width===undefined?size.x:size.width,h=size.height===undefined?size.y:size.height;
        this.width=w;this.height=h;this.left=cc.p(x,y+h/2);this.right=cc.p(x+w,y+h/2);
        this.top=cc.p(x+w/2,y+h);this.bottom=cc.p(x+w/2,y);this.center=cc.p(x+w/2,y+h/2);
        this.topLeft=cc.p(x,y+h);this.topRight=cc.p(x+w,y+h);this.bottomLeft=cc.p(x,y);this.bottomRight=cc.p(x+w,y);
    }};
    // Resolve against the current native view each time init is called after a resize.
    if(cc.director.getVisibleOrigin && cc.director.getVisibleSize)cc.visibleRect.init();
    // Match v3 jsb_boot.js: refresh cached geometry after the native view applies
    // its resolution policy. winSize already reads the native Director live.
    cc.view._setDesignResolutionSize = cc.view.setDesignResolutionSize;
    cc.view.setDesignResolutionSize = function (width, height, policy) {
        cc.view._setDesignResolutionSize(width, height, policy);
        cc.visibleRect.init();
    };
    cc.view.setRealPixelResolution = cc.view.setDesignResolutionSize;
    cc.view.setResolutionPolicy = function (policy) {
        var size = cc.view.getDesignResolutionSize();
        cc.view.setDesignResolutionSize(size.width, size.height, policy);
    };
})(this);
