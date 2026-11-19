/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyExtensionBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/ui/EditBox/EditBox.h"
#include "axmol/base/RefPtr.h"
#if AX_ENABLE_EXT_GUI
#    include "GUI/ControlExtension/Control.h"
#    include "GUI/ControlExtension/ControlColourPicker.h"
#    include "GUI/ControlExtension/ControlSlider.h"
#    include "GUI/ScrollView/TableView.h"
#endif
#if AX_ENABLE_EXT_COCOSBUILDER
#    include "cocosbuilder/CCBReader.h"
#endif

namespace axjs
{
namespace
{
using namespace manual;

// These roots belong to one VM. Native delegates are borrowed by their widgets,
// so the VM owns adapters until widget destruction or its cleanup callback.
struct Receiver
{
    std::shared_ptr<RuntimeState> state;
    JS::PersistentRootedObject object;

    Receiver(JSContext* cx, JS::HandleObject receiver) : state(runtimeState(cx)), object(cx, receiver) {}

    bool active() const { return state && state->alive && state->onOwnerThread(); }

    template <class... A>
    bool invoke(const char* method, JS::MutableHandleValue result, const A&... args)
    {
        if (!active())
            return false;
        auto* cx = state->context;
        JSAutoCompartment compartment(cx, state->engine->getGlobalObject());
        JS::RootedObject receiver(cx, object);
        JS::RootedValue function(cx);
        if (!JS_GetProperty(cx, receiver, method, &function))
        {
            state->engine->reportException();
            return false;
        }
        if (function.isNullOrUndefined())
            return false;
        if (!function.isObject() || !JS_ObjectIsCallable(cx, &function.toObject()))
        {
            JS_ReportError(cx, "Legacy delegate method %s is not callable", method);
            state->engine->reportException();
            return false;
        }
        JS::AutoValueVector values(cx);
        bool ok = true;
        auto append = [&](const auto& arg) {
            JS::RootedValue value(cx);
            ok = ok && toJsValue(cx, arg, &value) && values.append(value);
        };
        (append(args), ...);
        ok = ok && JS_CallFunctionValue(cx, receiver, function, JS::HandleValueArray(values), result);
        if (!ok)
            state->engine->reportException();
        return ok;
    }

    template <class... A>
    void notify(const char* method, const A&... args)
    {
        if (!active())
            return;
        JS::RootedValue result(state->context);
        invoke(method, &result, args...);
    }
};

std::shared_ptr<Receiver> makeReceiver(JSContext* cx, JS::HandleValue value)
{
    if (value.isNullOrUndefined())
        return {};
    if (!value.isObject())
    {
        error(cx, "Delegate/data source must be an object or null");
        return {};
    }
    JS::RootedObject object(cx, &value.toObject());
    return std::make_shared<Receiver>(cx, object);
}

struct EditBridge final : ax::ui::EditBoxDelegate, std::enable_shared_from_this<EditBridge>
{
    ax::WeakPtr<ax::ui::EditBox> owner;
    std::shared_ptr<Receiver> receiver;

    explicit EditBridge(ax::ui::EditBox* box) : owner(box) {}

    void editBoxEditingDidBegin(ax::ui::EditBox* box) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
            target->notify("editBoxEditingDidBegin", box);
    }
    void editBoxTextChanged(ax::ui::EditBox* box, std::string_view text) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
            target->notify("editBoxTextChanged", box, text);
    }
    void editBoxReturn(ax::ui::EditBox* box) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
            target->notify("editBoxReturn", box);
    }
    void editBoxEditingDidEndWithAction(ax::ui::EditBox* box, EditBoxEndAction action) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
        {
            target->notify("editBoxEditingDidEnd", box);
            target->notify("editBoxEditingDidEndWithAction", box, static_cast<int>(action));
        }
    }
    void detach()
    {
        if (auto* box = owner.get(); box && box->getDelegate() == this)
            box->setDelegate(nullptr);
        receiver.reset();
    }
};

