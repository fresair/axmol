/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/PropertiesBindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Properties.h"
#include "axmol/platform/FileUtils.h"

namespace axjs
{
namespace
{
bool ownedProperties(JSContext* cx, std::unique_ptr<ax::Properties> native, JS::MutableHandleValue out)
{
    if (!native) { out.setNull(); return true; }
    JS::RootedObject prototype(cx, getPrototype<ax::Properties>(cx));
    JS::RootedObject object(cx, JS_NewObject(cx, detail::getClassInfo(cx, typeid(ax::Properties)), prototype, JS::NullPtr()));
    if (!object || !bindNative(cx, object, native.get(), NativeOwnership::Constructor)) return false;
    native.release(); out.setObject(*object); return true;
}
bool createProperties(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); std::string url;
    if (argc != 1 || !fromJsValue(cx, args[0], url)) { JS_ReportError(cx, "Properties.create requires a file URL"); return false; }
    // The native parser requires nonempty input. Keep its missing-file result
    // semantics while protecting this script entry from its empty-data assert.
    auto hash = url.find('#'); auto file = url.substr(0, hash);
    auto* files = ax::FileUtils::getInstance();
    const auto path = file.empty() ? std::string{} : files->fullPathForFilename(file);
    if (path.empty() || files->getFileSize(path) <= 0)
    { args.rval().setNull(); return true; }
    return ownedProperties(cx, std::unique_ptr<ax::Properties>(ax::Properties::createNonRefCounted(url)), args.rval());
}
template<auto F, class T> bool propertyValue(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::Properties* self{}; std::string name; T value{};
    if (argc != 1 || !toNative(cx, args.thisv(), &self) || !self || !fromJsValue(cx, args[0], name))
    { if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, "Property lookup requires a live Properties object and name"); return false; }
    if (!(self->*F)(name, &value)) { args.rval().setNull(); return true; }
    return toJsValue(cx, value, args.rval());
}
}
bool registerProperties(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject constructor(cx, getClassObject<ax::Properties>(cx)), prototype(cx, getPrototype<ax::Properties>(cx));
    if (!constructor || !prototype) return false;
    const JSFunctionSpec methods[] = {
        JS_FN("getVec2", (propertyValue<&ax::Properties::getVec2, ax::Vec2>), 1, 0),
        JS_FN("getVec3", (propertyValue<&ax::Properties::getVec3, ax::Vec3>), 1, 0),
        JS_FN("getVec4", (propertyValue<&ax::Properties::getVec4, ax::Vec4>), 1, 0),
        JS_FN("getMat4", (propertyValue<&ax::Properties::getMat4, ax::Mat4>), 1, 0),
        JS_FN("getQuaternionFromAxisAngle", (propertyValue<&ax::Properties::getQuaternionFromAxisAngle, ax::Quat>), 1, 0),
        JS_FN("getColor", (propertyValue<&ax::Properties::getColor, ax::Color>), 1, 0), JS_FS_END
    };
    return JS_DefineFunction(cx, constructor, "createNonRefCounted", createProperties, 1, 0) &&
           JS_DefineFunction(cx, constructor, "create", createProperties, 1, 0) && JS_DefineFunctions(cx, prototype, methods);
}
} // namespace axjs
