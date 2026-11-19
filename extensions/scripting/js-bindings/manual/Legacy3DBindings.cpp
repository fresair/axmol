/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Legacy3DBindings.h"
#include "runtime/JsRuntime.h"
#if defined(AX_ENABLE_3D)
#include "axmol/3d/Terrain.h"
#include "axmol/scene/Camera.h"
#include "axmol/math/AABB.h"
#endif
namespace axjs
{
namespace
{
#if defined(AX_ENABLE_3D)
bool cameraVisible(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Camera* camera{};ax::AABB bounds;
    if(argc!=1) { JS_ReportError(cx,"Camera.isVisibleInFrustum requires one AABB");return false; }
    if(!toNative(cx,args.thisv(),&camera)||!camera)return false;
    if(!fromJsValue(cx,args[0],bounds)) {
        if(!JS_IsExceptionPending(cx))JS_ReportError(cx,"Camera.isVisibleInFrustum requires AABB {min,max}");
        return false;
    }
    // The value copy is owned by this call, matching the v3 manual JSB bridge.
    args.rval().setBoolean(camera->isVisibleInFrustum(&bounds));return true;
}
bool terrainHeight(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Terrain* terrain{};ax::Vec2 position;
    if(!toNative(cx,args.thisv(),&terrain)||!terrain)return false;
    if(argc==1) { if(!fromJsValue(cx,args[0],position))return false; }
    else if(argc==2) { if(!fromJsValue(cx,args[0],position.x)||!fromJsValue(cx,args[1],position.y))return false; }
    else { JS_ReportError(cx,"Terrain.getHeight requires x/z coordinates or a position");return false; }
    args.rval().set(nativeNumberValue(terrain->getHeight(position)));return true;
}
bool terrainIntersection(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Terrain* terrain{};ax::Ray ray;ax::Vec3 intersection;
    if((argc!=1&&argc!=2)||!toNative(cx,args.thisv(),&terrain)||!terrain||!fromJsValue(cx,args[0],ray))return false;
    if(argc==1)return toJsValue(cx,terrain->getIntersectionPoint(ray),args.rval());
    if(!fromJsValue(cx,args[1],intersection))return false;
    args.rval().setBoolean(terrain->getIntersectionPoint(ray,intersection));return true;
}
#endif
}
bool registerLegacy3D(JSContext* cx,JS::HandleObject)
{
#if defined(AX_ENABLE_3D)
    JS::RootedObject terrain(cx,getPrototype<ax::Terrain>(cx));
    JS::RootedObject camera(cx,getPrototype<ax::Camera>(cx));
    return camera && JS_DefineFunction(cx,camera,"isVisibleInFrustum",cameraVisible,1,0) &&
        terrain && JS_DefineFunction(cx,terrain,"getHeight",terrainHeight,2,0) &&
        JS_DefineFunction(cx,terrain,"getIntersectionPoint",terrainIntersection,1,0);
#else
    return true;
#endif
}
}
