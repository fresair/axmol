// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacySchedulerBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/base/Scheduler.h"
#include "axmol/2d/ActionManager.h"
#include <algorithm>
#include <climits>
#include <cfloat>
#include <unordered_map>

namespace axjs
{
namespace
{
using namespace manual;
struct Target;
struct Invocation
{
    std::shared_ptr<Target> target;
    std::shared_ptr<Callback> callback;
    void operator()(float dt) const;
};
struct Target
{
    std::unique_ptr<JS::PersistentRootedObject> receiver;
    ax::WeakPtr<ax::Scheduler> scheduler;
    ax::WeakPtr<ax::Object> native;
    bool hasNative;
    void* key;
    std::weak_ptr<Callback> update;
    std::unordered_map<std::string, std::weak_ptr<Callback>> timers;
    Target(JSContext* cx, JS::HandleObject object, ax::Scheduler* owner, ax::Object* target)
        : receiver(std::make_unique<JS::PersistentRootedObject>(cx, object)), scheduler(owner), native(target), hasNative(target != nullptr),
          key(target ? static_cast<void*>(target) : this) {}
    bool live() const { return receiver && scheduler.get() && (!hasNative || native.get()); }
    bool owns(const ax::SchedulerFunc* function) const
    {
        const auto* invocation = function ? function->target<Invocation>() : nullptr;
        return invocation && invocation->target.get() == this;
    }
    void cancel()
    {
        if (auto* owner = scheduler.get())
        {
            // A removed callback can stay alive until the end of the active
            // Scheduler tick. Check the current entry, not its reference count.
            if (owns(owner->getScheduledUpdateCallback(key))) owner->unscheduleUpdate(key);
            for (const auto& [name, callback] : timers)
                if (owns(owner->getScheduledCallback(name, key))) owner->unschedule(name, key);
        }
        update.reset();
        timers.clear();
    }
};
void Invocation::operator()(float dt) const
{
    if (target->live()) call(callback, dt);
}
struct State
{
    uint64_t next{1};
    std::vector<std::shared_ptr<Target>> targets;
    std::vector<std::weak_ptr<Target>> allTargets;
    void prune()
    {
        std::erase_if(targets, [](const auto& target) {
            if (!target->live()) { target->cancel(); return true; }
            auto* owner = target->scheduler.get();
            std::erase_if(target->timers, [&](const auto& timer) {
                return !target->owns(owner->getScheduledCallback(timer.first, target->key));
            });
            return !target->owns(owner->getScheduledUpdateCallback(target->key)) && target->timers.empty();
        });
        std::erase_if(allTargets, [](const auto& target) { return target.expired(); });
    }
    std::shared_ptr<Target> find(JSContext* cx, ax::Scheduler* scheduler, JS::HandleValue value, bool create)
    {
        if (!value.isObject()) { error(cx, "Scheduler target must be an object"); return {}; }
        prune();
        for (auto& target : targets)
            if (target->scheduler.get() == scheduler && target->receiver->get() == &value.toObject())
                return target;
        if (!create) return {};
        auto* native = detail::nativeObjectBase(cx, value);
        if (!native && detail::getNativePrototype(cx, value))
        { error(cx, "Scheduler target has been destroyed"); return {}; }
        JS::RootedObject receiver(cx, &value.toObject());
        auto target = std::make_shared<Target>(cx, receiver, scheduler, native);
        targets.push_back(target);
        allTargets.push_back(target);
        return target;
    }
};
std::unordered_map<JSContext*, std::shared_ptr<State>> states;

bool number(JSContext* cx, const JS::CallArgs& args, unsigned index, double& value)
{
    if (index >= args.length() || args[index].isUndefined()) return true;
    return JS::ToNumber(cx, args[index], &value);
}
bool scheduleUpdate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler || !argc) return error(cx, "scheduleUpdateForTarget requires a target");
    auto target = states.at(cx)->find(cx, scheduler, args[0], true);
    if (!target) return false;
    JS::RootedObject receiver(cx, *target->receiver);
    JS::RootedValue function(cx);
    if (!JS_GetProperty(cx, receiver, "update", &function)) return false;
    auto callback = Callback::create(cx, function, receiver);
    if (!callback) return false;
    double priority = 0;
    if (!number(cx, args, 1, priority)) return false;
    if (!std::isfinite(priority) || priority < INT_MIN || priority > INT_MAX)
        return error(cx, "Invalid update priority");
    const bool paused = argc > 2 && JS::ToBoolean(args[2]);
    // Native Scheduler intentionally retains the old callback on a duplicate
    // target. Replacing a JS update must remove that entry first.
    scheduler->unscheduleUpdate(target->key);
    target->update = callback;
    scheduler->scheduleUpdate(Invocation{target, callback}, target->key, static_cast<int>(priority), paused);
    args.rval().setUndefined();
    return true;
}
template<bool TargetFirst>
bool schedule(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler || argc < 2) return error(cx, "schedule requires a callback and target");
    unsigned fnIndex = TargetFirst ? 1 : 0, targetIndex = TargetFirst ? 0 : 1;
    if constexpr (!TargetFirst)
        if (args[1].isObject() && JS_ObjectIsCallable(cx, &args[1].toObject()))
        { fnIndex = 1; targetIndex = 0; }
    auto state = states.at(cx);
    auto target = state->find(cx, scheduler, args[targetIndex], true);
    if (!target) return false;
    JS::RootedObject receiver(cx, *target->receiver);
    JS::RootedValue fn(cx, args[fnIndex]);
    if (fn.isString())
    {
        std::string selector;
        if (!fromJsValue(cx, fn, selector) || !JS_GetProperty(cx, receiver, selector.c_str(), &fn)) return false;
    }
    auto callback = Callback::create(cx, fn, receiver);
    if (!callback) return false;
    double interval = 0, repeat = UINT_MAX, delay = 0;
    bool paused = false;
    JS::RootedValue instanceId(cx);
    if (!JS_GetProperty(cx, receiver, "__instanceId", &instanceId)) return false;
    JS::RootedString defaultKey(cx, JS::ToString(cx, instanceId));
    if (!defaultKey) return false;
    JS::RootedValue keyValue(cx, JS::StringValue(defaultKey));
    std::string key;
    if (!fromJsValue(cx, keyValue, key)) return false;
    if (!number(cx, args, 2, interval)) return false;
    if (!TargetFirst && (argc == 4 || argc == 5))
    {
        paused = JS::ToBoolean(args[3]);
        if (argc == 5 && !args[4].isUndefined() && !argument(cx, args, 4, key)) return false;
    }
    else
    {
        if (!number(cx, args, 3, repeat) || !number(cx, args, 4, delay)) return false;
        paused = argc > 5 && JS::ToBoolean(args[5]);
        if (argc > 6 && !args[6].isUndefined() && !argument(cx, args, 6, key)) return false;
    }
    if (!std::isfinite(interval) || interval < 0 || interval > FLT_MAX ||
        !std::isfinite(delay) || delay < 0 || delay > FLT_MAX || key.empty() ||
        !std::isfinite(repeat) || repeat < 0 || repeat > UINT_MAX)
        return error(cx, "Invalid schedule interval, repeat or delay");
    if constexpr (TargetFirst)
    {
        JS::RootedObject fnObject(cx, &fn.toObject());
        JS::RootedValue id(cx);
        if (!JS_GetProperty(cx, fnObject, "__axmolSchedulerCallbackId", &id)) return false;
        if (!id.isNumber())
        {
            id.set(nativeNumberValue(static_cast<double>(state->next++)));
            if (!JS_DefineProperty(cx, fnObject, "__axmolSchedulerCallbackId", id, 0)) return false;
        }
        key = "js.scheduler.callback." + std::to_string(static_cast<uint64_t>(id.toNumber()));
    }
    const auto found = target->timers.find(key);
    if (found == target->timers.end() || found->second.expired()) target->timers[key] = callback;
    scheduler->schedule(Invocation{target, callback}, target->key, static_cast<float>(interval),
                        static_cast<unsigned>(repeat), static_cast<float>(delay), paused, key);
    args.rval().setUndefined();
    return true;
}
// update-only, all-for-target, pause, resume, is-paused
template<int Operation>
bool targetOperation(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler || !argc) return error(cx, "Scheduler operation requires a target");
    auto state = states.at(cx);
    auto target = state->find(cx, scheduler, args[0], false);
    if (JS_IsExceptionPending(cx)) return false;
    void* key = target ? target->key : detail::nativeObjectBase(cx, args[0]);
    args.rval().setUndefined();
    if constexpr (Operation == 4) args.rval().setBoolean(key && scheduler->isTargetPaused(key));
    if (key)
    {
        if constexpr (Operation == 0) { scheduler->unscheduleUpdate(key); if (target) target->update.reset(); }
        if constexpr (Operation == 1) { scheduler->unscheduleAllForTarget(key); if (target) {target->update.reset();target->timers.clear();} }
        if constexpr (Operation == 2) scheduler->pauseTarget(key);
        if constexpr (Operation == 3) scheduler->resumeTarget(key);
    }
    state->prune();
    return true;
}
bool unschedule(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler || argc < 2) return error(cx, "unschedule requires a key and target");
    std::string key;
    if (!argument(cx, args, 0, key)) return false;
    auto state = states.at(cx);
    auto target = state->find(cx, scheduler, args[1], false);
    if (JS_IsExceptionPending(cx)) return false;
    void* native = target ? target->key : detail::nativeObjectBase(cx, args[1]);
    if (native) scheduler->unschedule(key, native);
    if (target) target->timers.erase(key);
    state->prune();
    args.rval().setUndefined();
    return true;
}
bool unscheduleCallback(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc && args[0].isString()) return unschedule(cx, argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler || argc != 2 || !args[1].isObject())
        return error(cx, "unscheduleCallbackForTarget requires a target and callback");
    auto state = states.at(cx);
    auto target = state->find(cx, scheduler, args[0], false);
    if (JS_IsExceptionPending(cx)) return false;
    JS::RootedObject fn(cx, &args[1].toObject());
    JS::RootedValue id(cx);
    if (!JS_GetProperty(cx, fn, "__axmolSchedulerCallbackId", &id)) return false;
    if (target && id.isNumber())
    {
        auto key = "js.scheduler.callback." + std::to_string(static_cast<uint64_t>(id.toNumber()));
        scheduler->unschedule(key, target->key);
        target->timers.erase(key);
    }
    state->prune();
    args.rval().setUndefined();
    return true;
}
template<bool Minimum>
bool unscheduleAll(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* scheduler = self<ax::Scheduler>(cx, args);
    if (!scheduler) return false;
    if constexpr (Minimum)
    {
        int priority = 0;
        if (!argument(cx, args, 0, priority)) return false;
        scheduler->unscheduleAllWithMinPriority(priority);
    }
    else scheduler->unscheduleAll();
    states.at(cx)->prune();
    args.rval().setUndefined();
    return true;
}
template<class T>
bool nativeUpdate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* target = self<T>(cx, args);
    float dt;
    if (!target || !argument(cx, args, 0, dt) || !std::isfinite(dt)) return error(cx, "Invalid update delta");
    target->update(dt);
    args.rval().setUndefined();
    return true;
}
}
bool registerLegacyScheduler(JSContext* cx, JS::HandleObject)
{
    auto state = std::make_shared<State>();
    states[cx] = state;
    engine(cx)->addUpdateCallback([state](float) { state->prune(); });
    engine(cx)->addCleanupCallback([cx, state] {
        // A native callback currently executing, or awaiting deferred deletion,
        // can outlive this VM even after its record was pruned from targets.
        for (const auto& weak : state->allTargets)
            if (auto target = weak.lock()) { target->cancel(); target->receiver.reset(); }
        state->targets.clear();
        state->allTargets.clear();
        states.erase(cx);
    });
    return method<ax::Scheduler>(cx,"scheduleUpdateForTarget",scheduleUpdate,1) &&
        method<ax::Scheduler>(cx,"schedule",schedule<false>,2) &&
        method<ax::Scheduler>(cx,"scheduleCallbackForTarget",schedule<true>,2) &&
        method<ax::Scheduler>(cx,"unschedule",unschedule,2) &&
        method<ax::Scheduler>(cx,"unscheduleCallbackForTarget",unscheduleCallback,2) &&
        method<ax::Scheduler>(cx,"unscheduleUpdate",targetOperation<0>,1) &&
        method<ax::Scheduler>(cx,"unscheduleUpdateForTarget",targetOperation<0>,1) &&
        method<ax::Scheduler>(cx,"unscheduleAllCallbacksForTarget",targetOperation<1>,1) &&
        method<ax::Scheduler>(cx,"unscheduleAllForTarget",targetOperation<1>,1) &&
        method<ax::Scheduler>(cx,"pauseTarget",targetOperation<2>,1) &&
        method<ax::Scheduler>(cx,"resumeTarget",targetOperation<3>,1) &&
        method<ax::Scheduler>(cx,"isTargetPaused",targetOperation<4>,1) &&
        method<ax::Scheduler>(cx,"unscheduleAllCallbacks",unscheduleAll<false>) &&
        method<ax::Scheduler>(cx,"unscheduleAll",unscheduleAll<false>) &&
        method<ax::Scheduler>(cx,"unscheduleAllWithMinPriority",unscheduleAll<true>,1) &&
        method<ax::Scheduler>(cx,"unscheduleAllCallbacksWithMinPriority",unscheduleAll<true>,1) &&
        method<ax::Scheduler>(cx,"update",nativeUpdate<ax::Scheduler>,1) &&
        method<ax::ActionManager>(cx,"update",nativeUpdate<ax::ActionManager>,1);
}
}
