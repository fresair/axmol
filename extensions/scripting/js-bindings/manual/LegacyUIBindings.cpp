/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyUIBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/ui/Widget.h"
#include "axmol/ui/InputField.h"
#include "axmol/ui/LayoutParameter.h"
#include "axmol/ui/RichText.h"
#include "axmol/base/InputSystem.h"
#if defined(AX_ENABLE_VIDEO)
#include "axmol/ui/VideoPlayer.h"
#endif

namespace axjs
{
namespace
{
using namespace manual;
#if defined(AX_ENABLE_VIDEO)
bool videoLegacyFullscreen(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* video = self<ax::ui::VideoPlayer>(cx, args);
    if (!video)
        return false;
    if (argc != 1)
        return error(cx, "VideoPlayer.setFullScreenEnabled expects one argument");
    video->setLegacyFullscreen(JS::ToBoolean(args[0]));
    args.rval().setUndefined();
    return true;
}
bool videoLegacyFullscreenGet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* video = self<ax::ui::VideoPlayer>(cx, args);
    if (!video)
        return false;
    args.rval().setBoolean(video->isFullscreen());
    return true;
}
#endif
struct OwnedFocusCallback
{
    std::function<void(ax::ui::Widget*, ax::ui::Widget*)> callback;
    std::shared_ptr<int> owner;
    void operator()(ax::ui::Widget* previous, ax::ui::Widget* current) const { callback(previous, current); }
};
struct OwnedNextCallback
{
    std::function<ax::ui::Widget*(ax::ui::Widget::FocusDirection)> callback;
    std::shared_ptr<int> owner;
    ax::ui::Widget* operator()(ax::ui::Widget::FocusDirection direction) const { return callback(direction); }
};
struct UIState
{
    struct WidgetState
    {
        ax::WeakPtr<ax::ui::Widget> native;
        std::function<void(ax::ui::Widget*, ax::ui::Widget*)> originalFocus;
        std::function<ax::ui::Widget*(ax::ui::Widget::FocusDirection)> originalNext;
    };
    std::shared_ptr<int> callbackOwner = std::make_shared<int>(0);
    std::shared_ptr<int> imeOwner = std::make_shared<int>(0);
    std::unordered_map<ax::ui::InputField*, ax::WeakPtr<ax::ui::InputField>> imeFields;
    void trackIME(ax::ui::InputField* field)
    {
        field->setLegacyIMEEditingLifetime(imeOwner);
        // Factories/clones/Studio fields may already have pending native text.
        // Reapply the same real placeholder through the existing setter after
        // opt-in so measurement observes presentation, without an IME edit.
        if (field->getRenderLabel())
            field->setPlaceholderText(field->getPlaceholderText());
        imeFields[field] = ax::WeakPtr<ax::ui::InputField>(field);
    }
    std::unordered_map<ax::ui::Widget*, WidgetState> widgets;
    WidgetState& track(ax::ui::Widget* widget)
    {
        auto& entry = widgets[widget];
        if (entry.native.get() != widget)
        {
            entry.native = ax::WeakPtr<ax::ui::Widget>(widget);
            entry.originalFocus = widget->onFocusChanged;
            entry.originalNext = widget->onNextFocusedWidget;
            if (!entry.originalFocus)
                entry.originalFocus = [](ax::ui::Widget* previous, ax::ui::Widget* current) {
                    if (previous) previous->setFocused(false);
                    if (current) current->setFocused(true);
                };
        }
        return entry;
    }
    std::unordered_map<ax::ui::RichText*, ax::WeakPtr<ax::ui::RichText>> richTexts;
    void clear()
    {
        // No VM lookup in the Android input path. The delegate's weak token
        // expires before any queued OS operation can enter a stopped VM.
        std::weak_ptr<void> expiredOwner = imeOwner;
        imeOwner.reset();
        for (auto& [_, weak] : imeFields)
            if (auto* field = weak.get(); field && field->ownsLegacyIMEEditingLifetime(expiredOwner) &&
                ax::InputSystem::getInstance()->isAttachedDelegate(field))
                static_cast<ax::InputDelegate*>(field)->detachWithIME();
        imeFields.clear();
        for (auto& [_, weak] : richTexts)
            if (auto* rich = weak.get())
                rich->setOpenUrlHandler({});
        richTexts.clear();
        for (auto& [_, entry] : widgets)
            if (auto* widget = entry.native.get())
            {
                if (auto* callback = widget->onFocusChanged.target<OwnedFocusCallback>();
                    callback && callback->owner == callbackOwner)
                    widget->onFocusChanged = std::move(entry.originalFocus);
                if (auto* callback = widget->onNextFocusedWidget.target<OwnedNextCallback>();
                    callback && callback->owner == callbackOwner)
                    widget->onNextFocusedWidget = std::move(entry.originalNext);
            }
        widgets.clear();
    }
};
std::unordered_map<JSContext*, std::shared_ptr<UIState>> states;

bool widgetInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* widget = self<ax::ui::Widget>(cx, args);
    if (!widget || argc)
        return error(cx, "Widget.init expects no arguments");
    // Virtual dispatch initializes the actual native RichText/Slider/Layout/
    // ListView/PageView subclass, including each subclass's render resources.
    args.rval().setBoolean(widget->init());
    return true;
}
bool inputInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* field = self<ax::ui::InputField>(cx, args);
    std::string placeholder, font = "Arial";
    float size = 24;
    if (!field || argc > 3 || (argc && !argument(cx, args, 0, placeholder)) ||
        (argc > 1 && !argument(cx, args, 1, font)) || (argc > 2 && !argument(cx, args, 2, size)))
        return error(cx, "TextField.init expects optional placeholder, font and size");
    args.rval().setBoolean(field->initWithPlaceholder(placeholder, font, size, 2.f, ax::Color32::black));
    if (args.rval().toBoolean())
        states.at(cx)->trackIME(field);
    return true;
}
// The legacy public TextField API returns undefined. InputField protects its
// override, so dispatch through the real public InputDelegate virtual API.
// didNotSelectSelf was the old public detach entry point (not detachWithIME).
template<bool Attach>
bool inputIME(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* field = self<ax::ui::InputField>(cx, args);
    if (!field || argc)
        return error(cx, Attach ? "TextField.attachWithIME expects no arguments" :
                                 "TextField.didNotSelectSelf expects no arguments");
    auto* delegate = static_cast<ax::InputDelegate*>(field);
    if constexpr (Attach)
    {
        states.at(cx)->trackIME(field);
        delegate->attachWithIME();
    }
    else
        delegate->detachWithIME();
    args.rval().setUndefined();
    return true;
}
bool inputLegacyEditing(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* field = self<ax::ui::InputField>(cx, args);
    if (!field || argc)
        return error(cx, "TextField._useLegacyIME expects no arguments");
    states.at(cx)->trackIME(field);
    args.rval().setUndefined();
    return true;
}
bool richTextInitXML(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* rich = self<ax::ui::RichText>(cx, args);
    std::string xml;
    ax::ValueMap defaults;
    ax::ui::RichText::OpenUrlHandler handler;
    if (!rich || argc < 1 || argc > 3 || !argument(cx, args, 0, xml) ||
        (argc > 1 && !args[1].isNullOrUndefined() && !argument(cx, args, 1, defaults)))
        return error(cx, "RichText.initWithXML expects XML, optional defaults and URL handler");
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    if (argc > 2 && !Converter<decltype(handler)>::read(cx, args[2], handler, receiver))
        return error(cx, "RichText URL handler must be callable or null");
    args.rval().setBoolean(rich->initWithXML(xml, defaults, handler));
    states.at(cx)->richTexts[rich] = ax::WeakPtr<ax::ui::RichText>(rich);
    return true;
}
bool swallowSet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* widget = self<ax::ui::Widget>(cx, args);
    bool swallow{};
    if (!widget || argc != 1 || !argument(cx, args, 0, swallow))
        return error(cx, "Widget.setSwallowPointerEvents expects a boolean");
    widget->setSwallowPointerEvents(swallow);
    args.rval().setUndefined();
    return true;
}
bool swallowGet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* widget = self<ax::ui::Widget>(cx, args);
    if (!widget || argc)
        return error(cx, "Widget.isSwallowPointerEvents expects no arguments");
    args.rval().setBoolean(widget->isSwallowPointerEvents());
    return true;
}
bool marginSet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layout = self<ax::ui::LayoutParameter>(cx, args);
    ax::ui::Margin margin;
    if (!layout)
        return false;
    if (argc == 4)
    {
        if (!argument(cx, args, 0, margin.left) || !argument(cx, args, 1, margin.top) ||
            !argument(cx, args, 2, margin.right) || !argument(cx, args, 3, margin.bottom))
            return false;
    }
    else if (argc == 1 && args[0].isObject())
    {
        JS::RootedObject object(cx, &args[0].toObject());
        JS::RootedValue value(cx);
        auto read = [&](const char* name, float& target) {
            return JS_GetProperty(cx, object, name, &value) && fromJsValue(cx, value, target);
        };
        if (!read("left", margin.left) || !read("top", margin.top) ||
            !read("right", margin.right) || !read("bottom", margin.bottom))
            return error(cx, "Margin requires finite left, top, right and bottom values");
    }
    else
        return error(cx, "LayoutParameter.setMargin expects a margin object or four numbers");
    layout->setMargin(margin);
    args.rval().setUndefined();
    return true;
}
bool marginGet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layout = self<ax::ui::LayoutParameter>(cx, args);
    if (!layout)
        return false;
    const auto& margin = layout->getMargin();
    JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!object || !property(cx, object, "left", margin.left) || !property(cx, object, "top", margin.top) ||
        !property(cx, object, "right", margin.right) || !property(cx, object, "bottom", margin.bottom))
        return false;
    args.rval().setObject(*object);
    return true;
}
template <bool Next>
bool focusSet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* widget = self<ax::ui::Widget>(cx, args);
    if (!widget || argc != 1)
        return error(cx, "Widget focus callback setter expects a callable or null");
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    auto& state = *states.at(cx);
    if constexpr (Next)
    {
        std::function<ax::ui::Widget*(ax::ui::Widget::FocusDirection)> callback;
        if (!Converter<decltype(callback)>::read(cx, args[0], callback, receiver))
            return error(cx, "onNextFocusedWidget must be callable or null");
        if (!JS_SetProperty(cx, receiver, "__legacyNextFocusedWidget", args[0]))
            return false;
        auto& entry = state.track(widget);
        widget->onNextFocusedWidget = args[0].isNullOrUndefined() ? entry.originalNext :
            std::function<ax::ui::Widget*(ax::ui::Widget::FocusDirection)>(OwnedNextCallback{std::move(callback), state.callbackOwner});
    }
    else
    {
        std::function<void(ax::ui::Widget*, ax::ui::Widget*)> callback;
        if (!Converter<decltype(callback)>::read(cx, args[0], callback, receiver))
            return error(cx, "onFocusChanged must be callable or null");
        if (!JS_SetProperty(cx, receiver, "__legacyFocusChanged", args[0]))
            return false;
        auto& entry = state.track(widget);
        widget->onFocusChanged = args[0].isNullOrUndefined() ? entry.originalFocus :
            std::function<void(ax::ui::Widget*, ax::ui::Widget*)>(OwnedFocusCallback{std::move(callback), state.callbackOwner});
    }
    args.rval().setUndefined();
    return true;
}
template <bool Next>
bool focusGet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (!self<ax::ui::Widget>(cx, args))
        return false;
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    return JS_GetProperty(cx, receiver, Next ? "__legacyNextFocusedWidget" : "__legacyFocusChanged", args.rval());
}
}

