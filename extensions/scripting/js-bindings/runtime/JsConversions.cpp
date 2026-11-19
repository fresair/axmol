/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include <array>

namespace axjs
{
namespace
{
template<class T> bool read(JSContext* cx,JS::HandleValue object,const char* field,T& out)
{
    JS::RootedValue value(cx);
    return detail::readProperty(cx,object,field,&value) && fromJsValue(cx,value,out);
}
// v3 jsval_to_ccpoint used JS::ToNumber, so original sources routinely pass
// numeric strings through cc.p() (e.g. Number.toFixed in UIImageViewTest).
// Coerce the same way but keep NaN/Infinity rejected so malformed values still
// fail instead of silently becoming garbage geometry.
template<class T> bool readComponent(JSContext* cx,JS::HandleValue object,const char* field,T& out)
{
    JS::RootedValue value(cx);
    double number=0;
    if(!detail::readProperty(cx,object,field,&value) || !JS::ToNumber(cx,value,&number) ||
        !std::isfinite(number)) return false;
    out=static_cast<T>(number);
    return true;
}
template<class T> bool write(JSContext* cx,JS::HandleObject object,const char* field,const T& in)
{
    JS::RootedValue value(cx);
    return toJsValue(cx,in,&value) && JS_DefineProperty(cx,object,field,value,JSPROP_ENUMERATE);
}
JSObject* object(JSContext* cx) { return JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()); }
bool colorByte(JSContext* cx,JS::HandleValue value,uint8_t& out)
{
    if(!value.isNumber() || !std::isfinite(value.toNumber())) return false;
    // Legacy cc.color accepts fractional channels (for example cc.rand()%255).
    // Give the old integer-to-byte conversion defined truncation/wrap behavior,
    // while identifiers and other integral arguments remain strictly checked.
    double byte=std::fmod(std::trunc(value.toNumber()),256.0);
    if(byte<0) byte+=256.0;
    out=static_cast<uint8_t>(byte);
    return true;
}
bool readColorByte(JSContext* cx,JS::HandleValue value,const char* key,uint8_t& out)
{
    JS::RootedValue channel(cx);
    return detail::readProperty(cx,value,key,&channel) && colorByte(cx,channel,out);
}
}
bool Converter<ax::Vec2>::read(JSContext* cx,JS::HandleValue value,ax::Vec2& out)
{
    if(!value.isObject()) return false;
    ax::Vec2 temp;
    if(!axjs::readComponent(cx,value,"x",temp.x) && !axjs::readComponent(cx,value,"width",temp.x)) return false;
    if(!axjs::readComponent(cx,value,"y",temp.y) && !axjs::readComponent(cx,value,"height",temp.y)) return false;
    out=temp; return true;
}
bool Converter<ax::Vec2>::write(JSContext* cx,const ax::Vec2& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"x",v.x) || !axjs::write(cx,obj,"y",v.y) ||
        !axjs::write(cx,obj,"width",v.width) || !axjs::write(cx,obj,"height",v.height)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Vec3>::read(JSContext* cx,JS::HandleValue value,ax::Vec3& out)
{
    ax::Vec3 temp;
    if(!axjs::readComponent(cx,value,"x",temp.x) || !axjs::readComponent(cx,value,"y",temp.y) ||
        !axjs::readComponent(cx,value,"z",temp.z)) return false;
    out=temp; return true;
}
bool Converter<ax::Vec3>::write(JSContext* cx,const ax::Vec3& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"x",v.x) || !axjs::write(cx,obj,"y",v.y) || !axjs::write(cx,obj,"z",v.z)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Vec4>::read(JSContext* cx,JS::HandleValue value,ax::Vec4& out)
{
    ax::Vec4 temp;
    if(!axjs::readComponent(cx,value,"x",temp.x) || !axjs::readComponent(cx,value,"y",temp.y) ||
        !axjs::readComponent(cx,value,"z",temp.z) || !axjs::readComponent(cx,value,"w",temp.w)) return false;
    out=temp; return true;
}
bool Converter<ax::Vec4>::write(JSContext* cx,const ax::Vec4& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"x",v.x) || !axjs::write(cx,obj,"y",v.y) ||
        !axjs::write(cx,obj,"z",v.z) || !axjs::write(cx,obj,"w",v.w)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Rect>::read(JSContext* cx,JS::HandleValue value,ax::Rect& out)
{
    ax::Rect temp;
    if(axjs::readComponent(cx,value,"x",temp.origin.x) && axjs::readComponent(cx,value,"y",temp.origin.y) &&
        axjs::readComponent(cx,value,"width",temp.size.width) &&
        axjs::readComponent(cx,value,"height",temp.size.height))
    { out=temp; return true; }
    if(!axjs::read(cx,value,"origin",temp.origin) || !axjs::read(cx,value,"size",temp.size)) return false;
    out=temp; return true;
}
bool Converter<ax::Rect>::write(JSContext* cx,const ax::Rect& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"x",v.origin.x) || !axjs::write(cx,obj,"y",v.origin.y) ||
        !axjs::write(cx,obj,"width",v.size.width) || !axjs::write(cx,obj,"height",v.size.height)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Color32>::read(JSContext* cx,JS::HandleValue value,ax::Color32& out)
{
    ax::Color32 temp{0,0,0,255};
    if(!readColorByte(cx,value,"r",temp.r) || !readColorByte(cx,value,"g",temp.g) || !readColorByte(cx,value,"b",temp.b)) return false;
    JS::RootedValue alpha(cx);
    if(!value.isObject()) return false;
    JS::RootedObject obj(cx,&value.toObject());
    if(!JS_GetProperty(cx,obj,"a",&alpha)) return false;
    if(!alpha.isUndefined() && !colorByte(cx,alpha,temp.a)) return false;
    out=temp; return true;
}
bool Converter<ax::Color32>::write(JSContext* cx,const ax::Color32& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"r",v.r) || !axjs::write(cx,obj,"g",v.g) ||
        !axjs::write(cx,obj,"b",v.b) || !axjs::write(cx,obj,"a",v.a)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Color>::read(JSContext* cx,JS::HandleValue value,ax::Color& out)
{
    ax::Color temp{0.f,0.f,0.f,255.f};
    if(!axjs::read(cx,value,"r",temp.r) || !axjs::read(cx,value,"g",temp.g) || !axjs::read(cx,value,"b",temp.b)) return false;
    JS::RootedValue alpha(cx); JS::RootedObject obj(cx,&value.toObject());
    if(!JS_GetProperty(cx,obj,"a",&alpha)) return false;
    if(!alpha.isUndefined() && !fromJsValue(cx,alpha,temp.a)) return false;
    out=ax::Color{temp.r/255.f,temp.g/255.f,temp.b/255.f,temp.a/255.f}; return true;
}
bool Converter<ax::Color>::write(JSContext* cx,const ax::Color& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"r",std::trunc(v.r*255.f)) || !axjs::write(cx,obj,"g",std::trunc(v.g*255.f)) ||
        !axjs::write(cx,obj,"b",std::trunc(v.b*255.f)) || !axjs::write(cx,obj,"a",std::trunc(v.a*255.f))) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::rhi::BlendFactor>::read(JSContext* cx,JS::HandleValue value,ax::rhi::BlendFactor& out)
{
    uint32_t factor;
    if(!fromJsValue(cx,value,factor)) return false;
    using F=ax::rhi::BlendFactor;
    switch(factor)
    {
    case 0: out=F::ZERO; break;
    case 1: out=F::ONE; break;
    case 0x0300: out=F::SRC_COLOR; break;
    case 0x0301: out=F::ONE_MINUS_SRC_COLOR; break;
    case 0x0302: out=F::SRC_ALPHA; break;
    case 0x0303: out=F::ONE_MINUS_SRC_ALPHA; break;
    case 0x0304: out=F::DST_ALPHA; break;
    case 0x0305: out=F::ONE_MINUS_DST_ALPHA; break;
    case 0x0306: out=F::DST_COLOR; break;
    case 0x0307: out=F::ONE_MINUS_DST_COLOR; break;
    case 0x0308: out=F::SRC_ALPHA_SATURATE; break;
    case 0x8001: out=F::BLEND_COLOR; break;
    case 0x8003: out=F::CONSTANT_ALPHA; break;
    case 0x8004: out=F::ONE_MINUS_CONSTANT_ALPHA; break;
    default:
        if(factor>static_cast<uint32_t>(F::BLEND_COLOR)) return false;
        out=static_cast<F>(factor);
    }
    return true;
}
bool Converter<ax::rhi::BlendFactor>::write(JSContext* cx,const ax::rhi::BlendFactor& value,JS::MutableHandleValue out)
{
    using F=ax::rhi::BlendFactor;
    uint32_t factor;
    switch(value)
    {
    case F::ZERO: factor=0; break;
    case F::ONE: factor=1; break;
    case F::SRC_COLOR: factor=0x0300; break;
    case F::ONE_MINUS_SRC_COLOR: factor=0x0301; break;
    case F::SRC_ALPHA: factor=0x0302; break;
    case F::ONE_MINUS_SRC_ALPHA: factor=0x0303; break;
    case F::DST_ALPHA: factor=0x0304; break;
    case F::ONE_MINUS_DST_ALPHA: factor=0x0305; break;
    case F::DST_COLOR: factor=0x0306; break;
    case F::ONE_MINUS_DST_COLOR: factor=0x0307; break;
    case F::SRC_ALPHA_SATURATE: factor=0x0308; break;
    case F::BLEND_COLOR: factor=0x8001; break;
    case F::CONSTANT_ALPHA: factor=0x8003; break;
    case F::ONE_MINUS_CONSTANT_ALPHA: factor=0x8004; break;
    default: return false;
    }
    return toJsValue(cx,factor,out);
}
bool Converter<ax::BlendFunc>::read(JSContext* cx,JS::HandleValue value,ax::BlendFunc& out)
{ return axjs::read(cx,value,"src",out.src) && axjs::read(cx,value,"dst",out.dst); }
bool Converter<ax::BlendFunc>::write(JSContext* cx,const ax::BlendFunc& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"src",v.src) || !axjs::write(cx,obj,"dst",v.dst)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::AffineTransform>::read(JSContext* cx,JS::HandleValue value,ax::AffineTransform& out)
{
    ax::AffineTransform temp{};
    if(!axjs::read(cx,value,"a",temp.a) || !axjs::read(cx,value,"b",temp.b) || !axjs::read(cx,value,"c",temp.c) ||
        !axjs::read(cx,value,"d",temp.d) || !axjs::read(cx,value,"tx",temp.tx) || !axjs::read(cx,value,"ty",temp.ty)) return false;
    out=temp; return true;
}
bool Converter<ax::AffineTransform>::write(JSContext* cx,const ax::AffineTransform& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,object(cx));
    if(!obj || !axjs::write(cx,obj,"a",v.a) || !axjs::write(cx,obj,"b",v.b) || !axjs::write(cx,obj,"c",v.c) ||
        !axjs::write(cx,obj,"d",v.d) || !axjs::write(cx,obj,"tx",v.tx) || !axjs::write(cx,obj,"ty",v.ty)) return false;
    out.setObject(*obj); return true;
}
bool Converter<ax::Mat4>::read(JSContext* cx,JS::HandleValue value,ax::Mat4& out)
{
    if(!value.isObject()) return false;
    JS::RootedObject array(cx,&value.toObject());
    uint32_t length{};
    if(!JS_IsArrayObject(cx,array) || !JS_GetArrayLength(cx,array,&length) || length!=16) return false;
    ax::Mat4 temp;
    for(uint32_t i=0;i<16;++i)
    { JS::RootedValue v(cx); if(!JS_GetElement(cx,array,i,&v) || !fromJsValue(cx,v,temp.m[i])) return false; }
    out=temp; return true;
}
bool Converter<ax::Mat4>::write(JSContext* cx,const ax::Mat4& v,JS::MutableHandleValue out)
{
    JS::RootedObject array(cx,JS_NewArrayObject(cx,16));
    if(!array) return false;
    for(uint32_t i=0;i<16;++i) if(!JS_SetElement(cx,array,i,static_cast<double>(v.m[i]))) return false;
    out.setObject(*array); return true;
}
namespace
{
thread_local unsigned valueDepth{};
struct ValueDepth
{
    ValueDepth(){++valueDepth;}
    ~ValueDepth(){--valueDepth;}
};
}
bool Converter<ax::Value>::read(JSContext* cx,JS::HandleValue value,ax::Value& out)
{
    ValueDepth depth;
    if(valueDepth>128) { JS_ReportError(cx,"Value conversion exceeds maximum depth (cyclic object)"); return false; }
    if(value.isNullOrUndefined()) out=ax::Value();
    else if(value.isBoolean()) out=ax::Value(value.toBoolean());
    else if(value.isInt32()) out=ax::Value(value.toInt32());
    else if(value.isNumber()) out=ax::Value(value.toNumber());
    else if(value.isString())
    { std::string text; if(!detail::stringFromValue(cx,value,text)) return false; out=ax::Value(std::move(text)); }
    else if(value.isObject())
    {
        JS::RootedObject obj(cx,&value.toObject());
        if(JS_IsArrayObject(cx,obj))
        { ax::ValueVector vector; if(!fromJsValue(cx,value,vector)) return false; out=ax::Value(std::move(vector)); }
        else
        { ax::ValueMap map; if(!fromJsValue(cx,value,map)) return false; out=ax::Value(std::move(map)); }
    }
    else return false;
    return true;
}
bool Converter<ax::Value>::write(JSContext* cx,const ax::Value& value,JS::MutableHandleValue out)
{
    ValueDepth depth;
    if(valueDepth>128) { JS_ReportError(cx,"Value conversion exceeds maximum depth"); return false; }
    switch(value.getTypeFamily())
    {
    case ax::Value::Type::NONE: out.setNull(); return true;
    case ax::Value::Type::BOOLEAN: return toJsValue(cx,value.asBool(),out);
    case ax::Value::Type::INTEGER:
        switch(value.getType())
        {
        case ax::Value::Type::INT_UI32: return toJsValue(cx,value.asUint(),out);
        case ax::Value::Type::INT_I64: return toJsValue(cx,value.asInt64(),out);
        case ax::Value::Type::INT_UI64: return toJsValue(cx,value.asUint64(),out);
        default: return toJsValue(cx,value.asInt(),out);
        }
    case ax::Value::Type::FLOAT: return toJsValue(cx,value.asFloat(),out);
    case ax::Value::Type::DOUBLE: return toJsValue(cx,value.asDouble(),out);
    case ax::Value::Type::STRING: return toJsValue(cx,value.asStringRef(),out);
    case ax::Value::Type::VECTOR: return toJsValue(cx,value.asValueVector(),out);
    case ax::Value::Type::MAP: return toJsValue(cx,value.asValueMap(),out);
    case ax::Value::Type::INT_KEY_MAP: return toJsValue(cx,value.asIntKeyMap(),out);
    default: return false;
    }
}
} // namespace axjs
