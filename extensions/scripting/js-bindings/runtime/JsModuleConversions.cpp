// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "runtime/JsRuntime.h"
#if defined(AX_ENABLE_PHYSICS_3D)
#include "axmol/physics/3d/Collider3D.h"
#endif

namespace axjs
{
namespace
{
template<class T> bool read(JSContext* cx,JS::HandleValue value,const char* key,T& out)
{
    JS::RootedValue property(cx);
    return detail::readProperty(cx,value,key,&property) && fromJsValue(cx,property,out);
}
template<class T> bool readOptional(JSContext* cx,JS::HandleValue value,const char* key,T& out)
{
    if(!value.isObject()) return false;
    JS::RootedObject object(cx,&value.toObject());
    JS::RootedValue property(cx);
    return JS_GetProperty(cx,object,key,&property) && (property.isUndefined() || fromJsValue(cx,property,out));
}
template<class T> bool write(JSContext* cx,JS::HandleObject object,const char* key,const T& value)
{
    JS::RootedValue converted(cx);
    return toJsValue(cx,value,&converted) && JS_DefineProperty(cx,object,key,converted,JSPROP_ENUMERATE);
}
JSObject* newObject(JSContext* cx) { return JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()); }
template<class T> bool readRegisteredCopy(JSContext* cx,JS::HandleValue value,T& out)
{
    if(auto* native=static_cast<T*>(detail::unwrapNativeInfo(cx,value,typeid(T),false)))
    { out=*native; return true; }
    return false;
}
}

bool Converter<ax::Quat>::read(JSContext* cx,JS::HandleValue value,ax::Quat& out)
{
    ax::Quat temporary;
    if(!axjs::read(cx,value,"x",temporary.x) || !axjs::read(cx,value,"y",temporary.y) ||
        !axjs::read(cx,value,"z",temporary.z) || !axjs::read(cx,value,"w",temporary.w) ||
        !std::isfinite(temporary.x) || !std::isfinite(temporary.y) ||
        !std::isfinite(temporary.z) || !std::isfinite(temporary.w)) return false;
    out=temporary; return true;
}
bool Converter<ax::Quat>::write(JSContext* cx,const ax::Quat& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"x",value.x) || !axjs::write(cx,object,"y",value.y) ||
        !axjs::write(cx,object,"z",value.z) || !axjs::write(cx,object,"w",value.w)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::Viewport>::read(JSContext* cx,JS::HandleValue value,ax::Viewport& out)
{
    ax::Viewport temporary;
    if(!axjs::read(cx,value,"x",temporary.x) || !axjs::read(cx,value,"y",temporary.y) ||
        !axjs::read(cx,value,"width",temporary.width) || !axjs::read(cx,value,"height",temporary.height) ||
        temporary.width<0 || temporary.height<0) return false;
    out=temporary; return true;
}
bool Converter<ax::Viewport>::write(JSContext* cx,const ax::Viewport& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"x",value.x) || !axjs::write(cx,object,"y",value.y) ||
        !axjs::write(cx,object,"width",value.width) || !axjs::write(cx,object,"height",value.height)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::AABB>::read(JSContext* cx,JS::HandleValue value,ax::AABB& out)
{
    if(readRegisteredCopy(cx,value,out)) return true;
    ax::Vec3 min,max;
    if(!axjs::read(cx,value,"min",min) || !axjs::read(cx,value,"max",max)) return false;
    out.set(min,max); return true;
}
bool Converter<ax::AABB>::write(JSContext* cx,const ax::AABB& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"min",value._min) || !axjs::write(cx,object,"max",value._max)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::OBB>::read(JSContext* cx,JS::HandleValue value,ax::OBB& out)
{
    if(readRegisteredCopy(cx,value,out)) return true;
    ax::Vec3 center,xAxis,yAxis,zAxis,extents;
    if(!axjs::read(cx,value,"center",center) || !axjs::read(cx,value,"xAxis",xAxis) ||
        !axjs::read(cx,value,"yAxis",yAxis) || !axjs::read(cx,value,"zAxis",zAxis) ||
        !axjs::read(cx,value,"extents",extents)) return false;
    ax::OBB temporary;
    temporary.set(center,xAxis,yAxis,zAxis,extents);
    if(!readOptional(cx,value,"extentX",temporary._extentX) || !readOptional(cx,value,"extentY",temporary._extentY) ||
        !readOptional(cx,value,"extentZ",temporary._extentZ)) return false;
    out=temporary; return true;
}
bool Converter<ax::OBB>::write(JSContext* cx,const ax::OBB& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"center",value._center) || !axjs::write(cx,object,"xAxis",value._xAxis) ||
        !axjs::write(cx,object,"yAxis",value._yAxis) || !axjs::write(cx,object,"zAxis",value._zAxis) ||
        !axjs::write(cx,object,"extents",value._extents) || !axjs::write(cx,object,"extentX",value._extentX) ||
        !axjs::write(cx,object,"extentY",value._extentY) || !axjs::write(cx,object,"extentZ",value._extentZ)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::Ray>::read(JSContext* cx,JS::HandleValue value,ax::Ray& out)
{
    if(readRegisteredCopy(cx,value,out)) return true;
    ax::Vec3 origin,direction;
    if(!axjs::read(cx,value,"origin",origin) || !axjs::read(cx,value,"direction",direction)) return false;
    out.set(origin,direction); return true;
}
bool Converter<ax::Ray>::write(JSContext* cx,const ax::Ray& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"origin",value.origin) || !axjs::write(cx,object,"direction",value.direction)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::PhysicsMaterial2D>::read(JSContext* cx,JS::HandleValue value,ax::PhysicsMaterial2D& out)
{
    ax::PhysicsMaterial2D temporary;
    if(!axjs::read(cx,value,"density",temporary.density) || !axjs::read(cx,value,"restitution",temporary.restitution) ||
        !axjs::read(cx,value,"friction",temporary.friction)) return false;
    out=temporary; return true;
}
bool Converter<ax::PhysicsMaterial2D>::write(JSContext* cx,const ax::PhysicsMaterial2D& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"density",value.density) ||
        !axjs::write(cx,object,"restitution",value.restitution) || !axjs::write(cx,object,"friction",value.friction)) return false;
    out.setObject(*object); return true;
}

