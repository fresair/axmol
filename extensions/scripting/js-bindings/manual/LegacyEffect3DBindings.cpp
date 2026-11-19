/****************************************************************************
 Copyright (c) 2014-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2026 Axmol contributors.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 Adapted from cocos2d-x/v3 tests/js-tests/project/Classes/js_Effect3D_bindings.cpp
 at e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f. Its original GLSL sources are
 preserved in the imported fixture and compiled by legacy-gl/compile.py.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#include "manual/LegacyEffect3DBindings.h"
#include "manual/LegacyGLBindings.h"
#include "manual/ManualUtils.h"
#if AX_ENABLE_3D
#include "axmol/3d/Mesh.h"
#include "axmol/3d/MeshRenderer.h"
#include "axmol/base/RefPtr.h"
#include "axmol/renderer/Material.h"
#include "axmol/renderer/Pass.h"
#include "axmol/renderer/ProgramManager.h"
#include "axmol/renderer/Technique.h"
#include "axmol/rhi/ProgramState.h"
#include <algorithm>
#endif

namespace axjs
{
namespace
{
#if AX_ENABLE_3D
class Effect3DOutline final : public ax::Object
{
public:
    bool init() { return true; }
    static Effect3DOutline* create()
    {
        auto* effect=new Effect3DOutline;
        effect->autorelease();
        return effect;
    }
    void setOutlineColor(const ax::Vec3& color) { _color=color; updatePasses(); }
    const ax::Vec3& getOutlineColor() const { return _color; }
    void setOutlineWidth(float width) { _width=width; updatePasses(); }
    float getOutlineWidth() const { return _width; }

    ax::Pass* createPass(ax::Technique* technique,ax::Mesh* mesh)
    {
        if(!mesh->hasVertexAttrib(ax::MeshVertexAttribute::POSITION) ||
           !mesh->hasVertexAttrib(ax::MeshVertexAttribute::NORMAL) ||
           (mesh->getSkin() && (!mesh->hasVertexAttrib(ax::MeshVertexAttribute::BLENDWEIGHT) ||
                               !mesh->hasVertexAttrib(ax::MeshVertexAttribute::BLENDINDICES))))
            return nullptr;
        auto* files=ax::FileUtils::getInstance();
        const auto vertexSource=files->getStringFromFile(mesh->getSkin() ?
            "Shaders3D/SkinnedOutline.vert" : "Shaders3D/OutLine.vert");
        const auto fragmentSource=files->getStringFromFile("Shaders3D/OutLine.frag");
        std::string vertexArchive,fragmentArchive;
        if(!findLegacyShaderArchives(vertexSource,fragmentSource,"",vertexArchive,fragmentArchive)) return nullptr;
        auto* program=axpm->loadProgram(vertexArchive,fragmentArchive);
        if(!program) return nullptr;
        auto* state=new ax::rhi::ProgramState(program);
        auto* pass=ax::Pass::createWithProgramState(technique,state);
        state->release();
        if(!pass) return nullptr;
        pass->setName("legacy_outline");
        auto& renderState=pass->getStateBlock();
        renderState.setCullFace(true);
        renderState.setCullFaceSide(ax::CullFaceSide::FRONT);
        renderState.setDepthTest(true);
        // The source's glDepthMask is the normal opaque-mesh mask. Preserve
        // the material's alpha/depth-write policy rather than forcing a mask.
        renderState.setDepthFunc(ax::DepthFunc::LESS);
        apply(state);
        _passes.emplace_back(pass);
        return pass;
    }
private:
    void apply(ax::rhi::ProgramState* state)
    {
        state->setUniform(state->getUniformLocation("OutLineColor"),&_color,sizeof(_color));
        state->setUniform(state->getUniformLocation("OutlineWidth"),&_width,sizeof(_width));
    }
    void updatePasses()
    {
        std::erase_if(_passes,[this](const ax::WeakPtr<ax::Pass>& weak) {
            if(auto* pass=weak.get()) { apply(pass->getProgramState()); return false; }
            return true;
        });
    }
    ax::Vec3 _color{1,1,1};
    float _width{1};
    std::vector<ax::WeakPtr<ax::Pass>> _passes;
};

// A real Material with an initially empty Technique lets us insert passes
// before and after the model without exposing or mutating native internals.
class EffectMaterial final : public ax::Material
{
public:
    explicit EffectMaterial(ax::Material* original)
    {
        setTransparent(original->isTransparent());
        setForce2DQueue(original->isForce2DQueue());
        setStateBlock(original->getStateBlock());
        setPrimitiveType(original->getPrimitiveType());
        setName("legacy_effect_3d");
        auto* technique=ax::Technique::create(this);
        technique->getStateBlock()=original->getTechnique()->getStateBlock();
        addTechnique(technique);
        setTechnique(technique->getName());
    }
    ~EffectMaterial() override=default;
};

class EffectSprite3D final : public ax::MeshRenderer
{
    struct Effect
    {
        int order;
        ax::RefPtr<Effect3DOutline> value;
    };
    struct OriginalMesh
    {
        ax::WeakPtr<ax::MeshRenderer> renderer;
        ax::WeakPtr<ax::Mesh> mesh;
        int index;
        ax::RefPtr<ax::Material> material;
    };
public:
    static EffectSprite3D* create(std::string_view path={},std::string_view texture={})
    {
        auto* sprite=new EffectSprite3D;
        if(path.empty() ? sprite->init() : sprite->initializeModel(path,texture))
        {
            sprite->autorelease();
            return sprite;
        }
        delete sprite;
        return nullptr;
    }
    bool initializeModel(std::string_view path,std::string_view texture={})
    {
        _originals.clear();
        if(!ax::MeshRenderer::initWithFile(path)) return false;
        if(!texture.empty()) setTexture(texture);
        captureOriginals(this);
        return rebuildMaterials();
    }
    bool addEffect(Effect3DOutline* effect,int order)
    {
        if(!effect) return true;
        _effects.push_back({order,ax::RefPtr<Effect3DOutline>(effect)});
        std::stable_sort(_effects.begin(),_effects.end(),[](const Effect& a,const Effect& b) {return a.order<b.order;});
        if(rebuildMaterials()) return true;
        // A failed preflight never installs a partially assembled material.
        const auto found=std::find_if(_effects.begin(),_effects.end(),[effect,order](const Effect& entry) {
            return entry.value.get()==effect && entry.order==order;
        });
        if(found!=_effects.end()) _effects.erase(found);
        return false;
    }
    bool setEffect3D(Effect3DOutline* effect)
    {
        auto previous=_default;
        _default=ax::RefPtr<Effect3DOutline>(effect);
        if(rebuildMaterials()) return true;
        _default=std::move(previous);
        return false;
    }
private:
    void captureOriginals(ax::MeshRenderer* renderer)
    {
        for(int index=0;index<renderer->getMeshCount();++index)
        {
            auto* mesh=renderer->getMeshByIndex(index);
            _originals.push_back({ax::WeakPtr<ax::MeshRenderer>(renderer),ax::WeakPtr<ax::Mesh>(mesh),index,
                                  ax::RefPtr<ax::Material>(mesh->getMaterial())});
        }
        for(auto* child:renderer->getChildren())
            if(auto* nested=dynamic_cast<ax::MeshRenderer*>(child)) captureOriginals(nested);
    }
    bool rebuildMaterials()
    {
        if(_originals.empty()) captureOriginals(this);
        if(_originals.empty()) return _effects.empty() && !_default;
        std::vector<ax::RefPtr<EffectMaterial>> prepared;
        prepared.reserve(_originals.size());
        for(const auto& original:_originals)
        {
            auto* mesh=original.mesh.get();
            if(!mesh || !original.renderer || !original.material || !original.material->getTechnique()) return false;
            ax::RefPtr<EffectMaterial> material(new EffectMaterial(original.material.get()),tlx::adopt_object);
            auto* technique=material->getTechnique();
            auto addOutline=[&](Effect3DOutline* effect) {
                auto* pass=effect->createPass(technique,mesh);
                if(!pass) return false;
                technique->addPass(pass);
                return true;
            };
            for(const auto& entry:_effects)
                if(entry.order<0 && !addOutline(entry.value.get())) return false;
            if(_default)
            {
                if(!addOutline(_default.get())) return false;
            }
            else
            {
                for(auto* originalPass:original.material->getTechnique()->getPasses())
                {
                    auto* pass=originalPass->clone();
                    pass->setTechnique(technique);
                    technique->addPass(pass);
                }
            }
            for(const auto& entry:_effects)
                if(entry.order>0 && !addOutline(entry.value.get())) return false;
            prepared.push_back(std::move(material));
        }
        for(size_t index=0;index<_originals.size();++index)
            _originals[index].renderer->setMaterial(prepared[index].get(),_originals[index].index);
        // Mesh::draw uploads model color and current skin palette into every
        // Pass; Pass updates camera/MVP and restores its actual RHI state.
        return true;
    }
    std::vector<Effect> _effects;
    ax::RefPtr<Effect3DOutline> _default;
    std::vector<OriginalMesh> _originals;
};

template<auto Method>
bool nativeMethod(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(invoke<Method>(cx,args)) return true;
    return manual::error(cx,"Invalid Effect3D native arguments");
}
template<class T> bool allocate(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    return constructUninitialized<T>(cx,args);
}
bool outlineConstructor(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc) return manual::error(cx,"Effect3DOutline constructor takes no arguments");
    return construct<Effect3DOutline>(cx,args);
}
bool spriteFactory(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    std::string model,texture;
    if(argc>2 || (argc && !manual::argument(cx,args,0,model)) || (argc==2 && !manual::argument(cx,args,1,texture)))
        return manual::error(cx,"EffectSprite3D requires a model and optional texture path");
    auto* sprite=argc && model.empty() ? nullptr : EffectSprite3D::create(model,texture);
    if(!sprite && args.isConstructing()) return manual::error(cx,"EffectSprite3D model initialization failed");
    return toJsValue(cx,sprite,args.rval());
}
template<bool Constructor>
bool spriteInit(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* sprite=manual::self<EffectSprite3D>(cx,args);
    std::string model,texture;
    if(!sprite) return false;
    if constexpr(Constructor)
        if(argc==0) { args.rval().setBoolean(sprite->init()); return true; }
    if(argc<1 || argc>2 || !manual::argument(cx,args,0,model) ||
       (argc==2 && !manual::argument(cx,args,1,texture))) return false;
    args.rval().setBoolean(sprite->initializeModel(model,texture));
    return true;
}
template<bool Add>
bool setEffect(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* sprite=manual::self<EffectSprite3D>(cx,args);
    Effect3DOutline* effect{};
    int order{};
    if(!sprite || argc!=(Add ? 2u : 1u) || !manual::argument(cx,args,0,effect) ||
       (Add && !manual::argument(cx,args,1,order))) return false;
    if(!(Add ? sprite->addEffect(effect,order) : sprite->setEffect3D(effect)))
        return manual::error(cx,"Outline requires real mesh normals, skin attributes and compiled original shaders");
    args.rval().setUndefined();
    return true;
}
template<class T> JSClass& nativeClass(const char* name)
{
    static JSClass type=[] {JSClass value{};value.flags=JSCLASS_HAS_PRIVATE;value.addProperty=JS_PropertyStub;
        value.delProperty=JS_DeletePropertyStub;value.getProperty=JS_PropertyStub;value.setProperty=JS_StrictPropertyStub;
        value.enumerate=JS_EnumerateStub;value.resolve=JS_ResolveStub;value.convert=JS_ConvertStub;
        value.finalize=finalizeNative;return value;}();
    type.name=name;
    return type;
}
#endif
} // namespace

bool registerLegacyEffect3D(JSContext* cx,JS::HandleObject global)
{
#if AX_ENABLE_3D
    JS::RootedObject cc(cx,manual::namespaceObject(cx,global,"cc"));
    JS::RootedObject objectPrototype(cx,getPrototype<ax::Object>(cx)),meshPrototype(cx,getPrototype<ax::MeshRenderer>(cx));
    if(!cc || !objectPrototype || !meshPrototype) return false;
    const JSFunctionSpec outlineMethods[]={
        JS_FN("init",nativeMethod<&Effect3DOutline::init>,0,0),
        JS_FN("setOutlineColor",nativeMethod<&Effect3DOutline::setOutlineColor>,1,0),
        JS_FN("getOutlineColor",nativeMethod<&Effect3DOutline::getOutlineColor>,0,0),
        JS_FN("setOutlineWidth",nativeMethod<&Effect3DOutline::setOutlineWidth>,1,0),
        JS_FN("getOutlineWidth",nativeMethod<&Effect3DOutline::getOutlineWidth>,0,0),JS_FS_END};
    const JSFunctionSpec outlineStatics[]={JS_FN("create",nativeMethod<&Effect3DOutline::create>,0,0),
        JS_FN("__allocate",allocate<Effect3DOutline>,0,0),JS_FS_END};
    auto& outlineClass=nativeClass<Effect3DOutline>("Effect3DOutline");
    JS::RootedObject outlinePrototype(cx,JS_InitClass(cx,cc,objectPrototype,&outlineClass,outlineConstructor,0,
        nullptr,outlineMethods,nullptr,outlineStatics));
    if(!outlinePrototype || !registerType<Effect3DOutline,ax::Object>(cx,outlinePrototype,&outlineClass)) return false;
    const JSFunctionSpec spriteMethods[]={JS_FN("ctor",spriteInit<true>,0,0),
        JS_FN("initWithFile",spriteInit<false>,1,0),
        JS_FN("addEffect",setEffect<true>,2,0),JS_FN("setEffect3D",setEffect<false>,1,0),JS_FS_END};
    const JSFunctionSpec spriteStatics[]={JS_FN("create",spriteFactory,0,0),
        JS_FN("createFromObjFileAndTexture",spriteFactory,2,0),
        JS_FN("__allocate",allocate<EffectSprite3D>,0,0),JS_FS_END};
    auto& spriteClass=nativeClass<EffectSprite3D>("EffectSprite3D");
    JS::RootedObject spritePrototype(cx,JS_InitClass(cx,cc,meshPrototype,&spriteClass,spriteFactory,0,
        nullptr,spriteMethods,nullptr,spriteStatics));
    JS::RootedValue legacyConstructor(cx,JS::BooleanValue(true));
    return spritePrototype && registerType<EffectSprite3D,ax::MeshRenderer>(cx,spritePrototype,&spriteClass) &&
        JS_DefineProperty(cx,spritePrototype,"__legacyCtor",legacyConstructor,JSPROP_READONLY);
#else
    return true;
#endif
}
} // namespace axjs