#if AX_ENABLE_EXT_GUI
struct ScrollBridge final : ax::ext::ScrollViewDelegate, std::enable_shared_from_this<ScrollBridge>
{
    ax::WeakPtr<ax::ext::ScrollView> owner;
    std::shared_ptr<Receiver> receiver;
    explicit ScrollBridge(ax::ext::ScrollView* view) : owner(view) {}
    void scrollViewDidScroll(ax::ext::ScrollView* view) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
            target->notify("scrollViewDidScroll", view);
    }
    void scrollViewDidZoom(ax::ext::ScrollView* view) override
    {
        auto keep = shared_from_this();
        if (auto target = receiver)
            target->notify("scrollViewDidZoom", view);
    }
    void detach()
    {
        if (auto* view = owner.get(); view && view->getDelegate() == this)
        {
            view->unscheduleAllCallbacks();
            view->setTouchEnabled(false);
            view->setDelegate(nullptr);
        }
        receiver.reset();
    }
};

struct TableBridge final : ax::ext::TableViewDelegate,
                           ax::ext::TableViewDataSource,
                           std::enable_shared_from_this<TableBridge>
{
    ax::WeakPtr<ax::ext::TableView> owner;
    std::shared_ptr<Receiver> delegate;
    std::shared_ptr<Receiver> source;
    explicit TableBridge(ax::ext::TableView* table) : owner(table) {}

    void scrollViewDidScroll(ax::ext::ScrollView* view) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("scrollViewDidScroll", view);
    }
    void scrollViewDidZoom(ax::ext::ScrollView* view) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("scrollViewDidZoom", view);
    }
    void tableCellTouched(ax::ext::TableView* table, ax::ext::TableViewCell* cell) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("tableCellTouched", table, cell);
    }
    void tableCellHighlight(ax::ext::TableView* table, ax::ext::TableViewCell* cell) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("tableCellHighlight", table, cell);
    }
    void tableCellUnhighlight(ax::ext::TableView* table, ax::ext::TableViewCell* cell) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("tableCellUnhighlight", table, cell);
    }
    void tableCellWillRecycle(ax::ext::TableView* table, ax::ext::TableViewCell* cell) override
    {
        auto keep = shared_from_this();
        if (auto target = delegate)
            target->notify("tableCellWillRecycle", table, cell);
    }
    ax::Size tableCellSizeForIndex(ax::ext::TableView* table, ssize_t index) override
    {
        auto keep = shared_from_this();
        auto target = source;
        if (!target || !target->active())
            return {};
        auto* cx = target->state->context;
        JS::RootedValue result(cx);
        ax::Size size;
        if (target->invoke("tableCellSizeForIndex", &result, table, index))
        {
            if (fromJsValue(cx, result, size))
                return size;
            target->state->engine->reportException();
            return {};
        }
        return cellSizeForTable(table);
    }
    ax::Size cellSizeForTable(ax::ext::TableView* table) override
    {
        auto keep = shared_from_this();
        auto target = source;
        if (!target || !target->active())
            return {};
        auto* cx = target->state->context;
        JS::RootedValue result(cx);
        ax::Size size;
        if (target->invoke("cellSizeForTable", &result, table) && fromJsValue(cx, result, size))
            return size;
        if (JS_IsExceptionPending(cx))
            target->state->engine->reportException();
        return {};
    }
    ax::ext::TableViewCell* tableCellAtIndex(ax::ext::TableView* table, ssize_t index) override
    {
        auto keep = shared_from_this();
        auto target = source;
        if (!target || !target->active())
            return nullptr;
        auto* cx = target->state->context;
        JS::RootedValue result(cx);
        ax::ext::TableViewCell* cell{};
        if (target->invoke("tableCellAtIndex", &result, table, index) && toNative(cx, result, &cell) && cell)
            return cell;
        if (!JS_IsExceptionPending(cx))
            JS_ReportError(cx, "tableCellAtIndex must return a live TableViewCell");
        target->state->engine->reportException();
        return nullptr;
    }
    ssize_t numberOfCellsInTableView(ax::ext::TableView* table) override
    {
        auto keep = shared_from_this();
        auto target = source;
        if (!target || !target->active())
            return 0;
        auto* cx = target->state->context;
        JS::RootedValue result(cx);
        ssize_t count{};
        if (target->invoke("numberOfCellsInTableView", &result, table) && fromJsValue(cx, result, count) && count >= 0)
            return count;
        if (!JS_IsExceptionPending(cx))
            JS_ReportError(cx, "numberOfCellsInTableView must return a nonnegative integer");
        target->state->engine->reportException();
        return 0;
    }
    void detach()
    {
        if (auto* table = owner.get())
        {
            // Scroll deceleration and pointer dispatch can call a TableView
            // data source after this VM stops while native scene owners remain.
            table->unscheduleAllCallbacks();
            table->setTouchEnabled(false);
            if (table->getDelegate() == this)
                table->setDelegate(nullptr);
            if (table->getDataSource() == this)
                table->setDataSource(nullptr);
        }
        delegate.reset();
        source.reset();
    }
};

