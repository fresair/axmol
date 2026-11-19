// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacyActionBindings.h"
#include "manual/ManualUtils.h"
#include "manual/LegacyActionDurationBindings.h"
#include "runtime/JsEngine.h"
#include "axmol/2d/ActionManager.h"
#include "axmol/2d/ActionInterval.h"
#include "axmol/2d/ActionEase.h"
#include <cmath>
#include <unordered_map>

namespace axjs
{
namespace
{
using namespace manual;
struct LegacyOrderLeases
{
    uint64_t generation{};
    std::vector<ax::WeakPtr<ax::ActionManager>> managers;
};
// Context-local VM state, not an ownership/root index for native managers.
std::unordered_map<JSContext*, std::shared_ptr<LegacyOrderLeases>> orderLeases;

bool leaseTargetOrder(JSContext* cx, ax::ActionManager* manager)
{
    auto* vm = engine(cx);
    auto it = orderLeases.find(cx);
    if (!manager || !vm || it == orderLeases.end() || it->second->generation != vm->getGeneration())
        return error(cx, "Legacy action ordering has no live VM/manager");
    auto& managers = it->second->managers;
    for (const auto& weak : managers)
        if (weak.get() == manager)
            return true;
    managers.emplace_back(manager); // WeakPtr includes the native generation.
    manager->acquireLegacyTargetOrder();
    return true;
}

bool runAction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    ax::Action* action{};
    if (!node || argc != 1 || !argument(cx, args, 0, action) || !action)
        return error(cx, "runAction expects a live action");
    if (!leaseTargetOrder(cx, node->getActionManager()))
        return false;
    return invoke<static_cast<ax::Action* (ax::Node::*)(ax::Action*)>(&ax::Node::runAction)>(cx, args);
}

bool addAction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* manager = self<ax::ActionManager>(cx, args);
    ax::Action* action{};
    ax::Node* target{};
    bool paused{};
    if (!manager || argc != 3 || !argument(cx, args, 0, action) || !action ||
        !argument(cx, args, 1, target) || !target || !argument(cx, args, 2, paused))
        return error(cx, "addAction expects action, target and paused");
    if (!leaseTargetOrder(cx, manager))
        return false;
    return invoke<static_cast<void (ax::ActionManager::*)(ax::Action*, ax::Node*, bool)>(&ax::ActionManager::addAction)>(cx, args);
}

