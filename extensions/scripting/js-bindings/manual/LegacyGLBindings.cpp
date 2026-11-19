// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacyGLBindings.h"
#include "manual/LegacyGLShaderManifest.h"
#include "manual/LegacyGLBuiltinSources.h"
#include "manual/LegacyGLBuiltinVariants.h"
#include "manual/ManualUtils.h"
#include "axmol/base/RefPtr.h"
#include "axmol/2d/AtlasNode.h"
#include "axmol/2d/Sprite.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/CustomEvent.h"
#include "axmol/renderer/CustomCommand.h"
#include "axmol/renderer/Renderer.h"
#include "axmol/renderer/ProgramManager.h"
#include "axmol/renderer/VertexLayoutManager.h"
#include "axmol/renderer/Texture2D.h"
#include "axmol/renderer/TextureCube.h"
#include "axmol/renderer/RenderTexture.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/rhi/GraphicsContext.h"
#include "axmol/rhi/GraphicsDevice.h"
#include "axmol/rhi/ProgramState.h"
#include "axmol/rhi/RenderTarget.h"
#include "axmol/rhi/VertexLayout.h"
#if AX_ENABLE_3D
#include "axmol/3d/Mesh.h"
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace axjs::legacygl
{
using namespace manual;
using namespace ax;
namespace rhi = ax::rhi;
constexpr int ARRAY_BUFFER=0x8892, STATIC_DRAW=0x88e4, DYNAMIC_DRAW=0x88e8;
constexpr int VERTEX_SHADER=0x8b31, FRAGMENT_SHADER=0x8b30, COMPILE_STATUS=0x8b81, LINK_STATUS=0x8b82;
constexpr int TEXTURE_2D=0x0de1, RGBA=0x1908, RGB=0x1907, UNSIGNED_BYTE=0x1401, FLOAT=0x1406;
constexpr int TEXTURE_MAG_FILTER=0x2800, TEXTURE_MIN_FILTER=0x2801, TEXTURE_WRAP_S=0x2802, TEXTURE_WRAP_T=0x2803;
constexpr int NEAREST=0x2600, LINEAR=0x2601, CLAMP_TO_EDGE=0x812f, REPEAT=0x2901, MIRRORED_REPEAT=0x8370;

const ShaderArchive* archiveFor(std::string_view source,bool vertex)
{
    for(const auto& archive:archives)
        if(archive.vertex==vertex && archive.source==source && archive.defines.empty()) return &archive;
    return nullptr;
}
std::string_view uniformName(std::string_view name,const rhi::Program* program=nullptr)
{
    if(name=="CC_MVPMatrix")return "u_MVPMatrix";
    if(name=="CC_PMatrix")return program&&program->getUniformLocation("u_PMatrix")?"u_PMatrix":"u_MVPMatrix";
    if(name=="CC_MVMatrix")return "u_MVMatrix";
    if(name=="CC_NormalMatrix")return "u_NormalMatrix";
    if(name=="CC_Texture"||name=="CC_Texture0")return "u_tex0";
    return name;
}
std::optional<rhi::VertexSemantic> attributeSemantic(std::string_view name)
{
    if(name=="a_position"||name=="aVertexPosition")return rhi::VertexSemantic::POSITION;
    if(name=="a_color"||name=="aVertexColor")return rhi::VertexSemantic::COLOR0;
    if(name=="a_normal")return rhi::VertexSemantic::NORMAL;
    if(name=="a_blendWeight")return rhi::VertexSemantic::BLENDWEIGHT;
    if(name=="a_blendIndex")return rhi::VertexSemantic::BLENDINDICES;
    if(name=="a_tangent")return rhi::VertexSemantic::TANGENT;
    if(name=="a_binormal")return rhi::VertexSemantic::BINORMAL;
    if(name=="a_texCoord")return rhi::VertexSemantic::TEXCOORD0;
    if(name=="a_texCoord1")return rhi::VertexSemantic::TEXCOORD1;
    if(name=="a_texCoord2")return rhi::VertexSemantic::TEXCOORD2;
    if(name=="a_texCoord3")return rhi::VertexSemantic::TEXCOORD3;
    return std::nullopt;
}
struct LegacyAttributeBinding
{
    const rhi::VertexSemantic* semantic;
    const char* name;
};
const std::array<LegacyAttributeBinding,11>& legacyAttributeBindings()
{
    // CCGLProgram's public GL slots are stable across shaders. RHI locations
    // belong to each compiled shader and can differ, e.g. the native Texture
    // layout puts TEXCOORD0 at location 1 rather than the old GL slot 2.
    static const std::array<LegacyAttributeBinding,11> bindings{{
        {&rhi::VertexSemantic::POSITION,"a_position"},
        {&rhi::VertexSemantic::COLOR0,"a_color"},
        {&rhi::VertexSemantic::TEXCOORD0,"a_texCoord"},
        {&rhi::VertexSemantic::TEXCOORD1,"a_texCoord1"},
        {&rhi::VertexSemantic::TEXCOORD2,"a_texCoord2"},
        {&rhi::VertexSemantic::TEXCOORD3,"a_texCoord3"},
        {&rhi::VertexSemantic::NORMAL,"a_normal"},
        {&rhi::VertexSemantic::BLENDWEIGHT,"a_blendWeight"},
        {&rhi::VertexSemantic::BLENDINDICES,"a_blendIndex"},
        {&rhi::VertexSemantic::TANGENT,"a_tangent"},
        {&rhi::VertexSemantic::BINORMAL,"a_binormal"},
    }};
    return bindings;
}
int legacyAttributeLocation(const rhi::VertexSemantic& semantic)
{
    const auto& bindings=legacyAttributeBindings();
    for(size_t index=0;index<bindings.size();++index)
        if(semantic==*bindings[index].semantic)return static_cast<int>(index);
    return -1;
}
std::string legacyAttributeName(const rhi::VertexSemantic& semantic)
{
    const int index=legacyAttributeLocation(semantic);
    return index<0?std::string(semantic.getName()):legacyAttributeBindings()[index].name;
}
const rhi::UniformInfo* uniformInfo(const rhi::Program* program,const rhi::UniformLocation& location)
{
    if(program)for(const auto& [id,info]:program->getActiveUniformInfos())
    {
        AX_UNUSED_PARAM(id);
        if(info.location==location.location&&info.cpuOffset==location.cpuOffset&&info.offset==location.offset)return &info;
    }
    return nullptr;
}
unsigned uniformComponents(uint16_t type)
{
    switch(type)
    {
    case axslc::SC_TYPE_FLOAT:case axslc::SC_TYPE_INT:return 1;
    case axslc::SC_TYPE_FLOAT2:case axslc::SC_TYPE_INT2:return 2;
    case axslc::SC_TYPE_FLOAT3:case axslc::SC_TYPE_INT3:return 3;
    case axslc::SC_TYPE_FLOAT4:case axslc::SC_TYPE_INT4:return 4;
    case axslc::SC_TYPE_MAT3:return 9;
    case axslc::SC_TYPE_MAT4:return 16;
    default:return 0;
    }
}
bool writeUniform(rhi::ProgramState* ps,std::string_view name,const void* data,size_t size,uint16_t type)
{
    rhi::UniformLocationVector locations;ps->getUniformLocations(uniformName(name,ps->getProgram()),locations);
    if(locations.empty())return false;
    for(const auto& location:locations)
    {
        const auto* info=uniformInfo(ps->getProgram(),location);
        if(!info||info->varType!=type||location.offset<0||location.cpuOffset<0||size>info->sizeBytes)return false;
        const size_t offset=static_cast<size_t>(location.cpuOffset)+static_cast<size_t>(location.offset);
        if(offset>ps->getUniformBuffer().size()||size>ps->getUniformBuffer().size()-offset)return false;
    }
    for(const auto& location:locations)ps->setUniform(location,data,size);
    return true;
}

constexpr std::string_view legacyFragmentCoordinateUniform="_axLegacyFragCoordY";
std::array<float,2> legacyFragmentCoordinateTransform()
{
    const auto language=rhi::GraphicsCore::shaderLanguage();
    const bool topLeft=language==axslc::SHADER_LANG_SPIRV || language==axslc::SHADER_LANG_MSL ||
                       language==axslc::SHADER_LANG_HLSL;
    if(!topLeft)return {1.0f,0.0f};
    auto* director=Director::getInstance();
    auto* renderer=director->getRenderer();
    auto* target=renderer?renderer->getRenderTarget():nullptr;
    float height=0;
    if(target&&!target->isDefaultRenderTarget())
    {
        for(const auto& attachment:target->_color)
            if(attachment.texture)
            {
                height=std::max(1,attachment.texture->getHeight()>>std::min<unsigned>(attachment.level,31));
                break;
            }
        if(height==0&&target->_depthStencil.texture)
            height=std::max(1,target->_depthStencil.texture->getHeight()>>std::min<unsigned>(target->_depthStencil.level,31));
    }
    else if(auto* view=director->getRenderView())height=view->getRenderSize().y;
    return {-1.0f,height};
}
void configureLegacyFragmentCoordinates(rhi::ProgramState* ps)
{
    rhi::UniformLocationVector locations;
    ps->getUniformLocations(legacyFragmentCoordinateUniform,locations);
    if(locations.empty())return;
    // This private native callback has no JS roots or VM dependency. It runs
    // when the actual draw is encoded, after RenderTexture groups select their
    // active framebuffer, rather than during scene traversal.
    ps->setCallbackUniform(locations.front(),[locations](rhi::ProgramState* executing,const rhi::UniformLocation&) {
        const auto value=legacyFragmentCoordinateTransform();
        for(const auto& location:locations)executing->setUniform(location,value.data(),sizeof(value));
    });
}

class Buffer final:public Object
{
public:
    std::vector<uint8_t> bytes;
    rhi::BufferUsage usage=rhi::BufferUsage::STATIC;
};
class Shader final:public Object
{
public:
    bool vertex{};
    std::string source,path,diagnostic;
    RefPtr<rhi::ShaderModule> module;
    bool compile()
    {
        module.reset();path.clear();diagnostic.clear();
        const auto* archive=archiveFor(source,vertex);
        if(!archive) { diagnostic="No compiled RHI archive matches this exact legacy shader source. Compile and package it with the project.";return false; }
        if(!rhi::GraphicsCore::device()) { diagnostic="Shader compilation requires an initialized graphics device";return false; }
        auto data=FileUtils::getInstance()->getDataFromFile(archive->path);
        if(data.isNull()) {diagnostic="Compiled legacy shader archive is missing from the resource search path";return false;}
        auto* cache=rhi::ShaderCache::getInstance();
        module.reset(vertex?cache->acquireVertexShaderModule(data):cache->acquireFragmentShaderModule(data),tlx::adopt_object);
        if(!module||!module->isCompiled()) {diagnostic="Graphics backend rejected the compiled legacy shader archive";return false;}
        path=archive->path;return true;
    }
};
class Program;
class Uniform final:public Object
{
public:
    WeakPtr<Program> owner;
    std::string name;
    rhi::UniformLocationVector locations;
};
class Program final:public Object
{
public:
    RefPtr<Shader> vertex,fragment;
    RefPtr<rhi::Program> native;
    RefPtr<rhi::ProgramState> state;
    WeakPtr<rhi::ProgramState> borrowed;
    bool borrowedState{};
    std::map<std::string,int> bindings;
    std::unordered_map<std::string,RefPtr<Uniform>> uniforms;
    std::unordered_map<std::string,std::vector<float>> uniformValues;
    rhi::ProgramState* getState()const {return borrowedState?borrowed.get():state.get();}
    rhi::Program* getNative()const {auto* ps=getState();return borrowedState?(ps?ps->getProgram():nullptr):native.get();}
    bool link()
    {
        native.reset();state.reset();uniforms.clear();uniformValues.clear();
        if(!vertex||!fragment||!vertex->module||!fragment->module||!rhi::GraphicsCore::device())return false;
        const auto* inputs=archiveFor(vertex->source,true);
        if(!inputs)return false;
        // A Sprite-compatible layout uses the same native POSITION/TEXCOORD/
        // COLOR semantics. Imperative draws supply their own reflected layout.
        native.reset(ProgramManager::getInstance()->loadProgram(vertex->path,fragment->path,rhi::VertexLayoutKind::Sprite));
        if(!native||!native->isValid()) {native.reset();return false;}
        native->addUniformAlias("u_tex0","u_sampler0");
        if(fragment->source.find("u_sampler1")!=std::string::npos)
        {
            // Public names in the unchanged original Sprite3D JS cases. Their
            // original fragment shader resources call this sampler u_sampler1.
            if(fragment->source.find("duration")!=std::string::npos)native->addUniformAlias("caustics","u_sampler1");
            if(fragment->source.find("u_target_pos")!=std::string::npos)native->addUniformAlias("u_shadowTexture","u_sampler1");
        }
        state.reset(new rhi::ProgramState(native.get()),tlx::adopt_object);
        configureLegacyFragmentCoordinates(state.get());
        return true;
    }
    int attribute(std::string_view name)const
    {
        const auto semantic=attributeSemantic(name);
        auto* shader=getNative();if(!shader)return -1;
        if(!semantic)return -1;
        const auto* input=shader->getVertexInputDesc(*semantic);
        if(!input)return -1;
        const auto found=bindings.find(std::string(name));
        return found==bindings.end()?legacyAttributeLocation(*semantic):found->second;
    }
    Uniform* uniform(std::string_view name)
    {
        if(name==legacyFragmentCoordinateUniform)return nullptr;
        auto* shader=getNative();if(!shader)return nullptr;
        std::string key(uniformName(name,shader));
        auto found=uniforms.find(key);if(found!=uniforms.end())return found->second.get();
        auto item=RefPtr<Uniform>(new Uniform,tlx::adopt_object);
        item->owner=this;item->name=key;shader->getUniformLocations(key,item->locations);
        if(item->locations.empty())return nullptr;
        auto* result=item.get();uniforms.emplace(key,std::move(item));return result;
    }
    bool write(Uniform* uniform,std::span<const float> data)
    {
        auto* ps=getState();if(!uniform||uniform->owner.get()!=this||!ps)return false;
        for(const auto& location:uniform->locations)
        {
            const auto* info=uniformInfo(ps->getProgram(),location);
            if(!info)return false;
            const bool vector=info->varType<=axslc::SC_TYPE_FLOAT&&uniformComponents(info->varType)==data.size();
            const bool matrix=info->varType==axslc::SC_TYPE_MAT4&&data.size()==16;
            if(!vector&&!matrix)return false;
            if(location.offset<0||location.cpuOffset<0||data.size_bytes()>info->sizeBytes)return false;
            const size_t offset=static_cast<size_t>(location.offset)+static_cast<size_t>(location.cpuOffset);
            if(offset>ps->getUniformBuffer().size()||data.size_bytes()>ps->getUniformBuffer().size()-offset)return false;
        }
        for(const auto& location:uniform->locations)ps->setUniform(location,data.data(),data.size_bytes());
        uniformValues[uniform->name]=std::vector<float>(data.begin(),data.end());return true;
    }
};
std::vector<std::pair<std::string_view,const rhi::UniformInfo*>> activeUniforms(const rhi::Program* program)
{
    std::vector<std::string_view> names;program->getActiveUniformNames(names);
    std::vector<std::pair<std::string_view,const rhi::UniformInfo*>> result;
    for(auto name:names)
    {
        if(name==legacyFragmentCoordinateUniform)continue;
        rhi::UniformLocationVector locations;program->getUniformLocations(name,locations);
        if(!locations.empty())if(auto* info=uniformInfo(program,locations.front()))result.emplace_back(name,info);
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        const bool textureA=a.second->offset<0,textureB=b.second->offset<0;
        return textureA!=textureB?textureA>textureB:a.first<b.first;
    });
    return result;
}
int uniformGLType(const rhi::UniformInfo& info)
{
    switch(info.varType)
    {
    case axslc::SC_TYPE_FLOAT:return FLOAT;
    case axslc::SC_TYPE_FLOAT2:return 0x8b50;
    case axslc::SC_TYPE_FLOAT3:return 0x8b51;
    case axslc::SC_TYPE_FLOAT4:return 0x8b52;
    case axslc::SC_TYPE_INT:return 0x1404;
    case axslc::SC_TYPE_INT2:return 0x8b53;
    case axslc::SC_TYPE_INT3:return 0x8b54;
    case axslc::SC_TYPE_INT4:return 0x8b55;
    case axslc::SC_TYPE_MAT3:return 0x8b5b;
    case axslc::SC_TYPE_MAT4:return 0x8b5c;
    default:return info.varType==static_cast<uint16_t>(axslc::SC_TYPE_HALF)+static_cast<uint16_t>(axslc::DimCube)?0x8b60:0x8b5e;
    }
}
bool readUniform(JSContext* cx,Program* program,Uniform* uniform,JS::MutableHandleValue result)
{
    auto* ps=program->getState();
    if(!ps||!uniform||uniform->owner.get()!=program||uniform->locations.empty())return error(cx,"Expected a live uniform of this program");
    const auto& location=uniform->locations.front();const auto* info=uniformInfo(ps->getProgram(),location);
    if(!info)return error(cx,"Uniform reflection no longer exists");
    if(info->offset<0)
    {
        const auto& textures=ps->getTextureBindingSets();auto found=textures.find(info->location);
        result.setInt32(found==textures.end()||found->second.slots.empty()?0:found->second.slots.front());return true;
    }
    const unsigned components=uniformComponents(info->varType);const size_t count=std::max<uint16_t>(info->count,1);
    if(!components||location.cpuOffset<0||location.offset<0)return error(cx,"Uniform has an unsupported reflected type");
    const size_t offset=static_cast<size_t>(location.cpuOffset)+static_cast<size_t>(location.offset);
    const auto& buffer=ps->getUniformBuffer();
    if(offset>buffer.size()||info->sizeBytes>buffer.size()-offset)return error(cx,"Uniform reflection exceeds native buffer storage");
    const bool integer=info->varType>=axslc::SC_TYPE_INT4&&info->varType<=axslc::SC_TYPE_INT;
    const size_t payload=info->varType==axslc::SC_TYPE_MAT3?44:components*sizeof(uint32_t);
    // Native legacy archives use std140. Arrays have a minimum 16-byte stride;
    // mat3 uses three padded columns, while its last column may end at byte44.
    const size_t stride=count>1?(payload+15)&~size_t(15):info->sizeBytes;
    if((count-1)*stride+payload>info->sizeBytes)return error(cx,"Uniform reflection is too small");
    if(components==1&&count==1)
    {
        if(integer){int32_t value;std::memcpy(&value,buffer.data()+offset,sizeof(value));result.setInt32(value);}
        else{float value;std::memcpy(&value,buffer.data()+offset,sizeof(value));result.setDouble(JS::CanonicalizeNaN(value));}
        return true;
    }
    JS::RootedObject array(cx,integer?JS_NewInt32Array(cx,components*count):JS_NewFloat32Array(cx,components*count));
    if(!array)return false;
    auto* output=integer?static_cast<void*>(JS_GetInt32ArrayData(array)):static_cast<void*>(JS_GetFloat32ArrayData(array));
    for(size_t element=0;element<count;++element)
    {
        auto* destination=static_cast<uint8_t*>(output)+element*components*sizeof(uint32_t);
        const auto* source=buffer.data()+offset+element*stride;
        if(info->varType==axslc::SC_TYPE_MAT3)
        {
            if(stride<44)return error(cx,"mat3 reflected storage has no std140 column padding");
            for(unsigned column=0;column<3;++column)std::memcpy(destination+column*12,source+column*16,12);
        }
        else std::memcpy(destination,source,components*sizeof(uint32_t));
    }
    result.setObject(*array);return true;
}
class Texture final:public Object
{
public:
    RefPtr<Texture2D> native;
    rhi::SamplerDesc sampler;
    Texture() {sampler.mipFilter=rhi::SamplerFilter::MIP_DEFAULT;}
};
struct Attribute
{
    RefPtr<Buffer> buffer;
    int count=4;
    size_t stride{},offset{};
    bool enabled{};
};
struct VertexDeclaration
{
    rhi::VertexElementType format;
    size_t offset{},stride{};
    bool normalized{};
    rhi::VertexSemantic semantic;
};
struct StateVertexDeclarations
{
    WeakPtr<rhi::ProgramState> owner;
    std::map<int,VertexDeclaration> entries;
};
class GLNode;
struct State
{
    std::weak_ptr<RuntimeState> vm;
    std::vector<RefPtr<Object>> resources;
    RefPtr<Buffer> arrayBuffer;
    RefPtr<Texture> texture;
    Texture defaultTexture;
    RefPtr<Program> program;
    std::unordered_map<int,Attribute> attributes;
    std::unordered_map<rhi::ProgramState*,RefPtr<Program>> nativePrograms;
    std::unordered_map<rhi::ProgramState*,StateVertexDeclarations> vertexDeclarations;
    std::vector<WeakPtr<GLNode>> nodes;
    RefPtr<CustomEventListener> graphicsDrop;
    RefPtr<CustomEventListener> beforeDraw;
    WeakPtr<EventDispatcher> dispatcher;
    WeakPtr<rhi::GraphicsContext> readbackContext;
    struct UniformCallbackBinding {WeakPtr<rhi::ProgramState> owner;rhi::UniformLocation location;};
    std::vector<UniformCallbackBinding> uniformCallbacks;
    GLNode* drawing{};
    Renderer* renderer{};
    Mat4 mvp{Mat4::identity};
    Color clearColor{0,0,0,1};
    float clearDepth=1;
    unsigned clearStencil=0;
    template<class T> T* own(T* resource)
    {
        resources.emplace_back(resource,tlx::adopt_object);return resource;
    }
    void clearGraphics();
    void ensureReadback(Renderer* target)
    {
        auto* context = target ? target->getContext() : nullptr;
        if (context == readbackContext.get()) return;
        if (auto* previous = readbackContext.get()) previous->releaseLegacyDefaultFramebufferReadback();
        readbackContext = target ? target->acquireLegacyDefaultFramebufferReadback() : nullptr;
    }
};
std::unordered_map<JSContext*,std::shared_ptr<State>> states;
std::shared_ptr<State> state(JSContext* cx)
{
    auto it=states.find(cx);return it==states.end()?nullptr:it->second;
}
class GLNode final:public Node
{
public:
    std::weak_ptr<State> owner;
    std::vector<std::unique_ptr<CustomCommand>> commands;
    unsigned frame=std::numeric_limits<unsigned>::max();
    float order{};
    GLNode() = default;
    void draw(const SceneRenderState& renderState,const Mat4& transform,uint32_t flags)override
    {
        auto* renderer=renderState.getRenderer();const auto& view=renderState.getView();
        auto state=owner.lock();if(!state)return;
        auto vm=state->vm.lock();if(!vm||!vm->alive||!vm->onOwnerThread())return;
        auto* js=engine(vm->context);if(!js)return;
        const auto currentFrame=Director::getInstance()->getTotalFrames();
        if(frame!=currentFrame) {commands.clear();frame=currentFrame;}
        const auto savedNode=state->drawing;
        auto* savedRenderer=state->renderer;
        const Mat4 savedMvp=state->mvp;
        state->drawing=this;state->renderer=renderer;state->mvp=view.viewProjection*transform;order=getGlobalZOrder();
        JS::RootedValue result(vm->context);
        js->callNativeMethod(this,"draw",JS::HandleValueArray::empty(),&result);
        state->drawing=savedNode;state->renderer=savedRenderer;state->mvp=savedMvp;
        AX_UNUSED_PARAM(flags);
    }
};
void State::clearGraphics()
{
    if(auto* context=readbackContext.get())context->releaseLegacyDefaultFramebufferReadback();
    readbackContext.reset();
    for(auto& [pointer, declarations]:vertexDeclarations)
    {AX_UNUSED_PARAM(pointer);if(auto* owner=declarations.owner.get())owner->setVertexLayoutOverride(nullptr);}
    vertexDeclarations.clear();
    for(auto& callback:uniformCallbacks)if(auto* owner=callback.owner.get())owner->setCallbackUniform(callback.location,{});
    uniformCallbacks.clear();
    for(auto& weak:nodes)if(auto* node=weak.get())node->commands.clear();
    // Explicit user retain() may keep a legacy GL handle alive beyond device
    // shutdown. Drop its GPU allocations while the old device is still valid.
    for(auto& resource:resources)
    {
        if(auto* value=dynamic_cast<Program*>(resource.get())) {value->uniforms.clear();value->state.reset();value->native.reset();value->vertex.reset();value->fragment.reset();}
        else if(auto* value=dynamic_cast<Shader*>(resource.get()))value->module.reset();
        else if(auto* value=dynamic_cast<Texture*>(resource.get()))value->native.reset();
    }
    nativePrograms.clear();program.reset();texture.reset();arrayBuffer.reset();attributes.clear();
    resources.clear();defaultTexture.native.reset();drawing=nullptr;renderer=nullptr;
}
bool deviceRequired(JSContext* cx)
{return rhi::GraphicsCore::device()!=nullptr||error(cx,"This GL operation requires an initialized graphics device");}
bool bytes(JSContext* cx,JS::HandleValue value,std::vector<uint8_t>& out)
{
    if(!value.isObject()||!JS_IsArrayBufferViewObject(&value.toObject()))return error(cx,"Expected a typed array or DataView");
    JS::RootedObject view(cx,&value.toObject());
    const size_t size=JS_GetArrayBufferViewByteLength(view);
    const auto* data=static_cast<const uint8_t*>(JS_GetArrayBufferViewData(view));
    if(size&&!data)return error(cx,"Typed array has no accessible backing data");
    if(size)out.assign(data,data+size);else out.clear();return true;
}
bool floats(JSContext* cx,JS::HandleValue value,std::vector<float>& out)
{
    if(value.isObject()&&JS_IsTypedArrayObject(&value.toObject()))
    {
        JS::RootedObject view(cx,&value.toObject());
        if(JS_GetArrayBufferViewType(view)!=js::Scalar::Float32)return error(cx,"Expected Float32Array");
        const size_t count=JS_GetTypedArrayLength(view);
        const float* data=JS_GetFloat32ArrayData(view);out.assign(data,data+count);return true;
    }
    return fromJsValue(cx,value,out)||error(cx,"Expected a numeric array");
}
bool glInteger(JSContext* cx,JS::HandleValue value,int& result)
{
    double number;
    if(!JS::ToNumber(cx,value,&number))return false;
    if(!std::isfinite(number)||number<std::numeric_limits<int>::min()||number>std::numeric_limits<int>::max())
        return error(cx,"GL integer is outside its finite signed range");
    result=static_cast<int>(number);return true;
}