// Every public legacy constructor/extend allocation uses the real class's
// __allocate. Factories and explicit initialization also opt in at the native
// boundary. No JS business state or event/listener replacement is involved.
bool sliderOptInResult(JSContext* cx, const JS::CallArgs& args)
{
    if (args.rval().isNull()) // Preserve the existing nullable factory contract.
        return true;
    ax::ext::ControlSlider* slider{};
    if (!toNative(cx, args.rval(), &slider) || !slider)
        return error(cx, "ControlSlider allocation did not return a native Slider");
    slider->setLegacyTouchHitTestEnabled(true);
    return true;
}

bool sliderAllocate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    return constructUninitialized<ax::ext::ControlSlider>(cx, args) && sliderOptInResult(cx, args);
}

bool sliderCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    using Slider = ax::ext::ControlSlider;
    if (invoke<static_cast<Slider* (*)(const char*, const char*, const char*)>(&Slider::create)>(cx, args))
        return sliderOptInResult(cx, args);
    if (JS_IsExceptionPending(cx)) return false;
    if (invoke<static_cast<Slider* (*)(ax::Sprite*, ax::Sprite*, ax::Sprite*)>(&Slider::create)>(cx, args))
        return sliderOptInResult(cx, args);
    if (JS_IsExceptionPending(cx)) return false;
    if (invoke<static_cast<Slider* (*)(const char*, const char*, const char*, const char*)>(&Slider::create)>(cx, args))
        return sliderOptInResult(cx, args);
    if (JS_IsExceptionPending(cx)) return false;
    if (invoke<static_cast<Slider* (*)(ax::Sprite*, ax::Sprite*, ax::Sprite*, ax::Sprite*)>(&Slider::create)>(cx, args))
        return sliderOptInResult(cx, args);
    if (JS_IsExceptionPending(cx)) return false;
    return error(cx, "cc.ControlSlider.create: no overload matches the supplied arguments");
}

bool sliderInitWithSprites(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    using Slider = ax::ext::ControlSlider;
    bool matched = invoke<static_cast<bool (Slider::*)(ax::Sprite*, ax::Sprite*, ax::Sprite*)>(&Slider::initWithSprites)>(cx, args);
    if (!matched && JS_IsExceptionPending(cx)) return false;
    if (!matched)
        matched = invoke<static_cast<bool (Slider::*)(ax::Sprite*, ax::Sprite*, ax::Sprite*, ax::Sprite*)>(&Slider::initWithSprites)>(cx, args);
    if (!matched)
    {
        if (JS_IsExceptionPending(cx)) return false;
        return error(cx, "cc.ControlSlider.initWithSprites: no overload matches the supplied arguments");
    }
    if (args.rval().isBoolean() && args.rval().toBoolean())
    {
        auto* slider = self<Slider>(cx, args);
        if (!slider) return false;
        slider->setLegacyTouchHitTestEnabled(true);
    }
    return true;
}

template <class Picker>
bool pickerOptInResult(JSContext* cx, const JS::CallArgs& args)
{
    if (args.rval().isNull())
        return true;
    Picker* picker{};
    if (!toNative(cx, args.rval(), &picker) || !picker)
        return error(cx, "Control picker allocation did not return its native type");
    picker->setLegacyCompatibilityEnabled(true);
    return true;
}

template <class Picker>
bool pickerAllocate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    return constructUninitialized<Picker>(cx, args) && pickerOptInResult<Picker>(cx, args);
}

template <class Picker>
bool pickerCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (invoke<&Picker::create>(cx, args))
        return pickerOptInResult<Picker>(cx, args);
    if (JS_IsExceptionPending(cx)) return false;
    return error(cx, "Control picker create: no overload matches the supplied arguments");
}

template <class Picker, auto Initialize>
bool pickerInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (!invoke<Initialize>(cx, args))
    {
        if (JS_IsExceptionPending(cx)) return false;
        return error(cx, "Control picker init: no overload matches the supplied arguments");
    }
    if (args.rval().isBoolean() && args.rval().toBoolean())
    {
        auto* picker = self<Picker>(cx, args);
        if (!picker) return false;
        picker->setLegacyCompatibilityEnabled(true);
    }
    return true;
}