bool replace(JSContext* cx, const JS::CallArgs& args, ax::Action* replacement)
{
    if (!replacement)
        return error(cx, "Could not create the native action decorator");
    JS::RootedObject wrapper(cx, &args.thisv().toObject());
    if (!rebindNative(cx, wrapper, replacement))
        return false;
    args.rval().setObject(*wrapper);
    return true;
}
template <int Mode>
bool decorate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args    = JS::CallArgsFromVp(argc, vp);
    auto* action = self<ax::ActionInterval>(cx, args);
    if (!action)
        return false;
    if constexpr (Mode == 1)
    {
        if (argc)
            return error(cx, "repeatForever expects no arguments");
        return replace(cx, args, ax::RepeatForever::create(action));
    }
    else
    {
        double value;
        if (argc != 1 || !argument(cx, args, 0, value) || !std::isfinite(value))
            return error(cx, "Invalid action decorator argument");
        if constexpr (Mode == 0)
        {
            if (value < 1 || value > UINT32_MAX)
                return error(cx, "Repeat count must be positive and fit uint32");
            return replace(cx, args, ax::Repeat::create(action, static_cast<uint32_t>(value)));
        }
        else
        {
            if (value < 0)
                return error(cx, "Speed must not be negative");
            return replace(cx, args, ax::Speed::create(action, static_cast<float>(value)));
        }
    }
}
bool easing(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args    = JS::CallArgsFromVp(argc, vp);
    auto* action = self<ax::ActionInterval>(cx, args);
    if (!action || !argc || !args[0].isObject())
        return error(cx, "easing expects an ease descriptor");
    JS::RootedObject descriptor(cx, &args[0].toObject());
    JS::RootedValue value(cx);
    int tag;
    if (!JS_GetProperty(cx, descriptor, "tag", &value) || !fromJsValue(cx, value, tag))
        return error(cx, "Invalid ease tag");
    auto parameter = [&](const char* name, float& number) {
        return JS_GetProperty(cx, descriptor, name, &value) && fromJsValue(cx, value, number) && std::isfinite(number);
    };
    float rate = 0.3f;
    if (tag <= 2 && !parameter("param", rate))
        return error(cx, "Rate easing requires a numeric rate");
    if (tag >= 9 && tag <= 11)
    {
        if (!JS_GetProperty(cx, descriptor, "param", &value))
            return false;
        if (!value.isUndefined() && (!fromJsValue(cx, value, rate) || !std::isfinite(rate)))
            return error(cx, "Invalid elastic period");
    }
    ax::ActionInterval* result = nullptr;
#define AXJS_EASE_CASE(TAG, TYPE)          \
    case TAG:                              \
        result = ax::TYPE::create(action); \
        break
#define AXJS_RATE_CASE(TAG, TYPE)                \
    case TAG:                                    \
        result = ax::TYPE::create(action, rate); \
        break
    switch (tag)
    {
        AXJS_RATE_CASE(0, EaseIn);
        AXJS_RATE_CASE(1, EaseOut);
        AXJS_RATE_CASE(2, EaseInOut);
        AXJS_EASE_CASE(3, EaseExponentialIn);
        AXJS_EASE_CASE(4, EaseExponentialOut);
        AXJS_EASE_CASE(5, EaseExponentialInOut);
        AXJS_EASE_CASE(6, EaseSineIn);
        AXJS_EASE_CASE(7, EaseSineOut);
        AXJS_EASE_CASE(8, EaseSineInOut);
        AXJS_RATE_CASE(9, EaseElasticIn);
        AXJS_RATE_CASE(10, EaseElasticOut);
        AXJS_RATE_CASE(11, EaseElasticInOut);
        AXJS_EASE_CASE(12, EaseBounceIn);
        AXJS_EASE_CASE(13, EaseBounceOut);
        AXJS_EASE_CASE(14, EaseBounceInOut);
        AXJS_EASE_CASE(15, EaseBackIn);
        AXJS_EASE_CASE(16, EaseBackOut);
        AXJS_EASE_CASE(17, EaseBackInOut);
    case 18:
    {
        float p0, p1, p2, p3;
        if (!parameter("param", p0) || !parameter("param2", p1) || !parameter("param3", p2) || !parameter("param4", p3))
            return error(cx, "Bezier easing requires four control values");
        auto* bezier = ax::EaseBezierAction::create(action);
        if (bezier)
            bezier->setBezierParamer(p0, p1, p2, p3);
        result = bezier;
        break;
    }
        AXJS_EASE_CASE(19, EaseQuadraticActionIn);
        AXJS_EASE_CASE(20, EaseQuadraticActionOut);
        AXJS_EASE_CASE(21, EaseQuadraticActionInOut);
        AXJS_EASE_CASE(22, EaseQuarticActionIn);
        AXJS_EASE_CASE(23, EaseQuarticActionOut);
        AXJS_EASE_CASE(24, EaseQuarticActionInOut);
        AXJS_EASE_CASE(25, EaseQuinticActionIn);
        AXJS_EASE_CASE(26, EaseQuinticActionOut);
        AXJS_EASE_CASE(27, EaseQuinticActionInOut);
        AXJS_EASE_CASE(28, EaseCircleActionIn);
        AXJS_EASE_CASE(29, EaseCircleActionOut);
        AXJS_EASE_CASE(30, EaseCircleActionInOut);
        AXJS_EASE_CASE(31, EaseCubicActionIn);
        AXJS_EASE_CASE(32, EaseCubicActionOut);
        AXJS_EASE_CASE(33, EaseCubicActionInOut);
    default:
        return error(cx, "Unknown ease descriptor tag");
    }
#undef AXJS_EASE_CASE
#undef AXJS_RATE_CASE
    return replace(cx, args, result);
}
}  // namespace
bool registerLegacyActions(JSContext* cx, JS::HandleObject)
{
    auto* vm = engine(cx);
    if (!vm || orderLeases.contains(cx))
        return error(cx, "Legacy action ordering requires one registration per VM");
    auto state = std::make_shared<LegacyOrderLeases>();
    state->generation = vm->getGeneration();
    orderLeases.emplace(cx, state);
    vm->addCleanupCallback([cx, state] {
        for (const auto& weak : state->managers)
            if (auto* manager = weak.get())
                manager->releaseLegacyTargetOrder();
        auto it = orderLeases.find(cx);
        if (it != orderLeases.end() && it->second == state)
            orderLeases.erase(it);
    });
    return method<ax::Node>(cx, "runAction", runAction, 1) &&
           method<ax::ActionManager>(cx, "addAction", addAction, 3) &&
           registerLegacyActionDurations(cx) &&
           method<ax::ActionInterval>(cx, "repeat", decorate<0>, 1) &&
           method<ax::ActionInterval>(cx, "repeatForever", decorate<1>, 0) &&
           method<ax::ActionInterval>(cx, "_speed", decorate<2>, 1) &&
           method<ax::ActionInterval>(cx, "easing", easing, 1);
}
}  // namespace axjs