template<class T> JSClass& nativeClass(const char* name)
{
    static JSClass value=[] { JSClass c{};c.flags=JSCLASS_HAS_PRIVATE;
        c.addProperty=JS_PropertyStub;c.delProperty=JS_DeletePropertyStub;c.getProperty=JS_PropertyStub;
        c.setProperty=JS_StrictPropertyStub;c.enumerate=JS_EnumerateStub;c.resolve=JS_ResolveStub;
        c.convert=JS_ConvertStub;c.finalize=finalizeNative;return c;}();
    value.name=name;return value;
}
template<class T> bool allocate(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(!constructUninitialized<T>(cx,args))return false;
    if constexpr(std::is_same_v<T,GLNode>)
    {
        T* node{};if(!toNative(cx,args.rval(),&node)||!node)return false;
        node->owner=state(cx);state(cx)->nodes.emplace_back(node);
    }
    return true;
}
bool nodeConstructor(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc)return error(cx,"GLNode takes no constructor arguments");
    JS::RootedObject wrapper(cx,JS_NewObjectForConstructor(cx,detail::getClassInfo(cx,typeid(GLNode)),args));
    if(!wrapper)return false;
    auto* node=new GLNode;node->init();node->owner=state(cx);
    if(!bindNative(cx,wrapper,node,NativeOwnership::Constructor)) {delete node;return false;}
    state(cx)->nodes.emplace_back(node);args.rval().setObject(*wrapper);return true;
}
bool nodeBaseDraw(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(!self<GLNode>(cx,args))return false;
    // Node::draw has no geometry. Only gl.drawArrays inside the overriding
    // method creates commands, just as a native custom node does.
    args.rval().setUndefined();return true;
}
bool programConstructor(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc!=0&&argc!=2)return error(cx,"GLProgram expects a vertex and fragment source filename");
    auto* program=new Program;
    if(argc==2)
    {
        std::string vertex,fragment;
        if(!argument(cx,args,0,vertex)||!argument(cx,args,1,fragment)) {delete program;return false;}
        auto* files=FileUtils::getInstance();
        program->vertex.reset(new Shader,tlx::adopt_object);program->fragment.reset(new Shader,tlx::adopt_object);
        program->vertex->vertex=true;program->vertex->source=files->getStringFromFile(vertex);
        program->fragment->source=files->getStringFromFile(fragment);
        if(!program->vertex->compile()||!program->fragment->compile()||!program->link())
        {delete program;return error(cx,"Failed to compile/link legacy shader source files; see source archive manifest");}
    }
    JS::RootedObject wrapper(cx,JS_NewObjectForConstructor(cx,detail::getClassInfo(cx,typeid(Program)),args));
    if(!wrapper) {delete program;return false;}
    if(!bindNative(cx,wrapper,program,NativeOwnership::Constructor)) {delete program;return false;}
    state(cx)->resources.emplace_back(program);args.rval().setObject(*wrapper);return true;
}