template <class Picker, auto Initialize>
bool installLegacyPicker(JSContext* cx, const char* initName, unsigned initArity, unsigned createArity)
{
    JS::RootedObject picker(cx, getClassObject<Picker>(cx));
    return picker && JS_DefineFunction(cx, picker, "__allocate", pickerAllocate<Picker>, 0, 0) &&
           function(cx, picker, "create", pickerCreate<Picker>, createArity) &&
           method<Picker>(cx, initName, pickerInit<Picker, Initialize>, initArity);
}

struct ControlAction
{
    JS::PersistentRootedObject target;
    JS::PersistentRootedValue function;
    std::shared_ptr<Callback> callback;
    unsigned events;
    ControlAction(JSContext* cx, JS::HandleObject object, JS::HandleValue fn,
                  std::shared_ptr<Callback> cb, unsigned mask)
        : target(cx, object), function(cx, fn), callback(std::move(cb)), events(mask) {}
};

struct ControlBridge final : ax::Object
{
    ax::WeakPtr<ax::ext::Control> owner;
    std::vector<std::shared_ptr<ControlAction>> actions;
    explicit ControlBridge(ax::ext::Control* control) : owner(control) {}

    static ax::ext::Control::Handler handler()
    {
        return static_cast<ax::ext::Control::Handler>(&ControlBridge::dispatch);
    }
    void dispatch(ax::Object* sender, ax::ext::Control::EventType event)
    {
        ax::RefPtr<ControlBridge> keep(this);
        // One native invocation per event keeps the native dispatch vector stable
        // while JS callbacks add/remove targets or cancel themselves.
        auto snapshot = actions;
        for (auto& action : snapshot)
            if ((action->events & static_cast<unsigned>(event)) && action->callback->isValid())
                call(action->callback, sender, static_cast<unsigned>(event));
    }
    void detach()
    {
        if (auto* control = owner.get())
            control->removeTargetWithActionForControlEvents(this, handler(),
                static_cast<ax::ext::Control::EventType>((1u << kControlEventTotalNumber) - 1));
        for (auto& action : actions)
        {
            action->events = 0;
            action->callback->cancel();
        }
        actions.clear();
    }
};
#endif

struct LegacyState
{
    std::unordered_map<ax::ui::EditBox*, std::shared_ptr<EditBridge>> edits;
#if AX_ENABLE_EXT_GUI
    std::unordered_map<ax::ext::ScrollView*, std::shared_ptr<ScrollBridge>> scrolls;
    std::unordered_map<ax::ext::TableView*, std::shared_ptr<TableBridge>> tables;
    std::unordered_map<ax::ext::Control*, ax::RefPtr<ControlBridge>> controls;
#endif
    void prune()
    {
        std::erase_if(edits, [](auto& item) { return !item.second->owner.get(); });
#if AX_ENABLE_EXT_GUI
        std::erase_if(scrolls, [](auto& item) { return !item.second->owner.get(); });
        std::erase_if(tables, [](auto& item) { return !item.second->owner.get(); });
        std::erase_if(controls, [](auto& item) { return !item.second->owner.get(); });
#endif
    }
    void clear()
    {
        for (auto& [_, bridge] : edits)
            bridge->detach();
        edits.clear();
#if AX_ENABLE_EXT_GUI
        for (auto& [_, bridge] : scrolls)
            bridge->detach();
        for (auto& [_, bridge] : tables)
            bridge->detach();
        for (auto& [_, bridge] : controls)
            bridge->detach();
        scrolls.clear();
        tables.clear();
        controls.clear();
#endif
    }
};
std::unordered_map<JSContext*, std::shared_ptr<LegacyState>> states;
LegacyState& state(JSContext* cx) { return *states.at(cx); }

bool editSetDelegate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* box = self<ax::ui::EditBox>(cx, args);
    if (!box || argc != 1)
        return error(cx, "EditBox.setDelegate expects one object or null");
    auto target = makeReceiver(cx, args[0]);
    if (JS_IsExceptionPending(cx))
        return false;
    auto& bridge = state(cx).edits[box];
    if (!bridge || bridge->owner.get() != box)
        bridge = std::make_shared<EditBridge>(box);
    bridge->receiver = std::move(target);
    box->setDelegate(bridge->receiver ? bridge.get() : nullptr);
    args.rval().setUndefined();
    return true;
}
bool editGetDelegate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* box = self<ax::ui::EditBox>(cx, args);
    if (!box)
        return false;
    auto found = state(cx).edits.find(box);
    args.rval().setNull();
    if (found != state(cx).edits.end() && found->second->owner.get() == box && found->second->receiver)
        args.rval().setObject(*found->second->receiver->object);
    return true;
}

