/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/scene/Node.h"
#include "axmol/scene/Scene.h"
#include "axmol/platform/FileUtils.h"
#include <jsfriendapi.h>

namespace axjs::manual
{
inline bool error(JSContext* cx, const char* message)
{
    JS_ReportError(cx, "%s", message);
    return false;
}
template <class T>
T* self(JSContext* cx, const JS::CallArgs& args)
{
    T* result{};
    if (!toNative(cx, args.thisv(), &result) || !result)
    {
        JS_ReportError(cx, "Invalid native receiver");
        return nullptr;
    }
    return result;
}
template <class T>
bool argument(JSContext* cx, const JS::CallArgs& args, unsigned index, T& out)
{
    if (index >= args.length() || !fromJsValue(cx, args[index], out))
        return error(cx, "Invalid JavaScript argument");
    return true;
}
inline JSObject* namespaceObject(JSContext* cx, JS::HandleObject global, const char* name)
{
    JS::RootedObject object(cx);
    if (!getOrCreateNamespace(cx, global, name, &object))
        return nullptr;
    return object;
}
inline bool function(JSContext* cx, JS::HandleObject object, const char* name, JSNative fn, unsigned arity = 0)
{
    return JS_DefineFunction(cx, object, name, fn, arity, JSPROP_ENUMERATE) != nullptr;
}
inline bool property(JSContext* cx, JS::HandleObject object, const char* name, JS::HandleValue value)
{
    return JS_DefineProperty(cx, object, name, value, JSPROP_ENUMERATE);
}
inline bool property(JSContext* cx, JS::HandleObject object, const char* name, const JS::RootedValue& value)
{
    return JS_DefineProperty(cx, object, name, value, JSPROP_ENUMERATE);
}
template <class T>
bool property(JSContext* cx, JS::HandleObject object, const char* name, const T& v)
{
    JS::RootedValue value(cx);
    return toJsValue(cx, v, &value) && property(cx, object, name, value);
}
inline bool dimensions(JSContext* cx, const ax::Vec2& size, JS::MutableHandleValue value)
{
    JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!object || !property(cx, object, "width", size.x) || !property(cx, object, "height", size.y))
        return false;
    value.setObject(*object);
    return true;
}
inline std::shared_ptr<Callback> callback(JSContext* cx,
                                          const JS::CallArgs& args,
                                          unsigned index,
                                          JS::HandleObject receiver)
{
    if (index >= args.length())
    {
        error(cx, "Missing callback");
        return {};
    }
    return Callback::create(cx, args[index], receiver);
}
template <class... T>
bool call(const std::shared_ptr<Callback>& callback, const T&... values)
{
    if (!callback || !callback->isValid())
        return false;
    auto state = callback->state();
    auto* cx   = state->context;
    JS::AutoValueVector args(cx);
    bool ok     = true;
    auto append = [&](const auto& v) {
        JS::RootedValue value(cx);
        if (!toJsValue(cx, v, &value) || !args.append(value))
            ok = false;
    };
    (append(values), ...);
    return ok && callback->call(JS::HandleValueArray(args));
}
template <class T>
bool method(JSContext* cx, const char* name, JSNative fn, unsigned arity = 0)
{
    JS::RootedObject prototype(cx, getPrototype<T>(cx));
    return prototype ? function(cx, prototype, name, fn, arity)
                     : error(cx, "Required native class has not been registered");
}
}  // namespace axjs::manual