enum class Operation { CreateBuffer,BindBuffer,BufferData,CreateShader,ShaderSource,CompileShader,ShaderParameter,
 ShaderLog,CreateProgram,AttachShader,LinkProgram,ProgramParameter,UseProgram,AttribLocation,EnableAttrib,DisableAttrib,
 AttribPointer,UniformLocation,UniformMatrix,Uniform1,Uniform2,Uniform3,Uniform4,UniformRead,DrawArrays,
 CreateTexture,BindTexture,TexImage,TexParameter,GetTexParameter,Clear,ClearColor,ClearDepth,ClearStencil,
 ActiveAttrib,ActiveUniform,Extensions,Extension,ReadPixels,DeleteBuffer,DeleteShader,DeleteProgram,DeleteTexture };

template<Operation Op> bool remainingGlCall(JSContext*,const JS::CallArgs&,const std::shared_ptr<State>&);

template<Operation Op> bool glCall(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto s=state(cx);if(!s)return error(cx,"GL state belongs to a stopped VM");
    args.rval().setUndefined();
    if constexpr(Op==Operation::CreateBuffer)return wrapNative(cx,s->own(new Buffer),args.rval());
    else if constexpr(Op==Operation::CreateShader)
    {
        int stage;if(!argument(cx,args,0,stage))return false;
        if(stage!=VERTEX_SHADER&&stage!=FRAGMENT_SHADER)return error(cx,"Invalid shader stage");
        auto* shader=s->own(new Shader);shader->vertex=stage==VERTEX_SHADER;return wrapNative(cx,shader,args.rval());
    }
    else if constexpr(Op==Operation::CreateProgram)return wrapNative(cx,s->own(new Program),args.rval());
    else if constexpr(Op==Operation::CreateTexture)return wrapNative(cx,s->own(new Texture),args.rval());
    else if constexpr(Op==Operation::BindBuffer)
    {
        int target;Buffer* buffer{};if(!argument(cx,args,0,target)||!argument(cx,args,1,buffer))return false;
        if(target!=ARRAY_BUFFER)return error(cx,"Only ARRAY_BUFFER is supported by the legacy drawArrays API");
        s->arrayBuffer=buffer;return true;
    }
    else if constexpr(Op==Operation::BufferData)
    {
        int target,usage;if(!argument(cx,args,0,target)||!argument(cx,args,2,usage))return false;
        if(target!=ARRAY_BUFFER||!s->arrayBuffer)return error(cx,"No ARRAY_BUFFER is bound");
        if(usage!=STATIC_DRAW&&usage!=DYNAMIC_DRAW)return error(cx,"Unsupported buffer usage");
        s->arrayBuffer->usage=usage==STATIC_DRAW?rhi::BufferUsage::STATIC:rhi::BufferUsage::DYNAMIC;
        if(args[1].isNumber())
        {
            size_t count;if(!argument(cx,args,1,count))return false;s->arrayBuffer->bytes.assign(count,0);return true;
        }
        return bytes(cx,args[1],s->arrayBuffer->bytes);
    }
    else if constexpr(Op==Operation::ShaderSource||Op==Operation::CompileShader||Op==Operation::ShaderParameter||Op==Operation::ShaderLog)
    {
        Shader* shader{};if(!argument(cx,args,0,shader)||!shader)return error(cx,"Expected a live shader");
        if constexpr(Op==Operation::ShaderSource) {shader->module.reset();return argument(cx,args,1,shader->source);}
        else if constexpr(Op==Operation::CompileShader) {shader->compile();return true;}
        else if constexpr(Op==Operation::ShaderLog)return toJsValue(cx,shader->diagnostic,args.rval());
        else {int name;if(!argument(cx,args,1,name))return false;if(name!=COMPILE_STATUS)return error(cx,"Unsupported shader parameter");args.rval().setBoolean(shader->module&&shader->module->isCompiled());return true;}
    }
    else if constexpr(Op==Operation::AttachShader)
    {
        Program* program{};Shader* shader{};
        if(!argument(cx,args,0,program)||!argument(cx,args,1,shader)||!program||!shader)return error(cx,"Expected live program and shader");
        (shader->vertex?program->vertex:program->fragment)=shader;return true;
    }
    else if constexpr(Op==Operation::LinkProgram||Op==Operation::ProgramParameter||Op==Operation::UseProgram||Op==Operation::AttribLocation||Op==Operation::UniformLocation||Op==Operation::ActiveAttrib||Op==Operation::ActiveUniform||Op==Operation::UniformRead)
    {
        Program* program{};if(!argument(cx,args,0,program))return false;
        if constexpr(Op==Operation::UseProgram) {s->program=program;return true;}
        if(!program)return error(cx,"Expected a live program");
        if constexpr(Op==Operation::LinkProgram) {program->link();return true;}
        else if constexpr(Op==Operation::ProgramParameter)
        {
            int name;if(!argument(cx,args,1,name))return false;
            if(name==LINK_STATUS)args.rval().setBoolean(program->getNative()&&program->getNative()->isValid());
            else if(name==0x8b89)args.rval().setInt32(program->getNative()?static_cast<int>(program->getNative()->getActiveVertexInputs().size()):0);
            else if(name==0x8b86)args.rval().setInt32(program->getNative()?static_cast<int>(activeUniforms(program->getNative()).size()):0);
            else return error(cx,"Unsupported program parameter");return true;
        }
        else if constexpr(Op==Operation::AttribLocation||Op==Operation::UniformLocation)
        {
            std::string name;if(!argument(cx,args,1,name))return false;
            if constexpr(Op==Operation::AttribLocation) {args.rval().setInt32(program->attribute(name));return true;}
            else return wrapNative(cx,program->uniform(name),args.rval());
        }
        else if constexpr(Op==Operation::UniformRead)
        {
            Uniform* uniform{};if(!argument(cx,args,1,uniform)||!uniform||uniform->owner.get()!=program)return error(cx,"Uniform location belongs to another or destroyed program");
            return readUniform(cx,program,uniform,args.rval());
        }
        else if constexpr(Op==Operation::ActiveAttrib)
        {
            size_t index;if(!argument(cx,args,1,index))return false;
            if(!program->getNative()||index>=program->getNative()->getActiveVertexInputs().size()) {args.rval().setNull();return true;}
            std::vector<const rhi::VertexInputDesc*> inputs;for(const auto& entry:program->getNative()->getActiveVertexInputs())inputs.push_back(&entry.second);
            std::sort(inputs.begin(),inputs.end(),[](const auto* a,const auto* b){return a->location<b->location;});
            const auto* input=inputs[index];
            std::string name=legacyAttributeName(input->semantic);
            if(program->vertex)if(const auto* archive=archiveFor(program->vertex->source,true))
            {AX_UNUSED_PARAM(archive);if(name=="a_position"&&program->vertex->source.find("aVertexPosition")!=std::string::npos)name="aVertexPosition";else if(name=="a_color"&&program->vertex->source.find("aVertexColor")!=std::string::npos)name="aVertexColor";}
            const int type=input->varType==axslc::SC_TYPE_FLOAT4?0x8b52:input->varType==axslc::SC_TYPE_FLOAT3?0x8b51:input->varType==axslc::SC_TYPE_FLOAT2?0x8b50:FLOAT;
            JS::RootedObject info(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
            if(!info||!property(cx,info,"size",1)||!property(cx,info,"type",type)||!property(cx,info,"name",name))return false;args.rval().setObject(*info);return true;
        }
        else
        {
            size_t index;if(!argument(cx,args,1,index))return false;
            if(!program->getNative()) {args.rval().setNull();return true;}
            const auto uniforms=activeUniforms(program->getNative());
            if(index>=uniforms.size()) {args.rval().setNull();return true;}
            const auto& item=uniforms[index];std::string name=item.first=="u_tex0"?"CC_Texture0":std::string(item.first);
            JS::RootedObject info(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
            if(!info||!property(cx,info,"size",std::max<int>(item.second->count,1))||!property(cx,info,"type",uniformGLType(*item.second))||!property(cx,info,"name",name))return false;args.rval().setObject(*info);return true;
        }
    }
    else if constexpr(Op==Operation::EnableAttrib||Op==Operation::DisableAttrib||Op==Operation::AttribPointer)
    {
        int index;if(!argument(cx,args,0,index)||index<0)return error(cx,"Invalid vertex attribute index");auto& attribute=s->attributes[index];
        if constexpr(Op==Operation::EnableAttrib)attribute.enabled=true;
        else if constexpr(Op==Operation::DisableAttrib)attribute.enabled=false;
        else
        {
            int count,type;bool normalized;size_t stride,offset;
            if(!argument(cx,args,1,count)||!argument(cx,args,2,type)||!argument(cx,args,3,normalized)||!argument(cx,args,4,stride)||!argument(cx,args,5,offset))return false;
            if(!s->arrayBuffer||type!=FLOAT||count<1||count>4||normalized||stride%4||offset%4)return error(cx,"Expected a bound float attribute with aligned stride and offset");
            attribute.buffer=s->arrayBuffer;attribute.count=count;attribute.stride=stride;attribute.offset=offset;
        }
        return true;
    }
    else if constexpr(Op==Operation::UniformMatrix||Op==Operation::Uniform1||Op==Operation::Uniform2||Op==Operation::Uniform3||Op==Operation::Uniform4)
    {
        Uniform* uniform{};if(!argument(cx,args,0,uniform))return false;if(!uniform)return true;
        auto* owner=uniform->owner.get();if(!owner)return error(cx,"Uniform's native program has been destroyed");
        std::vector<float> data;
        if constexpr(Op==Operation::UniformMatrix)
        {
            bool transpose;if(!argument(cx,args,1,transpose)||!floats(cx,args[2],data))return false;
            if(transpose||data.size()!=16)return error(cx,"uniformMatrix4fv expects a non-transposed mat4");
        }
        else
        {
            constexpr size_t size=Op==Operation::Uniform1?1:Op==Operation::Uniform2?2:Op==Operation::Uniform3?3:4;
            data.resize(size);for(size_t i=0;i<size;++i)if(!argument(cx,args,static_cast<unsigned>(i+1),data[i]))return false;
        }
        return owner->write(uniform,data)||error(cx,"Invalid uniform write");
    }
    else if constexpr(Op==Operation::DrawArrays)
    {
        int mode;size_t first,count;if(!argument(cx,args,0,mode)||!argument(cx,args,1,first)||!argument(cx,args,2,count))return false;
        if(!s->drawing||!s->renderer)return error(cx,"gl.drawArrays must run inside a GLNode.draw override");
        auto* program=s->program.get();if(!program||!program->getNative()||!program->getState())return error(cx,"No linked program is in use");
        rhi::PrimitiveType primitive;
        if(mode==0x0004)primitive=rhi::PrimitiveType::TRIANGLE;
        else if(mode==0x0005)primitive=rhi::PrimitiveType::TRIANGLE_STRIP;
        else if(mode==0x0001)primitive=rhi::PrimitiveType::LINE;
        else if(mode==0x0000)primitive=rhi::PrimitiveType::POINT;
        else return error(cx,"Unsupported legacy drawArrays primitive");
        if(!count)return true;if(first>std::numeric_limits<size_t>::max()-count)return error(cx,"Vertex range overflows");
        rhi::VertexLayoutDesc layout;layout.startLayout(program->getNative()->getActiveVertexInputs().size());
        size_t stride=0;
        struct Binding {const Attribute* source;const rhi::VertexInputDesc* native;size_t output;};std::vector<Binding> bindings;
        for(const auto& entry:program->getNative()->getActiveVertexInputs())
        {
            const auto& input=entry.second;
            std::string name=legacyAttributeName(input.semantic);
            int index=program->attribute(name);
            const auto found=s->attributes.find(index);
            if(found==s->attributes.end()||!found->second.enabled||!found->second.buffer)return error(cx,"A reflected vertex attribute has no enabled buffer");
            const auto& source=found->second;
            const size_t sourceStride=source.stride?source.stride:static_cast<size_t>(source.count)*sizeof(float);
            const size_t last=first+count-1;
            if(source.offset>source.buffer->bytes.size()||last>(std::numeric_limits<size_t>::max()-source.offset)/sourceStride||
               source.offset+last*sourceStride>source.buffer->bytes.size()||static_cast<size_t>(source.count)*4>source.buffer->bytes.size()-(source.offset+last*sourceStride))
                return error(cx,"drawArrays reads beyond a bound attribute buffer");
            layout.addAttrib(&input,static_cast<rhi::VertexElementType>(4-source.count),stride,false);
            bindings.push_back({&source,&input,stride});stride+=source.count*sizeof(float);
        }
        if(stride==0||count>std::numeric_limits<size_t>::max()/stride)return error(cx,"Invalid vertex buffer size");
        layout.endLayout(static_cast<int>(stride));std::vector<uint8_t> data(count*stride);
        for(size_t vertex=0;vertex<count;++vertex)for(const auto& binding:bindings)
        {const auto& source=*binding.source;const size_t sourceStride=source.stride?source.stride:source.count*sizeof(float);
         std::memcpy(data.data()+vertex*stride+binding.output,source.buffer->bytes.data()+source.offset+(first+vertex)*sourceStride,source.count*sizeof(float));}
        auto snapshot=RefPtr<rhi::ProgramState>(program->getState()->clone(),tlx::adopt_object);
        configureLegacyFragmentCoordinates(snapshot.get());
        if(s->texture&&s->texture->native) snapshot->setTexture(s->texture->native->getRHITexture());
        auto vertices=RefPtr<rhi::VertexLayout>(rhi::GraphicsCore::device()->createVertexLayout(std::move(layout)),tlx::adopt_object);
        auto command=std::make_unique<CustomCommand>();command->init(s->drawing->order,BlendFunc::DISABLE);
        command->setOwnPSVL(snapshot.get(),vertices.get());command->setPrimitiveType(primitive);command->setDrawType(CustomCommand::DrawType::ARRAY);
        command->createVertexBuffer(stride,count,rhi::BufferUsage::DYNAMIC);command->updateVertexBuffer(data.data(),data.size());command->setVertexDrawInfo(0,count);
        s->renderer->addCommand(command.get());s->drawing->commands.push_back(std::move(command));return true;
    }
    else return remainingGlCall<Op>(cx,args,s);
}
} // namespace axjs::legacygl

namespace axjs::legacygl
{
template<Operation Op> bool remainingGlCall(JSContext* cx,const JS::CallArgs& args,const std::shared_ptr<State>& s)
{
    if constexpr(Op==Operation::BindTexture)
    {
        int target;if(!argument(cx,args,0,target))return false;
        if(target!=TEXTURE_2D)return error(cx,"Expected TEXTURE_2D");
        if(args.length()<2)return error(cx,"bindTexture requires a texture handle");
        Texture* texture{};
        if(fromJsValue(cx,args[1],texture)){s->texture=texture;return true;}
        Texture2D* native{};
        if(!fromJsValue(cx,args[1],native)||!native)return error(cx,"Expected a native Texture2D handle or a legacy GL texture");
        auto peer=RefPtr<Texture>(new Texture,tlx::adopt_object);peer->native=native;
        if(auto* gpu=native->getRHITexture())peer->sampler=gpu->getSamplerDesc();
        s->texture=std::move(peer);return true;
    }
    else if constexpr(Op==Operation::TexImage)
    {
        int target,level,internal,width,height,border,format,type;
        if(!argument(cx,args,0,target)||!argument(cx,args,1,level)||!argument(cx,args,2,internal)||
           !argument(cx,args,3,width)||!argument(cx,args,4,height)||!argument(cx,args,5,border)||
           !argument(cx,args,6,format)||!argument(cx,args,7,type))return false;
        if(!deviceRequired(cx))return false;
        if(target!=TEXTURE_2D||!s->texture||level!=0||border!=0||internal!=format||type!=UNSIGNED_BYTE||
           (format!=RGBA&&format!=RGB)||width<=0||height<=0||width>axdrv->getMaxTextureSize()||height>axdrv->getMaxTextureSize())
            return error(cx,"Unsupported texture image or invalid size");
        const size_t channels=format==RGBA?4:3;
        if(static_cast<size_t>(width)>std::numeric_limits<size_t>::max()/channels/static_cast<size_t>(height))return error(cx,"Texture size overflows");
        const size_t required=static_cast<size_t>(width)*height*channels;
        std::vector<uint8_t> pixels;
        if(args.length()<9||args[8].isNullOrUndefined())pixels.assign(required,0);
        else if(!bytes(cx,args[8],pixels))return false;
        if(pixels.size()<required)return error(cx,"Texture array is shorter than its dimensions");
        auto texture=RefPtr<Texture2D>(new Texture2D,tlx::adopt_object);
        if(!texture->initWithData(pixels.data(),static_cast<ssize_t>(required),format==RGBA?rhi::PixelFormat::RGBA8:rhi::PixelFormat::RGB8,width,height))
            return error(cx,"Graphics backend rejected the texture image");
        texture->getRHITexture()->updateSamplerDesc(s->texture->sampler);s->texture->native=std::move(texture);return true;
    }
    else if constexpr(Op==Operation::TexParameter||Op==Operation::GetTexParameter)
    {
        int target,name;if(!argument(cx,args,0,target)||!argument(cx,args,1,name))return false;
        if(target!=TEXTURE_2D)return error(cx,"Expected TEXTURE_2D");
        auto* texture=s->texture?s->texture.get():&s->defaultTexture;
        auto& sampler=texture->sampler;
        if constexpr(Op==Operation::GetTexParameter)
        {
            int value;
            if(name==TEXTURE_MAG_FILTER)value=sampler.magFilter==rhi::SamplerFilter::MAG_NEAREST?NEAREST:LINEAR;
            else if(name==TEXTURE_MIN_FILTER)value=sampler.minFilter==rhi::SamplerFilter::MIN_NEAREST?NEAREST:LINEAR;
            else if(name==TEXTURE_WRAP_S||name==TEXTURE_WRAP_T)
            {const auto mode=name==TEXTURE_WRAP_S?sampler.sAddressMode:sampler.tAddressMode;value=mode==rhi::SamplerAddressMode::REPEAT?REPEAT:mode==rhi::SamplerAddressMode::MIRROR?MIRRORED_REPEAT:CLAMP_TO_EDGE;}
            else return error(cx,"Unsupported texture parameter");args.rval().setInt32(value);return true;
        }
        else
        {
            int value;if(!argument(cx,args,2,value))return false;
            if(name==TEXTURE_MAG_FILTER||name==TEXTURE_MIN_FILTER)
            {
                if(value!=NEAREST&&value!=LINEAR)return error(cx,"Unsupported texture filter");
                if(name==TEXTURE_MAG_FILTER)sampler.magFilter=value==NEAREST?rhi::SamplerFilter::MAG_NEAREST:rhi::SamplerFilter::MAG_LINEAR;
                else sampler.minFilter=value==NEAREST?rhi::SamplerFilter::MIN_NEAREST:rhi::SamplerFilter::MIN_LINEAR;
            }
            else if(name==TEXTURE_WRAP_S||name==TEXTURE_WRAP_T)
            {
                rhi::SamplerAddressMode mode;if(value==CLAMP_TO_EDGE)mode=rhi::SamplerAddressMode::CLAMP;
                else if(value==REPEAT)mode=rhi::SamplerAddressMode::REPEAT;
                else if(value==MIRRORED_REPEAT)mode=rhi::SamplerAddressMode::MIRROR;
                else return error(cx,"Unsupported texture address mode");
                if(name==TEXTURE_WRAP_S)sampler.sAddressMode=mode;else sampler.tAddressMode=mode;
            }
            else return error(cx,"Unsupported texture parameter");
            if(texture->native)texture->native->getRHITexture()->updateSamplerDesc(sampler);return true;
        }
    }
    else if constexpr(Op==Operation::ClearColor)
    {
        float r,g,b,a;if(!argument(cx,args,0,r)||!argument(cx,args,1,g)||!argument(cx,args,2,b)||!argument(cx,args,3,a))return false;
        s->clearColor=Color(r,g,b,a);return true;
    }
    else if constexpr(Op==Operation::ClearDepth)return argument(cx,args,0,s->clearDepth);
    else if constexpr(Op==Operation::ClearStencil)return argument(cx,args,0,s->clearStencil);
    else if constexpr(Op==Operation::Clear)
    {
        int mask;if(!argument(cx,args,0,mask))return false;
        if(!s->drawing||!s->renderer)return error(cx,"gl.clear must run inside a GLNode.draw override");
        if(mask&~(0x4000|0x0100|0x0400))return error(cx,"Unsupported clear mask");
        auto flags=ClearFlag::NONE;if(mask&0x4000)flags=flags|ClearFlag::COLOR;if(mask&0x0100)flags=flags|ClearFlag::DEPTH;if(mask&0x0400)flags=flags|ClearFlag::STENCIL;
        s->renderer->clear(flags,s->clearColor,s->clearDepth,s->clearStencil,s->drawing->order);return true;
    }
    else if constexpr(Op==Operation::Extensions||Op==Operation::Extension)
    {
        if(!deviceRequired(cx))return false;
        if constexpr(Op==Operation::Extensions)
        {
            const auto text=axdrv->dumpExtensions();std::vector<std::string> values;
            size_t offset=0;while(offset<text.size()) {const auto begin=text.find_first_not_of(" \t\r\n,",offset);if(begin==std::string::npos)break;auto end=text.find_first_of(" \t\r\n,",begin);if(end==std::string::npos)end=text.size();values.emplace_back(text.substr(begin,end-begin));offset=end;}
            return toJsValue(cx,values,args.rval());
        }
        else
        {
            std::string name;if(!argument(cx,args,0,name))return false;
            if(!axdrv->hasExtension(name)) {args.rval().setNull();return true;}
            JS::RootedObject extension(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
            if(!extension||!property(cx,extension,"name",name))return false;args.rval().setObject(*extension);return true;
        }
    }
    else if constexpr(Op==Operation::ReadPixels)
    {
        int x,y,width,height,format,type;
        if(args.length()<7||!glInteger(cx,args[0],x)||!glInteger(cx,args[1],y)||
           !glInteger(cx,args[2],width)||!glInteger(cx,args[3],height)||
           !argument(cx,args,4,format)||!argument(cx,args,5,type))return false;
        if(format!=RGBA||type!=UNSIGNED_BYTE||width<0||height<0||x<0||y<0||args.length()<7||!args[6].isObject())return error(cx,"readPixels expects a nonnegative RGBA/UNSIGNED_BYTE rectangle and Uint8Array");
        JS::RootedObject output(cx,&args[6].toObject());
        if(!JS_IsTypedArrayObject(output)||(JS_GetArrayBufferViewType(output)!=js::Scalar::Uint8&&JS_GetArrayBufferViewType(output)!=js::Scalar::Uint8Clamped))return error(cx,"readPixels output must be Uint8Array");
        if(height&&static_cast<size_t>(width)>std::numeric_limits<size_t>::max()/static_cast<size_t>(height)/4)
            return error(cx,"readPixels rectangle exceeds addressable storage");
        const size_t required=static_cast<size_t>(width)*height*4;
        if(JS_GetArrayBufferViewByteLength(output)<required)return error(cx,"readPixels output is too short");
        if(!width||!height)return true;if(!deviceRequired(cx))return false;
        auto* renderer=Director::getInstance()->getRenderer();
        s->ensureReadback(renderer);
        rhi::PixelBufferDesc pixels;
        if(!renderer||!renderer->readPixelsSync(renderer->getRenderTarget(),pixels))
            return error(cx,"Synchronous readPixels has no readable rendered framebuffer contents");
        if(static_cast<uint64_t>(x)+width>pixels._width||static_cast<uint64_t>(y)+height>pixels._height)
            return error(cx,"readPixels rectangle lies outside the physical framebuffer");
        auto* data=static_cast<uint8_t*>(JS_GetArrayBufferViewData(output));if(!data)return error(cx,"readPixels output has no backing storage");
        // RHI screen captures are top-first on every backend (GL's capture
        // implementation already flips its default framebuffer).
        const bool topFirst=renderer->getRenderTarget()->isDefaultRenderTarget()||!rhi::GraphicsCore::isOpenGL();
        bool bgra=false;
        if(rhi::GraphicsCore::isVulkan())
            bgra=renderer->getRenderTarget()->getColorAttachmentPixelFormat(0)==rhi::PixelFormat::BGRA8;
        for(int row=0;row<height;++row)for(int column=0;column<width;++column)
        {
            const int px=x+column;
            int py=y+row;if(topFirst)py=pixels._height-1-py;
            const size_t source=(static_cast<size_t>(py)*pixels._width+px)*4;
            if(source+4>pixels._data.size())return error(cx,"Backend returned an inconsistent pixel buffer");
            const auto* pixel=pixels._data.data()+source;auto* destination=data+(static_cast<size_t>(row)*width+column)*4;
            destination[0]=pixel[bgra?2:0];destination[1]=pixel[1];destination[2]=pixel[bgra?0:2];destination[3]=pixel[3];
        }
        return true;
    }
    else if constexpr(Op==Operation::DeleteBuffer||Op==Operation::DeleteShader||Op==Operation::DeleteProgram||Op==Operation::DeleteTexture)
    {
        Object* object{};if(!argument(cx,args,0,object))return false;if(!object)return true;
        if constexpr(Op==Operation::DeleteBuffer) {if(s->arrayBuffer.get()==object)s->arrayBuffer.reset();for(auto& entry:s->attributes)if(entry.second.buffer.get()==object)entry.second.buffer.reset();}
        else if constexpr(Op==Operation::DeleteProgram) {if(s->program.get()==object)s->program.reset();}
        else if constexpr(Op==Operation::DeleteTexture) {if(s->texture.get()==object)s->texture.reset();}
        engine(cx)->invalidateScriptObject(object);
        std::erase_if(s->resources,[object](const auto& resource){return resource.get()==object;});return true;
    }
    else return error(cx,"Unknown legacy GL operation");
}

enum class ProgramOperation { Link,Use,UpdateUniforms,Builtins,GetProgram,Location,Attribute,Uniform,Create };
template<ProgramOperation Op> bool programCall(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);args.rval().setUndefined();
    if constexpr(Op==ProgramOperation::Create)
    {
        std::string vertex,fragment;if(!argument(cx,args,0,vertex)||!argument(cx,args,1,fragment))return false;
        auto* s=state(cx).get();auto* program=s->own(new Program);
        program->vertex.reset(new Shader,tlx::adopt_object);program->fragment.reset(new Shader,tlx::adopt_object);
        program->vertex->vertex=true;program->vertex->source=FileUtils::getInstance()->getStringFromFile(vertex);program->fragment->source=FileUtils::getInstance()->getStringFromFile(fragment);
        if(!program->vertex->compile()||!program->fragment->compile()||!program->link())return error(cx,"Failed to create a legacy shader program from its compiled resources");
        return wrapNative(cx,program,args.rval());
    }
    else
    {
        auto* program=self<Program>(cx,args);if(!program)return false;
        if constexpr(Op==ProgramOperation::Link) {args.rval().setBoolean(program->link());return true;}
        else if constexpr(Op==ProgramOperation::Use) {if(!program->getNative())return error(cx,"Program is not linked");state(cx)->program=program;return true;}
        else if constexpr(Op==ProgramOperation::GetProgram)return wrapNative(cx,program,args.rval());
        else if constexpr(Op==ProgramOperation::Attribute) {std::string name;int location;if(!argument(cx,args,0,name)||!argument(cx,args,1,location))return false;if(location<0)return error(cx,"Invalid attribute location");program->bindings[name]=location;return true;}
        else if constexpr(Op==ProgramOperation::Location) {std::string name;if(!argument(cx,args,0,name))return false;return wrapNative(cx,program->uniform(name),args.rval());}
        else if constexpr(Op==ProgramOperation::Uniform)
        {
            Uniform* location{};if(!argument(cx,args,0,location))return false;if(!location)return true;
            if(argc<2||argc>5)return error(cx,"Expected one to four float components");
            std::array<float,4> values{};for(unsigned i=1;i<argc;++i)if(!argument(cx,args,i,values[i-1]))return false;
            return program->write(location,std::span<const float>(values.data(),argc-1))||error(cx,"Uniform does not belong to this program");
        }
        else if constexpr(Op==ProgramOperation::Builtins)
        {
            if(!program->getState())return error(cx,"Program is not linked");
            auto s=state(cx);if(!s->drawing)return error(cx,"Built-in matrices require the current GLNode draw transform");
            if(auto* matrix=program->uniform("CC_MVPMatrix"))program->write(matrix,std::span<const float>(s->mvp.m,16));
            const float elapsed=static_cast<float>(Director::getInstance()->getTotalFrames()*Director::getInstance()->getAnimationInterval());
            const std::array<float,4> time={elapsed/10,elapsed,elapsed*2,elapsed*4};
            if(auto* uniform=program->uniform("CC_Time"))program->write(uniform,time);
            const std::array<float,4> sine={elapsed/8,elapsed/4,elapsed/2,std::sin(elapsed)};
            const std::array<float,4> cosine={elapsed/8,elapsed/4,elapsed/2,std::cos(elapsed)};
            if(auto* uniform=program->uniform("CC_SinTime"))program->write(uniform,sine);
            if(auto* uniform=program->uniform("CC_CosTime"))program->write(uniform,cosine);
            return true;
        }
        else {if(!program->getState())return error(cx,"Program is not linked");program->getState()->updateBatchId();return true;}
    }
}
const BuiltinShaderSource* builtinSource(std::string_view name)
{
    for(const auto& source:builtinShaderSources) if(source.name==name) return &source;
    for(const auto& source:builtinShaderVariants) if(source.name==name) return &source;
    return nullptr;
}
rhi::Program* loadLegacyBuiltinProgram(std::string_view vertexName,std::string_view fragmentName)
{
    const auto* vertex=builtinSource(vertexName);
    const auto* fragment=builtinSource(fragmentName);
    std::string vertexPath,fragmentPath;
    if(!vertex || !fragment || !findLegacyShaderArchives(vertex->source,fragment->source,"",vertexPath,fragmentPath))
        return nullptr;
    return axpm->loadProgram(vertexPath,fragmentPath,rhi::VertexLayoutKind::Sprite);
}
rhi::ProgramState* acquireLegacySpriteTextureVariant(uint32_t programType,int textureSamplerFlags);
rhi::Program* loadLegacySpriteProgram(uint32_t programType)
{
    std::string_view fragment;
    switch(programType)
    {
    case rhi::ProgramType::POSITION_TEXTURE_COLOR:
    case rhi::ProgramType::POSITION_TEXTURE_COLOR_ALIAS:
        fragment="position-texture-color-no-mvp.frag";break;
    case rhi::ProgramType::POSITION_TEXTURE_GRAY:
        fragment="position-texture-gray-no-mvp.frag";break;
    case rhi::ProgramType::POSITION_TEXTURE_GRAY_ALPHA:
        fragment="position-texture-gray-alpha-no-mvp.frag";break;
    default:return nullptr;
    }
    auto* program=loadLegacyBuiltinProgram("position-texture-color-no-mvp.vert",fragment);
    if(program)ProgramStateRegistry::getInstance()->registerTextureVariant(program,acquireLegacySpriteTextureVariant);
    return program;
}
rhi::ProgramState* acquireLegacySpriteTextureVariant(uint32_t programType,int textureSamplerFlags)
{
    // These shaders use the bound texture's dynamic sampler, for either filter.
    // No JSContext/VM/Node state survives in this native program family.
    AX_UNUSED_PARAM(textureSamplerFlags);
    auto* program=loadLegacySpriteProgram(programType);
    return program?new rhi::ProgramState(program):nullptr;
}
template<class T> bool prepareLegacyTexturedNode(JSContext* cx,unsigned argc,JS::Value* vp,const char* typeName)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    T* node{};
    if(argc!=1 || !argument(cx,args,0,node) || !node)
    {
        JS_ReportError(cx,"Expected a live native %s",typeName);
        return false;
    }
    auto* existing=node->getProgramState();
    if(existing)
    {
        const auto type=existing->getProgram()->getProgramType();
        // Legacy Sprite/AtlasNode texture parameters need the texture's dynamic sampler.
        // Modern presets and explicitly assigned user shaders remain intact.
        if(type==rhi::ProgramType::POSITION_TEXTURE_COLOR || type==rhi::ProgramType::POSITION_TEXTURE_COLOR_ALIAS ||
           type==rhi::ProgramType::POSITION_TEXTURE_GRAY || type==rhi::ProgramType::POSITION_TEXTURE_GRAY_ALPHA)
        {
            if(!deviceRequired(cx))return false;
            // Validate every member before opting in: Animate may switch format
            // entirely in native code, without another JavaScript preparation.
            if(!loadLegacySpriteProgram(rhi::ProgramType::POSITION_TEXTURE_COLOR) ||
               !loadLegacySpriteProgram(rhi::ProgramType::POSITION_TEXTURE_GRAY) ||
               !loadLegacySpriteProgram(rhi::ProgramType::POSITION_TEXTURE_GRAY_ALPHA))
            {
                JS_ReportError(cx,"Original %s shader archives are missing or invalid",typeName);
                return false;
            }
            auto* texture=node->getTexture();
            auto* program=loadLegacySpriteProgram(texture?ProgramManager::chooseSpriteProgramType(texture->getPixelFormat()):
                rhi::ProgramType::POSITION_TEXTURE_COLOR);
            if(!program)
            {
                JS_ReportError(cx,"Original %s shader archive could not be loaded",typeName);
                return false;
            }
            auto replacement=RefPtr<rhi::ProgramState>(new rhi::ProgramState(program),tlx::adopt_object);
            // AtlasNode also updates its quad command and texture binding here.
            if(!node->setProgramState(replacement.get()))
            {
                JS_ReportError(cx,"Native %s rejected its legacy sampler program",typeName);
                return false;
            }
        }
    }
    args.rval().set(args[0]);
    return true;
}
bool prepareLegacySprite(JSContext* cx,unsigned argc,JS::Value* vp)
{
    return prepareLegacyTexturedNode<Sprite>(cx,argc,vp,"Sprite");
}
bool prepareLegacyAtlasNode(JSContext* cx,unsigned argc,JS::Value* vp)
{
    return prepareLegacyTexturedNode<AtlasNode>(cx,argc,vp,"AtlasNode");
}
bool builtinProgram(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);std::string name;if(!argument(cx,args,0,name)||!deviceRequired(cx))return false;
    uint32_t type;
    if(name=="ShaderPositionColor")type=rhi::ProgramType::POSITION_COLOR;
    else if(name=="ShaderPositionTexture")type=rhi::ProgramType::POSITION_TEXTURE;
    else if(name=="ShaderPositionTextureColor")type=rhi::ProgramType::POSITION_TEXTURE_COLOR;
    else if(name=="ShaderPositionTextureColorAlphaTest")type=rhi::ProgramType::POSITION_TEXTURE_COLOR_ALPHA_TEST;
    else if(name=="ShaderPosition_uColor")type=rhi::ProgramType::POSITION_UCOLOR;
    else return error(cx,"Unknown legacy built-in shader name");
    rhi::Program* native=nullptr;
    if(type==rhi::ProgramType::POSITION_TEXTURE)
        native=loadLegacyBuiltinProgram("position-texture.vert","position-texture.frag");
    else if(type==rhi::ProgramType::POSITION_TEXTURE_COLOR || type==rhi::ProgramType::POSITION_TEXTURE_COLOR_ALPHA_TEST)
        native=loadLegacyBuiltinProgram("position-texture-color.vert",type==rhi::ProgramType::POSITION_TEXTURE_COLOR ?
            "position-texture-color.frag" : "position-texture-color-alpha-test.frag");
    else native=ProgramManager::getInstance()->getBuiltinProgram(type);
    auto s=state(cx);
    if(!native)return error(cx,"Built-in RHI program was not created");
    for(const auto& item:s->resources)if(auto* program=dynamic_cast<Program*>(item.get());program&&program->native.get()==native)return wrapNative(cx,program,args.rval());
    auto* program=s->own(new Program);program->native=native;program->state.reset(new rhi::ProgramState(native),tlx::adopt_object);return wrapNative(cx,program,args.rval());
}
template<bool Set> bool nodeProgram(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* node=self<Node>(cx,args);if(!node)return false;
    if constexpr(Set)
    {
        Program* program{};if(!argument(cx,args,0,program)||!program||!program->getState())return error(cx,"Expected a linked GLProgram");
        node->setProgramState(program->getState(),false);args.rval().setUndefined();return true;
    }
    else
    {
        auto* native=node->getProgramState();if(!native) {args.rval().setNull();return true;}
        auto s=state(cx);auto found=s->nativePrograms.find(native);
        if(found!=s->nativePrograms.end()&&found->second->getState()==native)
            return wrapBorrowed(cx,found->second.get(),args.rval(),LifetimeToken(native));
        if(found!=s->nativePrograms.end())s->nativePrograms.erase(found);
        auto program=RefPtr<Program>(new Program,tlx::adopt_object);program->borrowed=native;program->borrowedState=true;
        auto* result=program.get();s->nativePrograms.emplace(native,std::move(program));return wrapBorrowed(cx,result,args.rval(),LifetimeToken(native));
    }
}
template<bool Independent=false> bool programStateFactory(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);Program* program{};
    if(!argument(cx,args,0,program)||!program||!program->getState())return error(cx,"Expected a linked GLProgram");
    auto* ps=program->getState();
    if constexpr(Independent) {ps=ps->clone();ps->autorelease();}
    return configureLegacyShaderProgramState(cx,ps)&&wrapNative(cx,ps,args.rval());
}
template<bool Set> bool nodeProgramState(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* node=self<Node>(cx,args);if(!node)return false;
    if constexpr(Set) {rhi::ProgramState* value{};if(!argument(cx,args,0,value)||!value)return false;node->setProgramState(value,false);args.rval().setUndefined();return true;}
    else return wrapNative(cx,node->getProgramState(),args.rval());
}
template<class T> bool programStateUniform(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* ps=self<rhi::ProgramState>(cx,args);std::string name;
    T value;if(!ps||argc!=2||!argument(cx,args,0,name)||!argument(cx,args,1,value))return false;
    constexpr uint16_t type=std::is_same_v<T,int>?axslc::SC_TYPE_INT:std::is_same_v<T,float>?axslc::SC_TYPE_FLOAT:
        std::is_same_v<T,Vec2>?axslc::SC_TYPE_FLOAT2:std::is_same_v<T,Vec3>?axslc::SC_TYPE_FLOAT3:
        std::is_same_v<T,Vec4>?axslc::SC_TYPE_FLOAT4:axslc::SC_TYPE_MAT4;
    const void* data=&value;if constexpr(std::is_same_v<T,Mat4>)data=value.m;
    if(!writeUniform(ps,name,data,sizeof(T),type))return error(cx,"Uniform is absent, has a different shader type or exceeds reflected storage");
    args.rval().setUndefined();return true;
}
bool vertexFormat(int count,int type,rhi::VertexElementType& format)
{
    if(count<1||count>4)return false;
    if(type==FLOAT)format=static_cast<rhi::VertexElementType>(static_cast<int>(rhi::VertexElementType::FLOAT)+1-count);
    else if(type==0x1404)format=static_cast<rhi::VertexElementType>(static_cast<int>(rhi::VertexElementType::INT)+1-count);
    else if(type==UNSIGNED_BYTE&&count==4)format=rhi::VertexElementType::UBYTE4;
    else if(type==0x1403&&count==2)format=rhi::VertexElementType::USHORT2;
    else if(type==0x1403&&count==4)format=rhi::VertexElementType::USHORT4;
    else return false;
    return true;
}
size_t vertexFormatBytes(rhi::VertexElementType type)
{
    switch(type)
    {
    case rhi::VertexElementType::FLOAT4:case rhi::VertexElementType::INT4:return 16;
    case rhi::VertexElementType::FLOAT3:case rhi::VertexElementType::INT3:return 12;
    case rhi::VertexElementType::FLOAT2:case rhi::VertexElementType::INT2:case rhi::VertexElementType::USHORT4:return 8;
    case rhi::VertexElementType::FLOAT:case rhi::VertexElementType::INT:case rhi::VertexElementType::UBYTE4:case rhi::VertexElementType::USHORT2:return 4;
    default:return 0;
    }
}
bool programStateVertexPointer(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* ps=self<rhi::ProgramState>(cx,args);
    std::string name;int components,type;bool normalized;size_t stride,offset;
    if(!ps||argc!=6||!argument(cx,args,0,name)||!argument(cx,args,1,components)||!argument(cx,args,2,type)||
        !argument(cx,args,3,normalized)||!argument(cx,args,4,stride)||!argument(cx,args,5,offset))return false;
    rhi::VertexElementType format;
    if(!vertexFormat(components,type,format))return error(cx,"Unsupported legacy vertex element type");
    const auto semantic=attributeSemantic(name);if(!semantic)return error(cx,"Unknown legacy vertex attribute name");
    const auto* input=ps->getProgram()->getVertexInputDesc(*semantic);
    // An inactive shader input has no layout entry, as in the original GL API.
    if(!input){args.rval().setUndefined();return true;}
    const size_t elementBytes=vertexFormatBytes(format);if(!stride)stride=elementBytes;
    if(stride>static_cast<size_t>(std::numeric_limits<int>::max())||offset>stride||elementBytes>stride-offset)
        return error(cx,"Vertex attribute is outside its stride");
    if(!deviceRequired(cx))return false;
    auto s=state(cx);auto& declaration=s->vertexDeclarations[ps];
    if(declaration.owner.get()!=ps){declaration.entries.clear();declaration.owner=ps;}
    auto entries=declaration.entries;
    entries[input->location]={format,offset,stride,normalized,*semantic};
    auto desc=VertexLayoutManager::getInstance()->allocateVertexLayoutDesc();desc.startLayout(entries.size());
    for(const auto& [index,entry]:entries)
    {
        AX_UNUSED_PARAM(index);
        if(entry.stride!=stride)return error(cx,"One native mesh binding requires a common vertex stride");
        const auto* reflected=ps->getProgram()->getVertexInputDesc(entry.semantic);
        if(reflected)desc.addAttrib(reflected,entry.format,entry.offset,entry.normalized);
    }
    desc.endLayout(static_cast<int>(stride));
    auto* layout=VertexLayoutManager::getInstance()->getVertexLayout(std::move(desc));
    if(!layout)return error(cx,"Graphics device rejected the legacy vertex layout");
    ps->setVertexLayoutOverride(layout);declaration.entries=std::move(entries);
    args.rval().setUndefined();return true;
}
bool programStateTexture(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* ps=self<rhi::ProgramState>(cx,args);std::string name;
    Object* resource{};if(!ps||argc!=2||!argument(cx,args,0,name)||!argument(cx,args,1,resource)||!resource)return false;
    rhi::Texture* texture=nullptr;
    if(auto* value=dynamic_cast<Texture2D*>(resource))texture=value->getRHITexture();
#if AX_ENABLE_3D
    else if(auto* value=dynamic_cast<TextureCube*>(resource))texture=value->getRHITexture();
#endif
    if(!texture)return error(cx,"Expected a live native Texture2D or TextureCube");
    rhi::UniformLocationVector locations;ps->getUniformLocations(uniformName(name,ps->getProgram()),locations);
    if(locations.empty())return error(cx,"Named texture sampler is not reflected by the native program");
    for(const auto& location:locations)
    {
        const auto* info=uniformInfo(ps->getProgram(),location);
        if(!info||info->offset>=0)return error(cx,"Named uniform is not a texture sampler");
        const auto& textures=ps->getProgram()->getActiveTextureInfos();
        int slot=0;bool found=false;
        for(const auto& entry:textures)
        {
            if(entry.second->location==location.location){found=true;break;}
            slot+=entry.second->count;
        }
        if(!found)return error(cx,"Named uniform is not a texture sampler");
        ps->setTexture(location,slot,texture);
    }
    args.rval().setUndefined();return true;
}
#if AX_ENABLE_3D
bool meshVertexAttribute(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* mesh=self<Mesh>(cx,args);int index;
    if(!mesh||argc!=1||!argument(cx,args,0,index))return false;
    if(index<0||index>=mesh->getMeshVertexAttribCount())return error(cx,"Mesh attribute index is outside the real vertex declaration");
    const auto& attribute=mesh->getMeshVertexAttribute(index);int count,type;
    switch(attribute.type)
    {
    case rhi::VertexElementType::FLOAT:case rhi::VertexElementType::FLOAT2:case rhi::VertexElementType::FLOAT3:case rhi::VertexElementType::FLOAT4:
        count=static_cast<int>(rhi::VertexElementType::FLOAT)+1-static_cast<int>(attribute.type);type=FLOAT;break;
    case rhi::VertexElementType::INT:case rhi::VertexElementType::INT2:case rhi::VertexElementType::INT3:case rhi::VertexElementType::INT4:
        count=static_cast<int>(rhi::VertexElementType::INT)+1-static_cast<int>(attribute.type);type=0x1404;break;
    case rhi::VertexElementType::USHORT2:count=2;type=0x1403;break;
    case rhi::VertexElementType::USHORT4:count=4;type=0x1403;break;
    case rhi::VertexElementType::UBYTE4:count=4;type=UNSIGNED_BYTE;break;
    default:return error(cx,"Mesh vertex element has no legacy GL representation");
    }
    JS::RootedObject result(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!result||!property(cx,result,"size",count)||!property(cx,result,"type",type)||
        !property(cx,result,"vertexAttrib",static_cast<int>(attribute.vertexAttrib))||
        !property(cx,result,"attribSizeBytes",attribute.getAttribSizeBytes()))return false;
    args.rval().setObject(*result);return true;
}
#endif
bool maxTextureSize(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);if(!deviceRequired(cx))return false;
    args.rval().setInt32(axdrv->getMaxTextureSize());return true;
}
bool programStateUniformCallback(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* ps=self<rhi::ProgramState>(cx,args);std::string name;
    if(!ps||argc!=2||!argument(cx,args,0,name))return false;
    rhi::UniformLocationVector locations;ps->getUniformLocations(uniformName(name,ps->getProgram()),locations);
    if(locations.empty())return error(cx,"Named callback uniform is not reflected by this program");
    const auto location=locations.front();
    if(args[1].isNullOrUndefined()) {ps->setCallbackUniform(location,{});args.rval().setUndefined();return true;}
    JS::RootedObject receiver(cx,&args.thisv().toObject());
    auto callback=Callback::create(cx,args[1],receiver);if(!callback)return false;
    auto s=state(cx);std::weak_ptr<State> weak=s;WeakPtr<rhi::ProgramState> owner(ps);
    ps->setCallbackUniform(location,[weak,owner,name,callback](rhi::ProgramState* executing,const rhi::UniformLocation&) {
        auto s=weak.lock();auto* native=owner.get();if(!s||!native||native!=executing||!callback->isValid())return;
        auto vm=callback->state();if(!vm||!vm->onOwnerThread())return;
        auto* cx=vm->context;
        auto found=s->nativePrograms.find(native);
        if(found==s->nativePrograms.end()||found->second->getState()!=native)
        {
            if(found!=s->nativePrograms.end())s->nativePrograms.erase(found);
            auto peer=RefPtr<Program>(new Program,tlx::adopt_object);peer->borrowed=native;peer->borrowedState=true;
            found=s->nativePrograms.emplace(native,std::move(peer)).first;
        }
        auto* peer=found->second.get();auto* uniform=peer->uniform(name);if(!uniform)return;
        JS::RootedValue program(cx),slot(cx);JS::RootedObject info(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
        if(!info||!wrapBorrowed(cx,peer,&program,LifetimeToken(native))||!wrapBorrowed(cx,uniform,&slot,LifetimeToken(native))||!property(cx,info,"location",slot))return;
        JS::AutoValueVector values(cx);JS::RootedValue description(cx,JS::ObjectValue(*info));
        if(values.append(program)&&values.append(description))callback->call(JS::HandleValueArray(values));
    });
    s->uniformCallbacks.push_back({owner,location});args.rval().setUndefined();return true;
}
template<class T> bool textureParameters(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);auto* texture=self<T>(cx,args);int minimum,maximum,s,t;
    if(!texture||argc!=4||!argument(cx,args,0,minimum)||!argument(cx,args,1,maximum)||!argument(cx,args,2,s)||!argument(cx,args,3,t))return false;
    rhi::SamplerDesc desc;
    if(minimum!=NEAREST&&minimum!=LINEAR)return error(cx,"Unsupported legacy minification filter");
    if(maximum!=NEAREST&&maximum!=LINEAR)return error(cx,"Unsupported legacy magnification filter");
    desc.minFilter=minimum==NEAREST?rhi::SamplerFilter::MIN_NEAREST:rhi::SamplerFilter::MIN_LINEAR;
    desc.magFilter=maximum==NEAREST?rhi::SamplerFilter::MAG_NEAREST:rhi::SamplerFilter::MAG_LINEAR;
    auto address=[](int value,rhi::SamplerAddressMode& out) {if(value==REPEAT)out=rhi::SamplerAddressMode::REPEAT;else if(value==MIRRORED_REPEAT)out=rhi::SamplerAddressMode::MIRROR;else if(value==CLAMP_TO_EDGE)out=rhi::SamplerAddressMode::CLAMP;else return false;return true;};
    rhi::SamplerAddressMode ss,tt;if(!address(s,ss)||!address(t,tt))return error(cx,"Unsupported legacy texture address mode");
    desc.sAddressMode=ss;desc.tAddressMode=tt;texture->setTexParameters(desc);args.rval().setUndefined();return true;
}
bool textureHandle(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(!self<Texture2D>(cx,args))return false;
    // The canonical native Texture2D wrapper is an opaque handle on every RHI.
    // Do not expose a fabricated GLuint or a Vulkan/Metal address as an integer.
    args.rval().set(args.thisv());return true;
}
template<class T,class Base=Object> bool registerClass(JSContext* cx,JS::HandleObject module,const char* name,JSNative constructor,const JSFunctionSpec* methods=nullptr,const JSFunctionSpec* statics=nullptr)
{
    auto& clasp=nativeClass<T>(name);JS::RootedObject parent(cx,getPrototype<Base>(cx));
    JS::RootedObject prototype(cx,JS_InitClass(cx,module,parent,&clasp,constructor,0,nullptr,methods,nullptr,statics));
    return prototype&&registerType<T,Base>(cx,prototype,&clasp);
}
} // namespace axjs::legacygl

