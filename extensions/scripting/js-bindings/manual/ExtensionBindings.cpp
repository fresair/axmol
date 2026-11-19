/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/ManualUtils.h"
#include "axmol/ui/Widget.h"
#if AX_ENABLE_EXT_SCENEIO
#    include "sceneio/ActionTimeline/CSLoader.h"
#    include "sceneext/ActionTimeline/ActionTimeline.h"
#    include "sceneext/ActionTimeline/Frame.h"
#    include "sceneext/ActionManagerEx.h"
#    include "sceneext/SSceneReader.h"
#    include "rapidjson/document.h"
#endif
#if AX_ENABLE_EXT_ASSETMANAGER
#    include "assets-manager/AssetsManagerEx.h"
#    include "assets-manager/EventListenerAssetsManagerEx.h"
#endif

namespace axjs
{
bool registerSpine(JSContext*, JS::HandleObject);
namespace
{
using namespace manual;
#if AX_ENABLE_EXT_SCENEIO
bool addAnimationInfo(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args      = JS::CallArgsFromVp(argc, vp);
    auto* timeline = self<ax::ext::timeline::ActionTimeline>(cx, args);
    if (!timeline || !argc || !args[0].isObject())
        return error(cx, "addAnimationInfo expects an object");
    JS::RootedObject object(cx, &args[0].toObject());
    JS::RootedValue name(cx), start(cx), end(cx), fn(cx);
    ax::ext::timeline::AnimationInfo info;
    if (!JS_GetProperty(cx, object, "name", &name) || !fromJsValue(cx, name, info.name) ||
        !JS_GetProperty(cx, object, "startIndex", &start) || !fromJsValue(cx, start, info.startIndex) ||
        !JS_GetProperty(cx, object, "endIndex", &end) || !fromJsValue(cx, end, info.endIndex))
        return error(cx, "Invalid animation info");
    if (!JS_GetProperty(cx, object, "clipEndCallBack", &fn))
        return false;
    if (!fn.isNullOrUndefined())
    {
        auto cb = Callback::create(cx, fn, object);
        if (!cb)
            return false;
        info.clipEndCallBack = [cb] { call(cb); };
    }
    timeline->addAnimationInfo(info);
    args.rval().setUndefined();
    return true;
}
bool getAnimationInfo(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args      = JS::CallArgsFromVp(argc, vp);
    auto* timeline = self<ax::ext::timeline::ActionTimeline>(cx, args);
    std::string name;
    if (!timeline || !argument(cx, args, 0, name))
        return false;
    if (!timeline->IsAnimationInfoExists(name))
        return error(cx, "Animation name does not exist");
    const auto& info = timeline->getAnimationInfo(name);
    JS::RootedObject out(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!out || !property(cx, out, "name", info.name) || !property(cx, out, "startIndex", info.startIndex) ||
        !property(cx, out, "endIndex", info.endIndex))
        return false;
    args.rval().setObject(*out);
    return true;
}
bool frameCallback(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args      = JS::CallArgsFromVp(argc, vp);
    auto* timeline = self<ax::ext::timeline::ActionTimeline>(cx, args);
    if (!timeline || !argc)
        return false;
    if (args[0].isNullOrUndefined())
    {
        timeline->setFrameEventCallFunc({});
        args.rval().setUndefined();
        return true;
    }
    JS::RootedObject receiver(cx, argc > 1 && args[1].isObject() ? &args[1].toObject() : &args.thisv().toObject());
    auto cb = Callback::create(cx, args[0], receiver);
    if (!cb)
        return false;
    timeline->setFrameEventCallFunc([cb](ax::ext::timeline::Frame* frame) { call(cb, frame); });
    args.rval().setUndefined();
    return true;
}
bool actionDictionary(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args     = JS::CallArgsFromVp(argc, vp);
    auto* manager = self<ax::ext::ActionManagerEx>(cx, args);
    std::string file, json;
    ax::Object* root{};
    int version = 1600;
    if (!manager || argc < 3 || !argument(cx, args, 0, file) || !argument(cx, args, 2, root) ||
        (argc > 3 && !argument(cx, args, 3, version)))
        return false;
    JS::RootedObject global(cx, engine(cx)->getGlobalObject());
    JS::RootedValue jsonValue(cx), text(cx), input(cx, args[1]);
    if (!JS_GetProperty(cx, global, "JSON", &jsonValue) || !jsonValue.isObject())
        return false;
    JS::RootedObject jsonObject(cx, &jsonValue.toObject());
    if (!JS_CallFunctionName(cx, jsonObject, "stringify", JS::HandleValueArray(input), &text) ||
        !fromJsValue(cx, text, json))
        return false;
    rapidjson::Document document;
    document.Parse(json.c_str(), json.size());
    if (document.HasParseError() || !document.IsObject())
        return error(cx, "Invalid CocoStudio animation dictionary");
    manager->initWithDictionary(file, document, root, version);
    args.rval().setUndefined();
    return true;
}
bool widgetEvent(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args    = JS::CallArgsFromVp(argc, vp);
    auto* widget = self<ax::ui::Widget>(cx, args);
    if (!widget || !argc)
        return false;
    if (args[0].isNullOrUndefined())
    {
        widget->addCCSEventListener({});
        args.rval().setUndefined();
        return true;
    }
    JS::RootedObject receiver(cx, argc > 1 && args[1].isObject() ? &args[1].toObject() : &args.thisv().toObject());
    auto cb = Callback::create(cx, args[0], receiver);
    if (!cb)
        return false;
    widget->addCCSEventListener([cb](ax::Object* sender, int event) { call(cb, sender, event); });
    args.rval().setUndefined();
    return true;
}
void bindWidgetCallbacks(JSContext* cx, ax::Node* node, JS::HandleObject controller)
{
    if (auto* widget = dynamic_cast<ax::ui::Widget*>(node); widget && !widget->getCallbackName().empty())
    {
        JS::RootedValue fn(cx);
        std::string name(widget->getCallbackName());
        if (JS_GetProperty(cx, controller, name.c_str(), &fn) && fn.isObject())
        {
            auto cb = Callback::create(cx, fn, controller);
            if (cb)
            {
                auto type = widget->getCallbackType();
                if (type == "Click")
                    widget->addClickEventListener([cb](ax::Object* sender) { call(cb, sender); });
                else if (type == "Touch")
                    widget->addTouchEventListener([cb](ax::Object* sender, ax::ui::Widget::TouchEventType t) {
                        call(cb, sender, static_cast<int>(t));
                    });
                else if (type == "Event")
                    widget->addCCSEventListener([cb](ax::Object* sender, int t) { call(cb, sender, t); });
            }
        }
    }
    for (auto* child : node->getChildren())
        bindWidgetCallbacks(cx, child, controller);
}
bool loadCsb(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string file;
    if (!argument(cx, args, 0, file))
        return false;
    if (!ax::FileUtils::getInstance()->isFileExist(file))
        return error(cx, "CocoStudio resource does not exist");
    auto* node = ax::CSLoader::createNode(file);
    if (!node)
        return error(cx, "CocoStudio resource could not be loaded");
    auto* action = ax::CSLoader::createTimeline(file);
    if (argc > 1 && args[1].isObject())
    {
        JS::RootedObject controller(cx, &args[1].toObject());
        bindWidgetCallbacks(cx, node, controller);
        if (JS_IsExceptionPending(cx))
            return false;
    }
    JS::RootedObject out(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    JS::RootedValue value(cx);
    if (!out || !wrapNative(cx, node, &value, NativeOwnership::Factory) || !property(cx, out, "node", value))
        return false;
    if (!wrapNative(cx, action, &value, NativeOwnership::Factory) || !property(cx, out, "action", value))
        return false;
    args.rval().setObject(*out);
    return true;
}
bool bindController(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Node* node{};
    if (argc < 2 || !argument(cx, args, 0, node) || !node || !args[1].isObject())
        return error(cx, "bindController expects a node and controller");
    JS::RootedObject controller(cx, &args[1].toObject());
    bindWidgetCallbacks(cx, node, controller);
    args.rval().setUndefined();
    return !JS_IsExceptionPending(cx);
}
#endif
#if AX_ENABLE_EXT_ASSETMANAGER
bool assetsListener(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::ext::AssetsManagerEx* manager{};
    if (argc < 2 || !argument(cx, args, 0, manager) || !manager)
        return error(cx, "Assets event listener requires an AssetsManager");
    auto cb = Callback::create(cx, args[1]);
    if (!cb)
        return false;
    auto* listener = ax::ext::EventListenerAssetsManagerEx::create(manager, [cb](ax::ext::EventAssetsManagerEx* event) {
        if (!cb->isValid())
            return;
        auto* context = cb->state()->context;
        ScopedNative scope;
        JS::RootedValue value(context);
        if (wrapBorrowed(context, event, &value, scope.token()))
            cb->call(JS::HandleValueArray(value));
    });
    return wrapNative(cx, listener, args.rval(), NativeOwnership::Factory);
}
bool verifyAsset(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args     = JS::CallArgsFromVp(argc, vp);
    auto* manager = self<ax::ext::AssetsManagerEx>(cx, args);
    if (!manager || !argc)
        return false;
    if (args[0].isNullOrUndefined())
    {
        manager->setVerifyCallback({});
        args.rval().setUndefined();
        return true;
    }
    auto cb = Callback::create(cx, args[0]);
    if (!cb)
        return false;
    manager->setVerifyCallback([cb](std::string_view path, ax::ext::Manifest::Asset asset) {
        // Axmol's verification callback is delivered from its main-thread update pump.
        if (!cb->isValid() || !cb->state()->onOwnerThread())
            return false;
        auto* context = cb->state()->context;
        JS::RootedObject object(context, JS_NewObject(context, nullptr, JS::NullPtr(), JS::NullPtr()));
        if (!object || !property(context, object, "path", asset.path) || !property(context, object, "md5", asset.md5) ||
            !property(context, object, "compressed", asset.compressed) ||
            !property(context, object, "size", asset.size))
            return false;
        JS::RootedValue p(context), result(context);
        if (!toJsValue(context, path, &p))
            return false;
        JS::AutoValueVector values(context);
        if (!values.append(p) || !values.append(JS::ObjectValue(*object)))
            return false;
        return cb->call(JS::HandleValueArray(values), &result) && JS::ToBoolean(result);
    });
    args.rval().setUndefined();
    return true;
}
#endif
}  // namespace
bool registerExtensions(JSContext* cx, JS::HandleObject global)
{
#if AX_ENABLE_EXT_SCENEIO
    setStaticLifetime<ax::CSLoader>(cx);
    setStaticLifetime<ax::ext::SceneReader>(cx);
    JS::RootedObject ccs(cx, manual::namespaceObject(cx, global, "ccs"));
    if (!manual::method<ax::ext::timeline::ActionTimeline>(cx, "addAnimationInfo", addAnimationInfo, 1) ||
        !manual::method<ax::ext::timeline::ActionTimeline>(cx, "getAnimationInfo", getAnimationInfo, 1) ||
        !manual::method<ax::ext::timeline::ActionTimeline>(cx, "setFrameEventCallFunc", frameCallback, 1) ||
        !manual::method<ax::ext::ActionManagerEx>(cx, "initWithDictionary", actionDictionary, 3) ||
        !manual::method<ax::ui::Widget>(cx, "addCCSEventListener", widgetEvent, 1))
        return false;
    if (!ccs || !manual::function(cx, ccs, "_loadCsb", loadCsb, 1) ||
        !manual::function(cx, ccs, "bindController", bindController, 2))
        return false;
#endif
#if AX_ENABLE_EXT_ASSETMANAGER
    JS::RootedObject klass(cx, getClassObject<ax::ext::EventListenerAssetsManagerEx>(cx));
    if (!klass || !manual::function(cx, klass, "create", assetsListener, 2) ||
        !manual::method<ax::ext::AssetsManagerEx>(cx, "setVerifyCallback", verifyAsset, 1))
        return false;
#endif
#if AX_ENABLE_EXT_SPINE
    if (!registerSpine(cx, global))
        return false;
#endif
    return true;
}
}  // namespace axjs