bool registerLegacyUI(JSContext* cx, JS::HandleObject global)
{
#if defined(AX_ENABLE_VIDEO)
    if (!method<ax::ui::VideoPlayer>(cx, "_setLegacyFullscreen", videoLegacyFullscreen, 1) ||
        !method<ax::ui::VideoPlayer>(cx, "_isLegacyFullscreen", videoLegacyFullscreenGet))
        return false;
#endif
    auto state = std::make_shared<UIState>();
    states[cx] = state;
    engine(cx)->addUpdateCallback([state](float) {
        std::erase_if(state->widgets, [](const auto& item) { return !item.second.native.get(); });
        std::erase_if(state->richTexts, [](const auto& item) { return !item.second.get(); });
        std::erase_if(state->imeFields, [](const auto& item) { return !item.second.get(); });
    });
    engine(cx)->addCleanupCallback([state, cx] { state->clear(); states.erase(cx); });
    return method<ax::ui::Widget>(cx, "init", widgetInit) &&
        method<ax::ui::Widget>(cx, "_init", widgetInit) &&
        method<ax::ui::InputField>(cx, "init", inputInit) &&
        method<ax::ui::InputField>(cx, "attachWithIME", inputIME<true>) &&
        method<ax::ui::InputField>(cx, "didNotSelectSelf", inputIME<false>) &&
        method<ax::ui::InputField>(cx, "_useLegacyIME", inputLegacyEditing) &&
        method<ax::ui::RichText>(cx, "initWithXML", richTextInitXML, 1) &&
        method<ax::ui::Widget>(cx, "setSwallowPointerEvents", swallowSet, 1) &&
        method<ax::ui::Widget>(cx, "isSwallowPointerEvents", swallowGet) &&
        method<ax::ui::LayoutParameter>(cx, "setMargin", marginSet, 1) &&
        method<ax::ui::LayoutParameter>(cx, "getMargin", marginGet) &&
        method<ax::ui::Widget>(cx, "_setFocusChangedCallback", focusSet<false>, 1) &&
        method<ax::ui::Widget>(cx, "_getFocusChangedCallback", focusGet<false>) &&
        method<ax::ui::Widget>(cx, "_setNextFocusedWidgetCallback", focusSet<true>, 1) &&
        method<ax::ui::Widget>(cx, "_getNextFocusedWidgetCallback", focusGet<true>);
}
}