#if defined(AX_ENABLE_3D)
bool Converter<ax::Terrain::DetailMap>::read(JSContext* cx,JS::HandleValue value,ax::Terrain::DetailMap& out)
{
    if(readRegisteredCopy(cx,value,out)) return true;
    ax::Terrain::DetailMap temporary;
    if(!axjs::read(cx,value,"file",temporary._detailMapSrc) || !axjs::read(cx,value,"size",temporary._detailMapSize)) return false;
    out=std::move(temporary); return true;
}
bool Converter<ax::Terrain::DetailMap>::write(JSContext* cx,const ax::Terrain::DetailMap& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"file",value._detailMapSrc) || !axjs::write(cx,object,"size",value._detailMapSize)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::Terrain::TerrainData>::read(JSContext* cx,JS::HandleValue value,ax::Terrain::TerrainData& out)
{
    if(readRegisteredCopy(cx,value,out)) return true;
    ax::Terrain::TerrainData temporary;
    // Fixed v3 JSB gives omitted/undefined legacy skirt ratio a default of 1.
    temporary._skirtHeightRatio=1.0f;
    std::vector<ax::Terrain::DetailMap> maps;
    if(!axjs::read(cx,value,"heightMap",temporary._heightMapSrc) || !axjs::read(cx,value,"alphaMap",temporary._alphaMapSrc) ||
        !axjs::read(cx,value,"chunkSize",temporary._chunkSize) || !axjs::read(cx,value,"mapHeight",temporary._mapHeight) ||
        !axjs::read(cx,value,"mapScale",temporary._mapScale) || !axjs::read(cx,value,"detailMap",maps) ||
        maps.empty() || maps.size()>4 || !readOptional(cx,value,"skirtHeightRatio",temporary._skirtHeightRatio)) return false;
    temporary._detailMapAmount=static_cast<int>(maps.size());
    std::move(maps.begin(),maps.end(),std::begin(temporary._detailMaps));
    out=std::move(temporary); return true;
}
bool Converter<ax::Terrain::TerrainData>::write(JSContext* cx,const ax::Terrain::TerrainData& value,JS::MutableHandleValue out)
{
    if(value._detailMapAmount<0 || value._detailMapAmount>4) return false;
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"heightMap",value._heightMapSrc) || !axjs::write(cx,object,"alphaMap",value._alphaMapSrc) ||
        !axjs::write(cx,object,"chunkSize",value._chunkSize) || !axjs::write(cx,object,"mapHeight",value._mapHeight) ||
        !axjs::write(cx,object,"mapScale",value._mapScale) || !axjs::write(cx,object,"skirtHeightRatio",value._skirtHeightRatio) ||
        !axjs::write(cx,object,"detailMap",std::span(value._detailMaps,static_cast<size_t>(value._detailMapAmount)))) return false;
    out.setObject(*object); return true;
}
#endif