namespace axjs
{
bool findLegacyShaderArchives(std::string_view vertexSource,std::string_view fragmentSource,std::string_view defines,std::string& vertexPath,std::string& fragmentPath)
{
    std::map<std::string,std::string> values;std::istringstream definitions{std::string(defines)};std::string segment;
    while(std::getline(definitions,segment,';'))
    {
        std::replace(segment.begin(),segment.end(),'=',' ');std::istringstream tokens(segment);std::string name,value;
        if(!(tokens>>name))continue;if(!(tokens>>value))value="1";values[name]=value;
    }
    std::string key;for(const auto& [name,value]:values) {if(!key.empty())key+=';';key+=name+'='+value;}
    vertexPath.clear();fragmentPath.clear();
    for(const auto& archive:legacygl::archives)
    {
        if(archive.defines!=key)continue;
        if(archive.vertex&&archive.source==vertexSource)vertexPath=archive.path;
        else if(!archive.vertex&&archive.source==fragmentSource)fragmentPath=archive.path;
    }
    return !vertexPath.empty()&&!fragmentPath.empty();
}
bool prepareLegacyShaderProgramAliases(std::string_view vertexPath,std::string_view fragmentPath)
{
    auto* files=ax::FileUtils::getInstance();
    if(!ax::rhi::GraphicsCore::device()||!files->isFileExist(vertexPath)||!files->isFileExist(fragmentPath))return false;
    auto* program=ax::ProgramManager::getInstance()->loadProgram(vertexPath,fragmentPath);
    if(!program||!program->isValid())return false;
    program->addUniformAlias("u_tex0","u_sampler0");return true;
}
bool configureLegacyShaderProgramState(JSContext* cx,ax::rhi::ProgramState* ps)
{
    auto s=legacygl::state(cx);
    if(!s||!ps||!ps->getProgram())return manual::error(cx,"Legacy shader state requires a live VM and native program");
    legacygl::configureLegacyFragmentCoordinates(ps);
    const char* names[]={"CC_Time","CC_SinTime","CC_CosTime","CC_Random01"};
    for(unsigned kind=0;kind<std::size(names);++kind)
    {
        ax::rhi::UniformLocationVector locations;ps->getUniformLocations(names[kind],locations);
        if(locations.empty())continue;
        const auto first=locations.front();
        // A previously installed custom callback remains the caller's choice.
        if(ps->getCallbackUniforms().contains(first))continue;
        std::weak_ptr<legacygl::State> weak=s;ax::WeakPtr<ax::rhi::ProgramState> owner(ps);
        ps->setCallbackUniform(first,[weak,owner,locations,kind](ax::rhi::ProgramState* executing,const ax::rhi::UniformLocation&) {
            auto state=weak.lock();if(!state||owner.get()!=executing)return;
            auto vm=state->vm.lock();if(!vm||!vm->alive||!vm->onOwnerThread())return;
            auto* director=ax::Director::getInstance();
            const float time=static_cast<float>(director->getTotalFrames()*director->getAnimationInterval());
            std::array<float,4> value;
            if(kind==0)value={time/10,time,time*2,time*4};
            else if(kind==1)value={time/8,time/4,time/2,std::sin(time)};
            else if(kind==2)value={time/8,time/4,time/2,std::cos(time)};
            else for(auto& component:value)component=static_cast<float>(static_cast<double>(std::rand())/RAND_MAX);
            for(const auto& location:locations)executing->setUniform(location,value.data(),sizeof(value));
        });
        s->uniformCallbacks.push_back({owner,first});
    }
    return true;
}
bool registerLegacyGL(JSContext* cx,JS::HandleObject global)
{
    using namespace legacygl;
    if(states.contains(cx))return true;
    auto s=std::make_shared<State>();s->vm=runtimeState(cx);states[cx]=s;
    auto* director=Director::getInstance();s->dispatcher=director->getEventDispatcher();
    // Opt in before the first drawable, rather than waiting for a JS read or
    // EVENT_BEFORE_DRAW (Scheduler can read the preceding frame before that).
    s->ensureReadback(director->getRenderer());
    engine(cx)->addCleanupCallback([cx,s] {
        if(auto* dispatcher=s->dispatcher.get()) {
            if(s->graphicsDrop)dispatcher->removeEventListener(s->graphicsDrop.get());
            if(s->beforeDraw)dispatcher->removeEventListener(s->beforeDraw.get());
        }
        s->graphicsDrop.reset();s->beforeDraw.reset();s->clearGraphics();states.erase(cx);
    });
    JS::RootedObject cc(cx,namespaceObject(cx,global,"cc")),gl(cx,namespaceObject(cx,global,"gl"));
    JS::RootedObject jsb(cx,namespaceObject(cx,global,"jsb"));
    if(!cc||!gl||!jsb||!function(cx,jsb,"_prepareLegacySprite",prepareLegacySprite,1)||
       !function(cx,jsb,"_prepareLegacyAtlasNode",prepareLegacyAtlasNode,1))return false;
    const JSFunctionSpec programMethods[]={JS_FN("link",programCall<ProgramOperation::Link>,0,0),JS_FN("use",programCall<ProgramOperation::Use>,0,0),JS_FN("updateUniforms",programCall<ProgramOperation::UpdateUniforms>,0,0),JS_FN("setUniformsForBuiltins",programCall<ProgramOperation::Builtins>,0,0),JS_FN("getProgram",programCall<ProgramOperation::GetProgram>,0,0),JS_FN("getUniformLocationForName",programCall<ProgramOperation::Location>,1,0),JS_FN("addAttribute",programCall<ProgramOperation::Attribute>,2,0),JS_FN("setUniformLocationF32",programCall<ProgramOperation::Uniform>,2,0),JS_FN("setUniformLocationWith1f",programCall<ProgramOperation::Uniform>,2,0),JS_FN("setUniformLocationWith3f",programCall<ProgramOperation::Uniform>,4,0),JS_FS_END};
    const JSFunctionSpec programStatics[]={JS_FN("create",programCall<ProgramOperation::Create>,2,0),JS_FS_END};
    const JSFunctionSpec nodeMethods[]={JS_FN("draw",nodeBaseDraw,0,0),JS_FS_END};
    const JSFunctionSpec nodeStatics[]={JS_FN("__allocate",allocate<GLNode>,0,0),JS_FS_END};
    if(!registerClass<Buffer>(cx,cc,"__GLBuffer",nullptr)||!registerClass<Shader>(cx,cc,"__GLShader",nullptr)||
       !registerClass<Texture>(cx,cc,"__GLTexture",nullptr)||!registerClass<Uniform>(cx,cc,"__GLUniform",nullptr)||
       !registerClass<Program>(cx,cc,"GLProgram",programConstructor,programMethods,programStatics)||
       !registerClass<GLNode,Node>(cx,cc,"GLNode",nodeConstructor,nodeMethods,nodeStatics))return false;
#define AXGL(name,op,arity) if(!function(cx,gl,name,glCall<Operation::op>,arity))return false
    AXGL("createBuffer",CreateBuffer,0);AXGL("bindBuffer",BindBuffer,2);AXGL("bufferData",BufferData,3);
    AXGL("createShader",CreateShader,1);AXGL("shaderSource",ShaderSource,2);AXGL("compileShader",CompileShader,1);
    AXGL("getShaderParameter",ShaderParameter,2);AXGL("getShaderInfoLog",ShaderLog,1);
    AXGL("createProgram",CreateProgram,0);AXGL("attachShader",AttachShader,2);AXGL("linkProgram",LinkProgram,1);
    AXGL("getProgramParameter",ProgramParameter,2);AXGL("useProgram",UseProgram,1);AXGL("getAttribLocation",AttribLocation,2);
    AXGL("enableVertexAttribArray",EnableAttrib,1);AXGL("disableVertexAttribArray",DisableAttrib,1);AXGL("vertexAttribPointer",AttribPointer,6);
    AXGL("getUniformLocation",UniformLocation,2);AXGL("uniformMatrix4fv",UniformMatrix,3);
    AXGL("uniform1f",Uniform1,2);AXGL("uniform2f",Uniform2,3);AXGL("uniform3f",Uniform3,4);AXGL("uniform4f",Uniform4,5);AXGL("getUniform",UniformRead,2);
    AXGL("drawArrays",DrawArrays,3);AXGL("createTexture",CreateTexture,0);AXGL("bindTexture",BindTexture,2);
    AXGL("texImage2D",TexImage,9);AXGL("texParameteri",TexParameter,3);AXGL("getTexParameter",GetTexParameter,2);
    AXGL("clear",Clear,1);AXGL("clearColor",ClearColor,4);AXGL("clearDepth",ClearDepth,1);AXGL("clearStencil",ClearStencil,1);
    AXGL("getActiveAttrib",ActiveAttrib,2);AXGL("getActiveUniform",ActiveUniform,2);AXGL("getSupportedExtensions",Extensions,0);AXGL("getExtension",Extension,1);AXGL("readPixels",ReadPixels,7);
    AXGL("deleteBuffer",DeleteBuffer,1);AXGL("deleteShader",DeleteShader,1);AXGL("deleteProgram",DeleteProgram,1);AXGL("deleteTexture",DeleteTexture,1);
#undef AXGL
    const std::pair<const char*,int> constants[]={{"ARRAY_BUFFER",ARRAY_BUFFER},{"STATIC_DRAW",STATIC_DRAW},{"DYNAMIC_DRAW",DYNAMIC_DRAW},{"VERTEX_SHADER",VERTEX_SHADER},{"FRAGMENT_SHADER",FRAGMENT_SHADER},{"COMPILE_STATUS",COMPILE_STATUS},{"LINK_STATUS",LINK_STATUS},{"ACTIVE_ATTRIBUTES",0x8b89},{"ACTIVE_UNIFORMS",0x8b86},{"TEXTURE_2D",TEXTURE_2D},{"RGBA",RGBA},{"RGB",RGB},{"UNSIGNED_BYTE",UNSIGNED_BYTE},{"FLOAT",FLOAT},{"TRIANGLES",4},{"TRIANGLE_STRIP",5},{"LINES",1},{"POINTS",0},{"COLOR_BUFFER_BIT",0x4000},{"DEPTH_BUFFER_BIT",0x0100},{"STENCIL_BUFFER_BIT",0x0400},{"TEXTURE_MAG_FILTER",TEXTURE_MAG_FILTER},{"TEXTURE_MIN_FILTER",TEXTURE_MIN_FILTER},{"TEXTURE_WRAP_S",TEXTURE_WRAP_S},{"TEXTURE_WRAP_T",TEXTURE_WRAP_T},{"NEAREST",NEAREST},{"LINEAR",LINEAR},{"CLAMP_TO_EDGE",CLAMP_TO_EDGE},{"REPEAT",REPEAT},{"MIRRORED_REPEAT",MIRRORED_REPEAT},{"DEPTH24_STENCIL8_OES",0x88f0}};
    for(const auto& entry:constants)if(!property(cx,gl,entry.first,entry.second))return false;
    JS::RootedObject cache(cx,namespaceObject(cx,cc,"shaderCache")),configuration(cx,namespaceObject(cx,cc,"configuration")),programState(cx,getClassObject<rhi::ProgramState>(cx));
    JS::RootedValue programStateValue(cx,programState?JS::ObjectValue(*programState):JS::UndefinedValue());
    if(!cache||!configuration||!programState||!function(cx,cache,"getProgram",builtinProgram,1)||!function(cx,configuration,"getMaxTextureSize",maxTextureSize,0)||
       !function(cx,programState,"getOrCreateWithGLProgram",programStateFactory<false>,1)||
       !function(cx,programState,"create",programStateFactory<true>,1)||!property(cx,cc,"GLProgramState",programStateValue)||
       !method<rhi::ProgramState>(cx,"setUniformFloat",programStateUniform<float>,2)||
       !method<rhi::ProgramState>(cx,"setUniformInt",programStateUniform<int>,2)||
       !method<rhi::ProgramState>(cx,"setUniformVec2",programStateUniform<Vec2>,2)||
       !method<rhi::ProgramState>(cx,"setUniformVec3",programStateUniform<Vec3>,2)||
       !method<rhi::ProgramState>(cx,"setUniformVec4",programStateUniform<Vec4>,2)||
       !method<rhi::ProgramState>(cx,"setUniformMat4",programStateUniform<Mat4>,2)||
       !method<rhi::ProgramState>(cx,"setUniformTexture",programStateTexture,2)||
       !method<rhi::ProgramState>(cx,"setVertexAttribPointer",programStateVertexPointer,6)||
       !method<rhi::ProgramState>(cx,"setUniformCallback",programStateUniformCallback,2)||
       !method<Texture2D>(cx,"setTexParameters",textureParameters<Texture2D>,4)||
       !method<Texture2D>(cx,"getName",textureHandle,0)||
       !method<Node>(cx,"getShaderProgram",nodeProgram<false>)||!method<Node>(cx,"setShaderProgram",nodeProgram<true>,1)||
       !method<Node>(cx,"getGLProgramState",nodeProgramState<false>)||!method<Node>(cx,"setGLProgramState",nodeProgramState<true>,1))return false;
#if AX_ENABLE_3D
    if(!method<TextureCube>(cx,"setTexParameters",textureParameters<TextureCube>,4)||
       !method<Mesh>(cx,"getMeshVertexAttribute",meshVertexAttribute,1))return false;
#endif
    std::weak_ptr<State> weak=s;
    auto* listener=CustomEventListener::create(Director::EVENT_BEFORE_GFX_DROP,[weak](CustomEvent*) {if(auto state=weak.lock())state->clearGraphics();});
    s->graphicsDrop=listener;Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(listener,1);
    auto* before=CustomEventListener::create(Director::EVENT_BEFORE_DRAW,[weak](CustomEvent* event) {
        if(auto state=weak.lock();state&&rhi::GraphicsCore::device())
            if(auto* owner=static_cast<Director*>(event->getUserData()))state->ensureReadback(owner->getRenderer());
    });
    s->beforeDraw=before;Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(before,1);
    return true;
}
} // namespace axjs
