/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/ManualUtils.h"
#include "axmol/base/EventDispatcher.h"
#include <unordered_map>
#include <unordered_set>
#include "axmol/base/PointerEventListener.h"
#include "axmol/base/KeyboardEventListener.h"
#include "axmol/base/CustomEventListener.h"
#include "axmol/base/PointerEvent.h"
#include "axmol/base/KeyboardEvent.h"
#include "axmol/base/CustomEvent.h"
#include "axmol/base/AccelerationEventListener.h"
#include "axmol/base/FocusEventListener.h"
#include "axmol/ui/Widget.h"

namespace axjs
{
namespace
{
using namespace manual;
// Only values dispatched from this binding have a JS representation. Rooted
// stack entries make nested custom dispatch safe without exporting void*.
std::unordered_map<const ax::CustomEvent*, const JS::RootedValue*> customData;
struct PointerCallbacks
{
    int legacyType{};
    bool swallow{};
    std::shared_ptr<Callback> down, move, up, cancel, scroll;
};
struct PointerState
{
    PointerCallbacks callbacks;
    std::unordered_set<intptr_t> claimed;
};
// Native virtual clone rebuilds the event closures around fresh per-listener
// claims. Only the immutable callback/receiver handles and swallow setting copy.
class LegacyPointerListener final : public ax::PointerEventListener
{
public:
    static LegacyPointerListener* create(const PointerCallbacks& callbacks);
    LegacyPointerListener* clone() override;
    int legacyType() const { return _state->callbacks.legacyType; }
    bool swallowsTouches() const { return _state->callbacks.swallow; }
    std::function<void(intptr_t, bool)> claimCleanup() const
    {
        std::weak_ptr<PointerState> weak = _state;
        return [weak](intptr_t id, bool clearAll) {
            if (auto state = weak.lock())
            {
                if (clearAll) state->claimed.clear();
                else state->claimed.erase(id);
            }
        };
    }
    void resetClaims() { _state->claimed.clear(); }
    void resetDetachedClaims() { if (!isAttached()) resetClaims(); }
private:
    bool initialize(const PointerCallbacks& callbacks);
    std::shared_ptr<PointerState> _state;
};
struct TrackedListener
{
    ax::WeakPtr<ax::EventListener> native;
    int legacyType{};
};
using Listeners = std::vector<TrackedListener>;
std::unordered_map<JSContext*, std::shared_ptr<Listeners>> listeners;
bool listenerResult(JSContext* cx, ax::EventListener* listener, JS::MutableHandleValue result, int legacyType = 0)
{
    if (!listener)
        return error(cx, "Native listener creation failed");
    auto* pointer = dynamic_cast<LegacyPointerListener*>(listener);
    if (pointer)
    {
        legacyType = pointer->legacyType();
        ax::Director::getInstance()->getEventDispatcher()->setLegacyPointerDispatchType(
            pointer, legacyType, pointer->swallowsTouches(), pointer->claimCleanup());
    }
    auto list = listeners.at(cx);
    std::erase_if(*list, [](const auto& item) { return !item.native.get(); });
    bool tracked = false;
    for (auto& item : *list)
        if (item.native.get() == listener)
        {
            if (legacyType) item.legacyType = legacyType;
            tracked = true;
            break;
        }
    if (!tracked)
        list->push_back({ax::WeakPtr<ax::EventListener>(listener), legacyType});
    // The manual subclass retains the public PointerEventListener prototype.
    if (pointer)
        return wrapNative(cx, static_cast<ax::PointerEventListener*>(pointer), result, NativeOwnership::Factory);
    return wrapNative(cx, listener, result, NativeOwnership::Factory);
}
bool cloneListener(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* source = self<ax::EventListener>(cx, args);
    if (!source || argc)
        return error(cx, "EventListener.clone expects no arguments");
    int legacyType{};
    for (const auto& item : *listeners.at(cx))
        if (item.native.get() == source) { legacyType = item.legacyType; break; }
    return listenerResult(cx, source->clone(), args.rval(), legacyType) &&
        inheritNativeSubclass(cx, args.thisv(), args.rval());
}
bool removeLegacyListeners(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* dispatcher = self<ax::EventDispatcher>(cx, args);
    int legacyType{};
    if (!dispatcher || argc != 1 || !argument(cx, args, 0, legacyType) || legacyType < 1 || legacyType > 8)
        return error(cx, "removeListeners requires a valid legacy listener type");
    // Native POINTER combines legacy one-by-one, all-at-once and mouse. Remove
    // only explicitly classified listeners; unknown native Widget/Menu/input
    // listeners must not disappear because they share that modern native type.
    const auto snapshot = *listeners.at(cx);
    for (const auto& item : snapshot)
        if (item.legacyType == legacyType)
            if (auto* listener = item.native.get())
            {
                if (auto* pointer = dynamic_cast<LegacyPointerListener*>(listener))
                {
                    if (!dispatcher->isLegacyPointerListenerAttached(pointer)) continue;
                    pointer->resetClaims();
                }
                dispatcher->removeEventListener(listener);
            }
    args.rval().setUndefined();
    return true;
}
void customCall(const std::shared_ptr<Callback>& cb, ax::CustomEvent* event)
{
    if (!cb || !cb->isValid())
        return;
    auto* context = cb->state()->context;
    ScopedNative scope;
    JS::RootedValue value(context);
    if (wrapBorrowed(context, event, &value, scope.token()))
        cb->call(JS::HandleValueArray(value));
}
bool dispatchCustom(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string name;
    if (!argument(cx, args, 0, name))
        return false;
    JS::RootedValue data(cx, argc > 1 ? args[1] : JS::UndefinedValue());
    ax::CustomEvent event(name);
    customData.emplace(&event, &data);
    ax::Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
    customData.erase(&event);
    args.rval().setUndefined();
    return !JS_IsExceptionPending(cx);
}
bool customUserData(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args   = JS::CallArgsFromVp(argc, vp);
    auto* event = self<ax::CustomEvent>(cx, args);
    if (!event)
        return false;
    auto found = customData.find(event);
    if (found == customData.end())
        args.rval().setUndefined();
    else
        args.rval().set(*found->second);
    return true;
}
std::shared_ptr<Callback> memberCallback(JSContext* cx, JS::HandleObject description, const char* name)
{
    JS::RootedValue fn(cx);
    if (!JS_GetProperty(cx, description, name, &fn) || fn.isNullOrUndefined())
        return {};
    return Callback::create(cx, fn, description);
}
bool pointerCall(const std::shared_ptr<Callback>& cb, ax::PointerEvent* event, bool all, bool mouse)
{
    if (!cb || !cb->isValid())
        return false;
    auto state = cb->state();
    auto* cx   = state->context;
    ScopedNative scope;
    JS::RootedValue touch(cx), arg(cx), result(cx);
    if (!wrapBorrowed(cx, event, &touch, scope.token()))
        return false;
    if (all)
    {
        JS::RootedObject array(cx, JS_NewArrayObject(cx, 0));
        if (!array || !JS_SetElement(cx, array, 0, touch))
            return false;
        arg.setObject(*array);
    }
    else
        arg.set(touch);
    JS::AutoValueVector args(cx);
    if (!args.append(arg) || (!mouse && !args.append(touch)))
        return false;
    if (!cb->call(JS::HandleValueArray(args), &result))
        return false;
    return JS::ToBoolean(result);
}
bool legacyTouchPhase(ax::PointerEvent* event)
{
    if (event->getPointerType() == ax::PointerType::Touch || event->getPointerType() == ax::PointerType::Pen)
        return event->getPhase() != ax::InputPhase::PointerScroll;
    if (event->getPointerType() != ax::PointerType::Mouse)
        return false;
    // v3 desktop emits touch input only for the captured left button; hover,
    // other mouse buttons and scroll are distinct MouseEvent input.
    switch (event->getPhase())
    {
    case ax::InputPhase::PointerDown:
    case ax::InputPhase::PointerUp:
        return event->getButton() == ax::InputButton::Primary;
    case ax::InputPhase::PointerMove:
        return event->isButtonPressed(ax::InputButton::Primary);
    case ax::InputPhase::PointerCancel:
        return true;
    default:
        return false;
    }
}
bool LegacyPointerListener::initialize(const PointerCallbacks& callbacks)
{
    if (!ax::PointerEventListener::init())
        return false;
    _state = std::make_shared<PointerState>();
    _state->callbacks = callbacks;
    auto state = _state;
    const bool all = callbacks.legacyType == 2, mouse = callbacks.legacyType == 4;
    ax::WeakPtr<LegacyPointerListener> weak(this);
    onPointerHitTest = [](ax::PointerEvent* event, ax::Vec3* hit) {
        // Other captured listeners may already have written a hit result on
        // this shared event. This listener must use the newly resolved camera
        // ray, not copy another listener's cached hit into its own hit test.
        const auto& ray = event->getRay();
        ax::Vec3 point{};
        if (ray.direction.z != 0.0f)
            point = ray.origin + ray.direction * (-ray.origin.z / ray.direction.z);
        if (hit) *hit = point;
        return true;
    };
    onPointerDown = [state, all, mouse, weak](ax::PointerEvent* event) {
        if (mouse ? event->getPointerType() != ax::PointerType::Mouse : !legacyTouchPhase(event))
            return false;
        bool claimed = pointerCall(state->callbacks.down, event, all, mouse);
        if (all || mouse) return false; // Uncaptured, separate native phases.
        if (claimed && weak.get() && weak.get()->isAttached())
        {
            state->claimed.insert(event->getPointerId());
            if (state->callbacks.swallow) event->stopPropagation();
        }
        return claimed;
    };
    onPointerMove = [state, all, mouse](ax::PointerEvent* event) {
        if (!mouse && !legacyTouchPhase(event)) return;
        if (!all && !mouse && !state->claimed.contains(event->getPointerId())) return;
        if (!mouse || event->getPointerType() == ax::PointerType::Mouse)
        {
            pointerCall(state->callbacks.move, event, all, mouse);
            if (!all && !mouse && state->callbacks.swallow) event->stopPropagation();
        }
    };
    onPointerUp = [state, all, mouse](ax::PointerEvent* event) {
        if (!mouse && !legacyTouchPhase(event)) return;
        const bool claimed = state->claimed.erase(event->getPointerId()) != 0;
        if (!all && !mouse && !claimed) return;
        if (!mouse || event->getPointerType() == ax::PointerType::Mouse)
        {
            pointerCall(state->callbacks.up, event, all, mouse);
            if (!all && !mouse && state->callbacks.swallow) event->stopPropagation();
        }
    };
    onPointerCancel = [state, all, mouse](ax::PointerEvent* event) {
        if (mouse ? event->getPointerType() != ax::PointerType::Mouse : !legacyTouchPhase(event)) return;
        const bool claimed = state->claimed.erase(event->getPointerId()) != 0;
        if (!all && !mouse && !claimed) return;
        pointerCall(state->callbacks.cancel, event, all, mouse);
        if (!all && !mouse && state->callbacks.swallow) event->stopPropagation();
    };
    onPointerScroll = [state, mouse](ax::PointerEvent* event) {
        if (mouse && event->getPointerType() == ax::PointerType::Mouse)
            pointerCall(state->callbacks.scroll, event, false, true);
    };
    return true;
}
LegacyPointerListener* LegacyPointerListener::create(const PointerCallbacks& callbacks)
{
    auto* listener = new LegacyPointerListener;
    if (!listener->initialize(callbacks)) { delete listener; return nullptr; }
    listener->autorelease();
    return listener;
}
LegacyPointerListener* LegacyPointerListener::clone()
{
    return create(_state->callbacks);
}
void keyboardCall(const std::shared_ptr<Callback>& cb, ax::KeyboardEvent* event)
{
    if (!cb || !cb->isValid())
        return;
    auto* cx = cb->state()->context;
    ScopedNative scope;
    JS::RootedValue value(cx);
    if (!wrapBorrowed(cx, event, &value, scope.token()))
        return;
    JS::RootedObject global(cx, engine(cx)->getGlobalObject());
    JS::RootedValue mapping(cx), key(cx, JS::Int32Value(0));
    if (!JS_GetProperty(cx, global, "jsbKeyArr", &mapping))
        return;
    if (mapping.isObject())
    {
        JS::RootedObject array(cx, &mapping.toObject());
        if (!JS_GetElement(cx, array, static_cast<uint32_t>(event->getKeyCode()), &key))
            return;
        if (!key.isNumber())
            key.setInt32(0);
    }
    JS::AutoValueVector args(cx);
    if (!args.append(key) || !args.append(value))
        return;
    cb->call(JS::HandleValueArray(args));
}
bool createListener(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 1 || !args[0].isObject())
        return error(cx, "EventListener.create requires a descriptor");
    JS::RootedObject descriptor(cx, &args[0].toObject());
    JS::RootedValue type(cx);
    if (!JS_GetProperty(cx, descriptor, "event", &type) || !type.isNumber())
        return error(cx, "Missing listener event type");
    int eventType = static_cast<int>(type.toNumber());
    if (eventType == 1 || eventType == 2 || eventType == 4)
    {
        const bool all = eventType == 2, mouse = eventType == 4;
        auto down   = memberCallback(cx, descriptor, mouse ? "onMouseDown" : all ? "onTouchesBegan" : "onTouchBegan");
        auto move   = memberCallback(cx, descriptor, mouse ? "onMouseMove" : all ? "onTouchesMoved" : "onTouchMoved");
        auto up     = memberCallback(cx, descriptor, mouse ? "onMouseUp" : all ? "onTouchesEnded" : "onTouchEnded");
        auto cancel = memberCallback(cx, descriptor, all ? "onTouchesCancelled" : "onTouchCancelled");
        auto scroll = memberCallback(cx, descriptor, "onMouseScroll");
        if (JS_IsExceptionPending(cx))
            return false;
        if (!down && !move && !up && !cancel && !scroll)
            return error(cx, "Pointer listener has no callbacks");
        JS::RootedValue swallowValue(cx);
        if (!JS_GetProperty(cx, descriptor, "swallowTouches", &swallowValue))
            return false;
        auto* listener = LegacyPointerListener::create({eventType, JS::ToBoolean(swallowValue),
            std::move(down), std::move(move), std::move(up), std::move(cancel), std::move(scroll)});
        return listenerResult(cx, listener, args.rval(), eventType);
    }
    if (eventType == 3)
    {
        auto pressed  = memberCallback(cx, descriptor, "onKeyPressed"),
             released = memberCallback(cx, descriptor, "onKeyReleased");
        if (JS_IsExceptionPending(cx))
            return false;
        if (!pressed && !released)
            return error(cx, "Keyboard listener has no callbacks");
        auto* listener          = ax::KeyboardEventListener::create();
        listener->onKeyPressed  = [pressed](ax::KeyboardEvent* e) { keyboardCall(pressed, e); };
        listener->onKeyReleased = [released](ax::KeyboardEvent* e) { keyboardCall(released, e); };
        return listenerResult(cx, listener, args.rval(), eventType);
    }
    if (eventType == 5)
    {
        auto cb = memberCallback(cx, descriptor, "callback");
        if (!cb)
            return error(cx, "Acceleration listener requires callback");
        auto* listener = ax::AccelerationEventListener::create([cb](ax::AccelerationEvent* event) {
            if (!cb->isValid())
                return;
            auto* context            = cb->state()->context;
            const auto& acceleration = event->getAcceleration();
            JS::RootedObject data(context, JS_NewObject(context, nullptr, JS::NullPtr(), JS::NullPtr()));
            ScopedNative scope;
            JS::RootedValue borrowed(context);
            if (!data || !property(context, data, "x", acceleration.x) ||
                !property(context, data, "y", acceleration.y) || !property(context, data, "z", acceleration.z) ||
                !property(context, data, "timestamp", acceleration.timestamp) ||
                !wrapBorrowed(context, static_cast<ax::Event*>(event), &borrowed, scope.token()))
                return;
            JS::AutoValueVector args(context);
            if (args.append(JS::ObjectValue(*data)) && args.append(borrowed))
                cb->call(JS::HandleValueArray(args));
        });
        return listenerResult(cx, listener, args.rval(), eventType);
    }
    if (eventType == 6)
    {
        auto cb = memberCallback(cx, descriptor, "onFocusChanged");
        if (!cb)
            return error(cx, "Focus listener requires onFocusChanged");
        auto* listener           = ax::FocusEventListener::create();
        listener->onFocusChanged = [cb](ax::ui::Widget* previous, ax::ui::Widget* current) {
            call(cb, previous, current);
        };
        return listenerResult(cx, listener, args.rval(), eventType);
    }
    if (eventType == 8)
    {
        JS::RootedValue nameValue(cx);
        std::string name;
        if (!JS_GetProperty(cx, descriptor, "eventName", &nameValue) || !fromJsValue(cx, nameValue, name))
            return error(cx, "Custom listener requires eventName");
        auto cb = memberCallback(cx, descriptor, "callback");
        if (!cb)
            return error(cx, "Custom listener requires callback");
        return listenerResult(
            cx, ax::CustomEventListener::create(name, [cb](ax::CustomEvent* e) { customCall(cb, e); }), args.rval(), eventType);
    }
    return error(cx, "Unsupported legacy listener type");
}
bool addListener(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 2)
        return error(cx, "addListener expects listener and node/priority");
    ax::EventListener* listener =
        static_cast<ax::EventListener*>(detail::unwrapNativeInfo(cx, args[0], typeid(ax::EventListener), false));
    JS::RootedValue created(cx);
    if (!listener)
    {
        JS::RootedObject global(cx, engine(cx)->getGlobalObject());
        JS::RootedValue cc(cx), klass(cx), factory(cx);
        if (!JS_GetProperty(cx, global, "cc", &cc))
            return false;
        JS::RootedObject ccobj(cx, &cc.toObject());
        if (!JS_GetProperty(cx, ccobj, "EventListener", &klass))
            return false;
        JS::RootedObject cls(cx, &klass.toObject());
        if (!JS_GetProperty(cx, cls, "create", &factory))
            return false;
        JS::RootedValue input(cx, args[0]);
        if (!JS_CallFunctionValue(cx, cls, factory, JS::HandleValueArray(input), &created) ||
            !toNative(cx, created, &listener))
            return false;
    }
    if (!listener)
        return error(cx, "Invalid event listener");
    if (auto* pointer = dynamic_cast<LegacyPointerListener*>(listener)) pointer->resetDetachedClaims();
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    if (args[1].isNumber())
    {
        int priority;
        if (!argument(cx, args, 1, priority) || priority == 0)
            return error(cx, "Fixed listener priority cannot be zero");
        dispatcher->addEventListenerWithFixedPriority(listener, priority);
    }
    else
    {
        ax::Node* node{};
        if (!argument(cx, args, 1, node) || !node)
            return error(cx, "Scene graph listener requires a Node");
        dispatcher->addEventListenerWithSceneGraphPriority(listener, node);
    }
    return listenerResult(cx, listener, args.rval());
}
bool addCustom(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string name;
    if (argc < 2 || !argument(cx, args, 0, name))
        return false;
    auto cb = Callback::create(cx, args[1]);
    if (!cb)
        return false;
    auto* listener = ax::Director::getInstance()->getEventDispatcher()->addCustomEventListener(
        name, [cb](ax::CustomEvent* e) { customCall(cb, e); });
    return listenerResult(cx, listener, args.rval(), 8);
}
// Cocos Touch stores viewport-relative design coordinates. Its GL location
// getters convert each raw current/previous/start point independently; modern
// PointerEvent ray/hit coordinates depend on the dispatch target and camera.
// Keep getPoint/getWorldPoint and the other modern APIs unchanged.
ax::Vec2 legacyPointerViewPoint(const ax::Vec2& screenPoint)
{
    auto* director = ax::Director::getInstance();
    if (!director->getRenderView())
        return screenPoint;
    auto canvasPoint = director->screenToCanvas(screenPoint);
    canvasPoint.y = director->getCanvasSize().height - canvasPoint.y;
    return canvasPoint;
}
ax::Vec2 legacyPointerGLPoint(const ax::Vec2& screenPoint)
{
    // screenToCanvas applies the actual viewport offset, view scale and the
    // top-left to bottom-left conversion used by legacy Director::convertToGL.
    // It does not inspect or mutate the PointerEvent's contextual ray/hit state.
    return ax::Director::getInstance()->screenToCanvas(screenPoint);
}
template <int Op>
bool pointerGetter(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* e   = self<ax::PointerEvent>(cx, args);
    if (!e)
        return false;
    if constexpr (Op == 0)
        return toJsValue(cx, legacyPointerGLPoint(e->getPoint()), args.rval());
    if constexpr (Op == 1)
        return toJsValue(cx, legacyPointerGLPoint(e->getPrevPoint()), args.rval());
    if constexpr (Op == 2)
        return toJsValue(cx, legacyPointerGLPoint(e->getStartPoint()), args.rval());
    if constexpr (Op == 3)
        return toJsValue(cx, legacyPointerGLPoint(e->getPoint()) - legacyPointerGLPoint(e->getPrevPoint()), args.rval());
    if constexpr (Op == 4)
        return toJsValue(cx, e->getPointerId(), args.rval());
    if constexpr (Op >= 5)
        if (argc)
            return error(cx, "Touch view location getters expect no arguments");
    if constexpr (Op == 5)
        return toJsValue(cx, legacyPointerViewPoint(e->getPoint()), args.rval());
    if constexpr (Op == 6)
        return toJsValue(cx, legacyPointerViewPoint(e->getPrevPoint()), args.rval());
    if constexpr (Op == 7)
        return toJsValue(cx, legacyPointerViewPoint(e->getStartPoint()), args.rval());
    return false;
}
}  // namespace
bool registerInput(JSContext* cx, JS::HandleObject global)
{
    auto tracked  = std::make_shared<Listeners>();
    listeners[cx] = tracked;
    ax::WeakPtr<ax::EventDispatcher> dispatcher(ax::Director::getInstance()->getEventDispatcher());
    engine(cx)->addCleanupCallback([cx, tracked, dispatcher] {
        for (auto& item : *tracked)
            if (auto* listener = item.native.get())
            {
                if (auto* pointer = dynamic_cast<LegacyPointerListener*>(listener))
                {
                    pointer->resetClaims();
                    ax::EventDispatcher::clearLegacyPointerDispatchType(pointer);
                }
                else if (auto* native = dispatcher.get())
                    native->removeEventListener(listener);
            }
        tracked->clear();
        listeners.erase(cx);
    });
    JS::RootedObject cc(cx, manual::namespaceObject(cx, global, "cc"));
    if (!cc)
        return false;
    JS::RootedObject listener(cx, getClassObject<ax::EventListener>(cx));
    if (!listener)
        return manual::error(cx, "Missing EventListener binding");
    if (!manual::function(cx, listener, "create", createListener, 1))
        return false;
    const char* names[] = {"UNKNOWN",
                           "TOUCH_ONE_BY_ONE",
                           "TOUCH_ALL_AT_ONCE",
                           "KEYBOARD",
                           "MOUSE",
                           "ACCELERATION",
                           "FOCUS",
                           "GAME_CONTROLLER",
                           "CUSTOM"};
    for (int i = 0; i < 9; ++i)
        if (!manual::property(cx, listener, names[i], i))
            return false;
    JS::RootedValue manager(cx);
    if (!wrapNative(cx, ax::Director::getInstance()->getEventDispatcher(), &manager))
        return false;
    if (!manual::property(cx, cc, "eventManager", manager))
        return false;
    JS::RootedObject object(cx, &manager.toObject());
    if (!manual::function(cx, object, "addListener", addListener, 2) ||
        !manual::function(cx, object, "addCustomListener", addCustom, 2) ||
        !manual::function(cx, object, "dispatchCustomEvent", dispatchCustom, 1) ||
        !manual::method<ax::EventDispatcher>(cx, "_removeLegacyListeners", removeLegacyListeners, 1) ||
        !manual::method<ax::EventListener>(cx, "clone", cloneListener) ||
        !manual::method<ax::PointerEventListener>(cx, "clone", cloneListener) ||
        !manual::method<ax::CustomEvent>(cx, "getUserData", customUserData))
        return false;
    return manual::method<ax::PointerEvent>(cx, "getLocation", pointerGetter<0>) &&
           manual::method<ax::PointerEvent>(cx, "getPreviousLocation", pointerGetter<1>) &&
           manual::method<ax::PointerEvent>(cx, "getStartLocation", pointerGetter<2>) &&
           manual::method<ax::PointerEvent>(cx, "getDelta", pointerGetter<3>) &&
           manual::method<ax::PointerEvent>(cx, "getID", pointerGetter<4>) &&
           manual::method<ax::PointerEvent>(cx, "getLocationInView", pointerGetter<5>) &&
           manual::method<ax::PointerEvent>(cx, "getPreviousLocationInView", pointerGetter<6>) &&
           manual::method<ax::PointerEvent>(cx, "getStartLocationInView", pointerGetter<7>);
}
}  // namespace axjs
