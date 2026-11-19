/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyPolygonBindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/2d/AutoPolygon.h"
#include "axmol/2d/Sprite.h"
#include "axmol/base/Director.h"
#include "axmol/platform/Image.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>

namespace axjs
{
namespace
{
bool failure(JSContext* cx,const char* message)
{
    if(!JS_IsExceptionPending(cx))JS_ReportError(cx,"%s",message);
    return false;
}
template<class T>
bool owned(JSContext* cx,std::unique_ptr<T> native,JS::MutableHandleValue out)
{
    JS::RootedObject prototype(cx,getPrototype<T>(cx));
    JS::RootedObject wrapper(cx,JS_NewObject(cx,detail::getClassInfo(cx,typeid(T)),prototype,JS::NullPtr()));
    if(!wrapper||!bindNative(cx,wrapper,native.get(),NativeOwnership::Constructor))return false;
    native.release();out.setObject(*wrapper);return true;
}
bool number(JSContext* cx,JS::HandleValue value,float& out)
{
    double parsed{};
    if(!JS::ToNumber(cx,value,&parsed))return false;
    if(!std::isfinite(parsed)||parsed>std::numeric_limits<float>::max()||parsed<-std::numeric_limits<float>::max())
        return failure(cx,"AutoPolygon numeric arguments must be finite");
    out=static_cast<float>(parsed);return true;
}
// The native tracing algorithm assumes an existing RGBA image, in-bounds rect
// and at least one opaque pixel. Check those actual data requirements before
// entering its assertion-only path; do not fabricate geometry on failure.
bool validateImage(JSContext* cx,const std::string& file,const ax::Rect& rect,float epsilon,float threshold)
{
    if(file.empty())return failure(cx,"AutoPolygon requires an image filename");
    ax::Image image;
    if(!image.initWithImageFile(file)||image.getWidth()<=0||image.getHeight()<=0||
       image.getPixelFormat()!=ax::rhi::PixelFormat::RGBA8)
        return failure(cx,"AutoPolygon requires an existing RGBA8 image");
    if(!std::isfinite(epsilon)||epsilon<0||!std::isfinite(threshold)||threshold<0||threshold>=255)
        return failure(cx,"AutoPolygon epsilon or alpha threshold is outside the native tracing range");
    const auto scale=ax::Director::getInstance()->getContentScaleFactor();
    const auto actual=rect.equals(ax::Rect::zero)?ax::Rect(0,0,image.getWidth(),image.getHeight()):
        ax::Rect(rect.origin*scale,rect.size*scale);
    if(!std::isfinite(actual.origin.x)||!std::isfinite(actual.origin.y)||
       !std::isfinite(actual.size.width)||!std::isfinite(actual.size.height)||
       actual.origin.x<0||actual.origin.y<0||actual.size.width<=0||actual.size.height<=0||
       actual.getMaxX()>image.getWidth()||actual.getMaxY()>image.getHeight())
        return failure(cx,"AutoPolygon rect must be inside the decoded image");
    auto* data=image.getData();bool hasOpaquePixel=false;
    for(float y=actual.origin.y;y<actual.getMaxY()&&!hasOpaquePixel;++y)
        for(float x=actual.origin.x;x<actual.getMaxX();++x)
            if(data[(static_cast<size_t>(y)*image.getWidth()+static_cast<size_t>(x))*4+3]>threshold)
            {hasOpaquePixel=true;break;}
    return hasOpaquePixel||failure(cx,"AutoPolygon rect contains no pixels above its alpha threshold");
}
bool generatePolygon(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);std::string file;ax::Rect rect=ax::Rect::zero;
    float epsilon=2,threshold=.05f;
    if(argc<1||argc>4||!fromJsValue(cx,args[0],file)||
       (argc>1&&!fromJsValue(cx,args[1],rect))||(argc>2&&!number(cx,args[2],epsilon))||
       (argc>3&&!number(cx,args[3],threshold)))
        return failure(cx,"AutoPolygon.generatePolygon requires file[,rect[,epsilon[,threshold]]]");
    if(!validateImage(cx,file,rect,epsilon,threshold))return false;
    try {
        return owned(cx,std::make_unique<ax::PolygonInfo>(ax::AutoPolygon::generatePolygon(file,rect,epsilon,threshold)),args.rval());
    } catch(const std::exception& error) {return failure(cx,error.what());}
}
bool createAutoPolygon(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);std::string file;
    if(argc!=1||!fromJsValue(cx,args[0],file))return failure(cx,"AutoPolygon constructor requires one image filename");
    if(!validateImage(cx,file,ax::Rect::zero,2,.05f))return false;
    try {return owned(cx,std::make_unique<ax::AutoPolygon>(file),args.rval());}
    catch(const std::exception& error) {return failure(cx,error.what());}
}
template<auto Function>
bool polygonMethod(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    return invoke<Function>(cx,args)||failure(cx,"PolygonInfo method requires a live native receiver and zero arguments");
}
bool polygonRectGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::PolygonInfo* native{};
    if(argc||!toNative(cx,args.thisv(),&native)||!native)return failure(cx,"PolygonInfo.rect requires a live native receiver");
    return toJsValue(cx,native->getRect(),args.rval());
}
bool polygonRectSet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::PolygonInfo* native{};ax::Rect value;
    if(argc!=1||!toNative(cx,args.thisv(),&native)||!native||!fromJsValue(cx,args[0],value))
        return failure(cx,"PolygonInfo.rect requires a live receiver and a rect");
    native->setRect(value);args.rval().setUndefined();return true;
}
bool polygonFileGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::PolygonInfo* native{};
    if(argc||!toNative(cx,args.thisv(),&native)||!native)return failure(cx,"PolygonInfo.filename requires a live native receiver");
    return toJsValue(cx,native->getFilename(),args.rval());
}
bool polygonFileSet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::PolygonInfo* native{};std::string value;
    if(argc!=1||!toNative(cx,args.thisv(),&native)||!native||!fromJsValue(cx,args[0],value))
        return failure(cx,"PolygonInfo.filename requires a live receiver and a filename");
    native->setFilename(value);args.rval().setUndefined();return true;
}
bool spritePolygonCreate(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::PolygonInfo* polygon{};
    if(argc!=1||!toNative(cx,args[0],&polygon)||!polygon)return failure(cx,"Sprite.createWithPolygon requires a PolygonInfo");
    return wrapNative(cx,ax::Sprite::create(*polygon),args.rval(),NativeOwnership::Factory);
}
template<bool Init>
bool spritePolygonSet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Sprite* sprite{};ax::PolygonInfo* polygon{};
    if(argc!=1||!toNative(cx,args.thisv(),&sprite)||!sprite||!toNative(cx,args[0],&polygon)||!polygon)
        return failure(cx,"Sprite polygon operation requires a live Sprite and PolygonInfo");
    if constexpr(Init)args.rval().setBoolean(sprite->initWithPolygon(*polygon));
    else {sprite->setPolygonInfo(*polygon);args.rval().setUndefined();}
    return true;
}
bool spritePolygonGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Sprite* sprite{};
    if(argc||!toNative(cx,args.thisv(),&sprite)||!sprite)return failure(cx,"Sprite.getPolygonInfo requires a live Sprite");
    // Return an owned deep copy: Sprite quad geometry can refer into _quad and
    // polygon replacement can invalidate its previous buffers at any time.
    return owned(cx,std::make_unique<ax::PolygonInfo>(sprite->getPolygonInfo()),args.rval());
}
}
bool registerLegacyPolygon(JSContext* cx,JS::HandleObject global)
{
    JS::RootedObject module(cx);if(!getOrCreateNamespace(cx,global,"jsb",&module))return false;
    JS::RootedObject prototype(cx,getPrototype<ax::PolygonInfo>(cx));
    JS::RootedObject constructor(cx,getClassObject<ax::PolygonInfo>(cx));
    if(!prototype||!constructor)return failure(cx,"Generated PolygonInfo class must be registered first");
    JS::RootedValue alias(cx,JS::ObjectValue(*constructor));
    if(!JS_SetProperty(cx,module,"PolygonInfo",alias))return false;
    const JSFunctionSpec methods[]={
        JS_FN("getArea",polygonMethod<&ax::PolygonInfo::getArea>,0,0),
        JS_FN("getVertCount",polygonMethod<&ax::PolygonInfo::getVertCount>,0,0),
        JS_FN("getTrianglesCount",polygonMethod<&ax::PolygonInfo::getTrianglesCount>,0,0),
        JS_FN("getTriaglesCount",polygonMethod<&ax::PolygonInfo::getTrianglesCount>,0,0),JS_FS_END};
    const JSPropertySpec properties[]={
        JS_PSGS("rect",polygonRectGet,polygonRectSet,JSPROP_ENUMERATE),
        JS_PSGS("filename",polygonFileGet,polygonFileSet,JSPROP_ENUMERATE),JS_PS_END};
    // Preserve the canonical generated type/prototype and its JS-owned native
    // constructor. The legacy namespace and field syntax refer to that class.
    if(!JS_DefineFunctions(cx,prototype,methods)||!JS_DefineProperties(cx,prototype,properties))return false;
    JS::RootedObject automatic(cx,getClassObject<ax::AutoPolygon>(cx));
    JS::RootedObject sprite(cx,getClassObject<ax::Sprite>(cx));
    JS::RootedObject spritePrototype(cx,getPrototype<ax::Sprite>(cx));
    return automatic&&sprite&&spritePrototype&&
        JS_DefineFunction(cx,automatic,"generatePolygon",generatePolygon,1,0)&&
        JS_DefineFunction(cx,automatic,"create",createAutoPolygon,1,0)&&
        JS_DefineFunction(cx,sprite,"createWithPolygon",spritePolygonCreate,1,0)&&
        JS_DefineFunction(cx,spritePrototype,"initWithPolygon",spritePolygonSet<true>,1,0)&&
        JS_DefineFunction(cx,spritePrototype,"setPolygonInfo",spritePolygonSet<false>,1,0)&&
        JS_DefineFunction(cx,spritePrototype,"getPolygonInfo",spritePolygonGet,0,0);
}
} // namespace axjs
