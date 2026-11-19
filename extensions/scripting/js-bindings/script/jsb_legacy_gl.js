// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Texture descriptors adapted from cocos2d-x/v3 jsb_cocos2d.js:177-181,
// 2595-2611, e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f.
// Copyright (c) 2013-2016 Chukong Technologies Inc.
// Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Original MIT notice: tools/bindings-generator/js/legacy-gl/cocos2d-x-MIT-LICENSE.txt.
/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
// The functions and GLNode below are native RHI bindings. These descriptors
// expose the spelling and numeric constants used by the original v3 scripts.
(function(global) {
    if(!global.gl||!cc.GLNode||!cc.GLProgram)return;
    cc._renderContext=global.gl;
    cc.LINEAR=gl.LINEAR;cc.REPEAT=gl.REPEAT;
    cc.CLAMP_TO_EDGE=gl.CLAMP_TO_EDGE;cc.MIRRORED_REPEAT=gl.MIRRORED_REPEAT;
    gl.FALSE=0;gl.TRUE=1;gl.FRONT=0x0404;gl.BACK=0x0405;
    gl.INT=0x1404;gl.UNSIGNED_SHORT=0x1403;
    // Original jsb_cocos2d.js slots and CCGLProgram.h's extended enum.
    // These are GL binding locations, distinct from an RHI shader's chosen
    // locations for its native 3D vertex semantics.
    cc.VERTEX_ATTRIB_POSITION=0;cc.VERTEX_ATTRIB_COLOR=1;
    cc.VERTEX_ATTRIB_TEX_COORD=cc.VERTEX_ATTRIB_TEX_COORDS=2;
    cc.VERTEX_ATTRIB_TEX_COORD1=3;cc.VERTEX_ATTRIB_TEX_COORD2=4;cc.VERTEX_ATTRIB_TEX_COORD3=5;
    cc.VERTEX_ATTRIB_NORMAL=6;cc.VERTEX_ATTRIB_BLEND_WEIGHT=7;cc.VERTEX_ATTRIB_BLEND_INDEX=8;
    cc.VERTEX_ATTRIB_TANGENT=9;cc.VERTEX_ATTRIB_BINORMAL=10;cc.VERTEX_ATTRIB_MAX=3;
    cc.VERTEX_ATTRIB_FLAG_NONE=0;cc.VERTEX_ATTRIB_FLAG_POSITION=1;
    cc.VERTEX_ATTRIB_FLAG_COLOR=2;cc.VERTEX_ATTRIB_FLAG_TEX_COORDS=4;
    cc.VERTEX_ATTRIB_FLAG_POS_COLOR_TEX=7;
    cc.attributeNames=['a_position','a_color','a_texCoord','a_texCoord1','a_texCoord2','a_texCoord3',
        'a_normal','a_blendWeight','a_blendIndex'];
    cc.defineGetterSetter(cc.Node.prototype,'shaderProgram',
        cc.Node.prototype.getShaderProgram,cc.Node.prototype.setShaderProgram);
    var formats={AUTO:0,DEFAULT:0,BGRA8888:1,RGBA8888:2,RGB888:3,RGB565:4,
        A8:5,I8:6,AI88:7,RGBA4444:8,RGB5A1:9,PVRTC4:10,PVRTC4A:11,
        PVRTC2:12,PVRTC2A:13,ETC:14,S3TC_DXT1:15,S3TC_DXT3:16,S3TC_DXT5:17,
        ATC_RGB:18,ATC_EXPLICIT_ALPHA:19,ATC_INTERPOLATED_ALPHA:20,NONE:-1};
    if(cc.Texture2D)Object.keys(formats).forEach(function(name) {
        cc.Texture2D['PIXEL_FORMAT_'+name]=formats[name];
        cc['TEXTURE_2D_PIXEL_FORMAT_'+name]=formats[name];
    });
    if(cc.Texture2D) {
        var texturePrototype=cc.Texture2D.prototype;
        var nativeTextureParameters=texturePrototype.setTexParameters;
        var nativeTextureHandle=texturePrototype.getName;
        texturePrototype._setTexParameters=nativeTextureParameters;
        texturePrototype.setTexParameters=function(texParams,magFilter,wrapS,wrapT) {
            var minFilter;
            if(magFilter===undefined) {
                minFilter=texParams.minFilter;magFilter=texParams.magFilter;
                wrapS=texParams.wrapS;wrapT=texParams.wrapT;
            } else minFilter=texParams;
            return nativeTextureParameters.call(this,minFilter,magFilter,wrapS,wrapT);
        };
        texturePrototype.handleLoadedTexture=function(premultiplied) {
            // Native textureCache.addImage has already decoded and uploaded the
            // image. The original v3 native bridge deliberately did nothing
            // here, unlike the browser's asynchronous image-load completion.
            // Validate the native receiver without rewriting image data or its
            // actual premultiplication flag.
            nativeTextureHandle.call(this);
        };
    }
    function installLegacyDynamicSampler(Type,prepare,initMethods) {
        if(!Type || !prepare)return;
        var prototype=Type.prototype;
        function configure(node) {
            if(node)prepare(node);
            return node;
        }
        // Real native factory calls can initialize these nodes without entering
        // their JS prototypes' init methods. Adapt the constructor's factories
        // as well as .extend()'s typed init path.
        var Native=Object.getPrototypeOf(Type);
        if(!Native || Native.prototype!==prototype)Native=Type;
        Object.getOwnPropertyNames(Native).forEach(function(name) {
            if(name.indexOf('create')!==0 || typeof Native[name]!=='function')return;
            var native=Native[name];
            if(native.__legacyDynamicSampler)return;
            var adapted=function() {return configure(native.apply(this,arguments));};
            adapted.__legacyDynamicSampler=true;
            Native[name]=adapted;
        });
        initMethods.forEach(function(name) {
            var native=prototype[name];
            if(typeof native!=='function' || native.__legacyDynamicSampler)return;
            var adapted=function() {
                var result=native.apply(this,arguments);
                if(result)configure(this);
                return result;
            };
            adapted.__legacyDynamicSampler=true;
            prototype[name]=adapted;
        });
        var nativeSetTexture=prototype.setTexture;
        // LabelAtlas inherits AtlasNode's adapted methods; keep that single
        // wrapper instead of adding another layer on the derived prototype.
        if(typeof nativeSetTexture==='function' && !nativeSetTexture.__legacyDynamicSampler) {
            var adaptedSetTexture=function() {
                var priorState=typeof this.getGLProgramState==='function'?this.getGLProgramState():null;
                var preserveCustomState=!!(priorState && this.__legacyBatchSamplerState && priorState!==this.__legacyBatchSamplerState);
                var result=nativeSetTexture.apply(this,arguments);
                if(preserveCustomState)this.setGLProgramState(priorState);
                else configure(this);
                return result;
            };
            adaptedSetTexture.__legacyDynamicSampler=true;
            prototype.setTexture=adaptedSetTexture;
            var textureDesc=Object.getOwnPropertyDescriptor(prototype,'texture');
            if(textureDesc && typeof textureDesc.set==='function') {
                Object.defineProperty(prototype,'texture',{
                    get:textureDesc.get,
                    set:adaptedSetTexture,
                    enumerable:textureDesc.enumerable,
                    configurable:textureDesc.configurable
                });
            }
        }
    }
    installLegacyDynamicSampler(cc.Sprite,jsb._prepareLegacySprite,
        ['initWithFile','initWithTexture','initWithSpriteFrame','initWithSpriteFrameName','initWithPolygon']);
    installLegacyDynamicSampler(cc.AtlasNode,jsb._prepareLegacyAtlasNode,['initWithTileFile','initWithTexture']);
    installLegacyDynamicSampler(cc.LabelAtlas,jsb._prepareLegacyAtlasNode,['initWithTileFile','initWithTexture','initWithString']);
    if(cc.SpriteBatchNode && cc.Sprite) {
        var NativeSprite=Object.getPrototypeOf(cc.Sprite);
        if(!NativeSprite || NativeSprite.prototype!==cc.Sprite.prototype)NativeSprite=cc.Sprite;
        installLegacyDynamicSampler(cc.SpriteBatchNode,function(node) {
            if(!node || typeof node.getGLProgramState!=='function' || typeof node.setGLProgramState!=='function')return;
            var existing=node.getGLProgramState();
            if(!existing)return;
            var texture=typeof node.getTexture==='function'?node.getTexture():null;
            var probe=texture?cc.Sprite.createWithTexture(texture):
                (typeof NativeSprite.create==='function'?NativeSprite.create():null);
            var state=probe && typeof probe.getGLProgramState==='function'?probe.getGLProgramState():null;
            if(state) {
                node.setGLProgramState(state);
                Object.defineProperty(node,'__legacyBatchSamplerState',{
                    value:state,writable:true,configurable:true,enumerable:false
                });
            }
        },['init','initWithFile','initWithTexture']);
    }
})(this);