#if AX_ENABLE_EXT_GUI
std::shared_ptr<TableBridge> tableBridge(JSContext* cx, ax::ext::TableView* table)
{
    auto& bridge = state(cx).tables[table];
    if (!bridge || bridge->owner.get() != table)
        bridge = std::make_shared<TableBridge>(table);
    return bridge;
}
bool tableSetSource(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* table = self<ax::ext::TableView>(cx, args);
    if (!table || argc != 1)
        return error(cx, "TableView.setDataSource expects one object or null");
    auto source = makeReceiver(cx, args[0]);
    if (JS_IsExceptionPending(cx))
        return false;
    auto bridge = tableBridge(cx, table);
    bridge->source = std::move(source);
    // An empty JS data source still uses the real adapter (zero cells); Axmol's
    // reloadData native implementation dereferences its data source.
    table->setDataSource(bridge.get());
    JS::RootedObject wrapper(cx, &args.thisv().toObject());
    if (!JS_SetProperty(cx, wrapper, "_dataSource", args[0]))
        return false;
    args.rval().setUndefined();
    return true;
}
bool tableGetSource(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* table = self<ax::ext::TableView>(cx, args);
    if (!table)
        return false;
    auto bridge = tableBridge(cx, table);
    args.rval().setNull();
    if (bridge->source)
        args.rval().setObject(*bridge->source->object);
    return true;
}
bool scrollSetDelegate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* view = self<ax::ext::ScrollView>(cx, args);
    if (!view || argc != 1)
        return error(cx, "ScrollView.setDelegate expects one object or null");
    auto target = makeReceiver(cx, args[0]);
    if (JS_IsExceptionPending(cx))
        return false;
    if (auto* table = dynamic_cast<ax::ext::TableView*>(view))
    {
        auto bridge = tableBridge(cx, table);
        bridge->delegate = std::move(target);
        table->setDelegate(bridge->delegate ? bridge.get() : nullptr);
    }
    else
    {
        auto& bridge = state(cx).scrolls[view];
        if (!bridge || bridge->owner.get() != view)
            bridge = std::make_shared<ScrollBridge>(view);
        bridge->receiver = std::move(target);
        view->setDelegate(bridge->receiver ? bridge.get() : nullptr);
    }
    JS::RootedObject wrapper(cx, &args.thisv().toObject());
    if (!JS_SetProperty(cx, wrapper, "_delegate", args[0]))
        return false;
    args.rval().setUndefined();
    return true;
}
bool scrollGetDelegate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* view = self<ax::ext::ScrollView>(cx, args);
    if (!view)
        return false;
    std::shared_ptr<Receiver> target;
    if (auto* table = dynamic_cast<ax::ext::TableView*>(view))
        target = tableBridge(cx, table)->delegate;
    else if (auto found = state(cx).scrolls.find(view); found != state(cx).scrolls.end() && found->second->owner.get() == view)
        target = found->second->receiver;
    args.rval().setNull();
    if (target)
        args.rval().setObject(*target->object);
    return true;
}
bool initializeTable(JSContext* cx, const JS::CallArgs& args, ax::ext::TableView* table)
{
    if (args.length() != 0 && (args.length() < 2 || args.length() > 3))
        return error(cx, "TableView requires a data source, size and optional container");
    ax::Size size;
    ax::Node* container{};
    std::shared_ptr<Receiver> source;
    if (args.length())
    {
        source = makeReceiver(cx, args[0]);
        if (JS_IsExceptionPending(cx) || !argument(cx, args, 1, size) ||
            (args.length() > 2 && !argument(cx, args, 2, container)))
            return false;
    }
    auto bridge = tableBridge(cx, table);
    bridge->source = std::move(source);
    table->setDataSource(bridge.get());
    if (!table->initWithViewSize(size, container))
        return error(cx, "TableView native initialization failed");
    table->reloadData();
    return true;
}
bool tableInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* table = self<ax::ext::TableView>(cx, args);
    if (!table || !initializeTable(cx, args, table))
        return false;
    JS::RootedObject wrapper(cx, &args.thisv().toObject());
    JS::RootedValue source(cx, argc ? args[0] : JS::NullValue());
    if (!JS_SetProperty(cx, wrapper, "_dataSource", source))
        return false;
    args.rval().setBoolean(true);
    return true;
}
bool tableCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* table = new ax::ext::TableView;
    // Register identity before init invokes the JS data source.
    if (!wrapNative(cx, table, args.rval(), NativeOwnership::Factory))
    {
        table->release();
        return false;
    }
    table->autorelease();
    if (!initializeTable(cx, args, table))
        return false;
    JS::RootedObject wrapper(cx, &args.rval().toObject());
    JS::RootedValue source(cx, argc ? args[0] : JS::NullValue());
    return JS_SetProperty(cx, wrapper, "_dataSource", source);
}

