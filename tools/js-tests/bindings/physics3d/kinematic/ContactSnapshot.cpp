// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "ContactSnapshot.h"
#include "runtime/JsRuntime.h"
#include "axmol/physics/3d/ContactEvent3D.h"
#include "axmol/physics/3d/Rigidbody3D.h"

namespace physics3dtest
{
namespace
{
template<class T>
bool property(JSContext* cx, JS::HandleObject object, const char* name, const T& input)
{
    JS::RootedValue value(cx);
    return axjs::toJsValue(cx, input, &value) && JS_SetProperty(cx, object, name, value);
}

bool snapshot(JSContext* cx, unsigned argc, JS::Value* vp)
{
    const auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 1)
    {
        JS_ReportError(cx, "Contact snapshot requires one live ContactEvent3D");
        return false;
    }
    ax::ContactEvent3D* event{};
    if (!axjs::toNative(cx, args[0], &event) || !event)
    {
        if (!JS_IsExceptionPending(cx))
            JS_ReportError(cx, "Contact snapshot requires one live ContactEvent3D");
        return false;
    }

    // Copy while the actual native event is live. No point/velocity, event,
    // reference to its inlined_vector, or temporary-event owner lease escapes.
    const ax::ContactInfo3D info = event->getContactInfo();
    const auto code = event->getEventCode();
    JS::RootedObject result(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    JS::RootedObject points(cx, JS_NewArrayObject(cx, 0));
    if (!result || !points || !property(cx, result, "eventCode", code) ||
        !property(cx, result, "actorA", info.actorA) || !property(cx, result, "actorB", info.actorB) ||
        !property(cx, result, "normal", info.normal))
        return false;

    unsigned index{};
    for (const auto& contact : info.points)
    {
        JS::RootedObject point(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
        if (!point || !property(cx, point, "worldPositionOnA", contact.sideA.point) ||
            !property(cx, point, "worldPositionOnB", contact.sideB.point))
            return false;
        if (contact.sideA.velocity && !property(cx, point, "velocityOnA", *contact.sideA.velocity))
            return false;
        if (contact.sideB.velocity && !property(cx, point, "velocityOnB", *contact.sideB.velocity))
            return false;
        JS::RootedValue value(cx, JS::ObjectValue(*point));
        if (!JS_SetElement(cx, points, index++, value))
            return false;
    }
    JS::RootedValue value(cx, JS::ObjectValue(*points));
    if (!JS_SetProperty(cx, result, "points", value))
        return false;
    args.rval().setObject(*result);
    return true;
}
}

bool registerContactSnapshot(JSContext* cx, JS::HandleObject global)
{
    return JS_DefineFunction(cx, global, "__physics3DNativeContactSnapshot", snapshot, 1, 0);
}
}