#if defined(AX_ENABLE_NAVMESH)
bool Converter<ax::NavMeshAgentParam>::read(JSContext* cx,JS::HandleValue value,ax::NavMeshAgentParam& out)
{
    ax::NavMeshAgentParam temporary;
    // The old jsb.navMeshAgentParam() returns every default, while callers may
    // pass only the fields they change. Preserve native defaults for omissions.
    if(!readOptional(cx,value,"radius",temporary.radius) || !readOptional(cx,value,"height",temporary.height) ||
        !readOptional(cx,value,"maxAcceleration",temporary.maxAcceleration) || !readOptional(cx,value,"maxSpeed",temporary.maxSpeed) ||
        !readOptional(cx,value,"collisionQueryRange",temporary.collisionQueryRange) ||
        !readOptional(cx,value,"pathOptimizationRange",temporary.pathOptimizationRange) ||
        !readOptional(cx,value,"separationWeight",temporary.separationWeight) || !readOptional(cx,value,"updateFlags",temporary.updateFlags) ||
        !readOptional(cx,value,"obstacleAvoidanceType",temporary.obstacleAvoidanceType) ||
        !readOptional(cx,value,"queryFilterType",temporary.queryFilterType)) return false;
    out=temporary; return true;
}
bool Converter<ax::NavMeshAgentParam>::write(JSContext* cx,const ax::NavMeshAgentParam& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"radius",value.radius) || !axjs::write(cx,object,"height",value.height) ||
        !axjs::write(cx,object,"maxAcceleration",value.maxAcceleration) || !axjs::write(cx,object,"maxSpeed",value.maxSpeed) ||
        !axjs::write(cx,object,"collisionQueryRange",value.collisionQueryRange) ||
        !axjs::write(cx,object,"pathOptimizationRange",value.pathOptimizationRange) ||
        !axjs::write(cx,object,"separationWeight",value.separationWeight) || !axjs::write(cx,object,"updateFlags",value.updateFlags) ||
        !axjs::write(cx,object,"obstacleAvoidanceType",value.obstacleAvoidanceType) ||
        !axjs::write(cx,object,"queryFilterType",value.queryFilterType)) return false;
    out.setObject(*object); return true;
}
bool Converter<ax::OffMeshLinkData>::read(JSContext* cx,JS::HandleValue value,ax::OffMeshLinkData& out)
{
    ax::OffMeshLinkData temporary;
    if(!axjs::read(cx,value,"startPosition",temporary.startPosition) || !axjs::read(cx,value,"endPosition",temporary.endPosition)) return false;
    out=temporary; return true;
}
bool Converter<ax::OffMeshLinkData>::write(JSContext* cx,const ax::OffMeshLinkData& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"startPosition",value.startPosition) || !axjs::write(cx,object,"endPosition",value.endPosition)) return false;
    out.setObject(*object); return true;
}
#endif

#if defined(AX_ENABLE_PHYSICS_3D)
bool Converter<ax::PhysicsMaterial>::read(JSContext* cx,JS::HandleValue value,ax::PhysicsMaterial& out)
{
    ax::PhysicsMaterial temporary;
    if(!readOptional(cx,value,"friction",temporary.friction) || !readOptional(cx,value,"restitution",temporary.restitution)) return false;
    out=temporary; return true;
}
bool Converter<ax::PhysicsMaterial>::write(JSContext* cx,const ax::PhysicsMaterial& value,JS::MutableHandleValue out)
{
    JS::RootedObject object(cx,newObject(cx));
    if(!object || !axjs::write(cx,object,"friction",value.friction) || !axjs::write(cx,object,"restitution",value.restitution)) return false;
    out.setObject(*object); return true;
}
#endif
} // namespace axjs