bool controlAddTarget(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* control = self<ax::ext::Control>(cx, args);
    unsigned events{};
    if (!control || argc != 3 || !args[0].isObject() || !argument(cx, args, 2, events) ||
        !events || (events & ~((1u << kControlEventTotalNumber) - 1)))
        return error(cx, "Control target requires receiver, callable and valid event mask");
    JS::RootedObject target(cx, &args[0].toObject());
    auto cb = Callback::create(cx, args[1], target);
    if (!cb)
        return error(cx, "Control action must be callable");
    auto& bridge = state(cx).controls[control];
    if (!bridge || bridge->owner.get() != control)
    {
        bridge = ax::RefPtr<ControlBridge>(new ControlBridge(control), tlx::adopt_object);
        control->addTargetWithActionForControlEvents(bridge.get(), ControlBridge::handler(),
            static_cast<ax::ext::Control::EventType>((1u << kControlEventTotalNumber) - 1));
    }
    bridge->actions.push_back(std::make_shared<ControlAction>(cx, target, args[1], std::move(cb), events));
    args.rval().setUndefined();
    return true;
}
bool controlRemoveTarget(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* control = self<ax::ext::Control>(cx, args);
    unsigned events{};
    if (!control || argc != 3 || !argument(cx, args, 2, events) ||
        (!args[0].isNullOrUndefined() && !args[0].isObject()) ||
        (!args[1].isNullOrUndefined() && (!args[1].isObject() || !JS_ObjectIsCallable(cx, &args[1].toObject()))))
        return error(cx, "Control removal requires receiver/action or null and an event mask");
    if (auto found = state(cx).controls.find(control); found != state(cx).controls.end() && found->second->owner.get() == control)
    {
        auto bridge = found->second;
        for (auto& action : bridge->actions)
            if ((args[0].isNullOrUndefined() || action->target.get() == &args[0].toObject()) &&
                (args[1].isNullOrUndefined() || action->function.get() == args[1]))
            {
                action->events &= ~events;
                if (!action->events)
                    action->callback->cancel();
            }
        std::erase_if(bridge->actions, [](const auto& action) { return !action->events; });
    }
    args.rval().setUndefined();
    return true;
}
#endif

#if AX_ENABLE_EXT_COCOSBUILDER
bool readerCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc)
        return error(cx, "BuilderReader.create does not accept arguments");
    auto* reader = new ax::ext::CCBReader;
    reader->autorelease();
    return wrapNative(cx, reader, args.rval(), NativeOwnership::Factory);
}
bool readerLoad(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* reader = self<ax::ext::CCBReader>(cx, args);
    std::string filename;
    ax::Object* owner{};
    ax::Size parentSize;
    if (!reader || argc < 1 || argc > 3 || !argument(cx, args, 0, filename) ||
        (argc > 1 && !argument(cx, args, 1, owner)) || (argc > 2 && !argument(cx, args, 2, parentSize)))
        return error(cx, "BuilderReader.load expects a filename, optional native owner and parent size");
    auto failed = std::make_shared<bool>(false);
    auto weak = std::weak_ptr<RuntimeState>(runtimeState(cx));
    reader->setNodePropertyCallback([weak, failed](ax::Node* node, std::string_view name, const ax::Value& value) {
        auto state = weak.lock();
        if (!state || !state->alive || !state->onOwnerThread())
            return;
        auto* context = state->context;
        JSAutoCompartment compartment(context, state->engine->getGlobalObject());
        JS::RootedValue wrapper(context), argument(context), fn(context), result(context);
        bool ok = wrapNative(context, node, &wrapper) && wrapper.isObject() && toJsValue(context, value, &argument);
        JS::RootedObject receiver(context, ok ? &wrapper.toObject() : nullptr);
        std::string method(name);
        ok = ok && JS_GetProperty(context, receiver, method.c_str(), &fn);
        if (ok && (!fn.isObject() || !JS_ObjectIsCallable(context, &fn.toObject())))
        {
            JS_ReportError(context, "CCB Layer property requires native JS method %s", method.c_str());
            ok = false;
        }
        ok = ok && JS_CallFunctionValue(context, receiver, fn, JS::HandleValueArray(argument), &result);
        if (!ok)
        {
            *failed = true;
            state->engine->reportException();
        }
    });
    ax::Node* node = argc > 2 ? reader->readNodeGraphFromFile(filename.c_str(), owner, parentSize)
                            : argc > 1 ? reader->readNodeGraphFromFile(filename.c_str(), owner)
                                       : reader->readNodeGraphFromFile(filename.c_str());
    if (*failed)
        return error(cx, "A CocosBuilder Layer input property could not be applied");
    if (!node)
        return error(cx, "CocosBuilder node graph could not be loaded");
    return wrapNative(cx, node, args.rval(), NativeOwnership::Factory);
}
#endif
} // namespace

bool registerLegacyExtensions(JSContext* cx, JS::HandleObject global)
{
    auto legacy = std::make_shared<LegacyState>();
    states[cx] = legacy;
    engine(cx)->addUpdateCallback([legacy](float) { legacy->prune(); });
    engine(cx)->addCleanupCallback([legacy, cx] {
        legacy->clear();
        states.erase(cx);
    });
    if (!method<ax::ui::EditBox>(cx, "setDelegate", editSetDelegate, 1) ||
        !method<ax::ui::EditBox>(cx, "getDelegate", editGetDelegate))
        return false;
#if AX_ENABLE_EXT_GUI
    JS::RootedObject slider(cx, getClassObject<ax::ext::ControlSlider>(cx));
    if (!slider || !JS_DefineFunction(cx, slider, "__allocate", sliderAllocate, 0, 0) ||
        !function(cx, slider, "create", sliderCreate, 3) ||
        !method<ax::ext::ControlSlider>(cx, "initWithSprites", sliderInitWithSprites, 3))
        return false;
    if (!installLegacyPicker<ax::ext::ControlColourPicker, &ax::ext::ControlColourPicker::init>(cx, "init", 0, 0) ||
        !installLegacyPicker<ax::ext::ControlHuePicker, &ax::ext::ControlHuePicker::initWithTargetAndPos>(
            cx, "initWithTargetAndPos", 2, 2) ||
        !installLegacyPicker<ax::ext::ControlSaturationBrightnessPicker,
                             &ax::ext::ControlSaturationBrightnessPicker::initWithTargetAndPos>(
            cx, "initWithTargetAndPos", 2, 2))
        return false;
    JS::RootedObject table(cx, getClassObject<ax::ext::TableView>(cx));
    if (!table || !function(cx, table, "create", tableCreate, 2) ||
        !method<ax::ext::TableView>(cx, "_init", tableInit, 2) ||
        !method<ax::ext::TableView>(cx, "setDataSource", tableSetSource, 1) ||
        !method<ax::ext::TableView>(cx, "getDataSource", tableGetSource) ||
        !method<ax::ext::TableView>(cx, "setDelegate", scrollSetDelegate, 1) ||
        !method<ax::ext::TableView>(cx, "getDelegate", scrollGetDelegate) ||
        !method<ax::ext::ScrollView>(cx, "setDelegate", scrollSetDelegate, 1) ||
        !method<ax::ext::ScrollView>(cx, "getDelegate", scrollGetDelegate) ||
        !method<ax::ext::Control>(cx, "addTargetWithActionForControlEvents", controlAddTarget, 3) ||
        !method<ax::ext::Control>(cx, "removeTargetWithActionForControlEvents", controlRemoveTarget, 3))
        return false;
    engine(cx)->setEventHandledByNative(ax::kControlEvent);
#endif
#if AX_ENABLE_EXT_COCOSBUILDER
    JS::RootedObject reader(cx, getClassObject<ax::ext::CCBReader>(cx));
    if (!reader || !function(cx, reader, "create", readerCreate) ||
        !method<ax::ext::CCBReader>(cx, "load", readerLoad, 1))
        return false;
#endif
    return true;
}
} // namespace axjs
