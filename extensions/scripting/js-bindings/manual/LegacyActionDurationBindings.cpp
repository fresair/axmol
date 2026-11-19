// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacyActionDurationBindings.h"
#include "manual/ManualUtils.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace axjs
{
namespace
{
using namespace manual;
// v3 ActionInterval initialized |duration| <= MATH_EPSILON to a positive
// epsilon and used that epsilon for the first step. Keep that behavior on
// these JS-created actions only. Modern ax::ActionInterval is untouched.
template<class Base>
class LegacyMinimumInterval final : public Base
{
public:
    void normalizeDuration()
    {
        _minimumDuration = std::abs(this->getDuration()) <= MATH_EPSILON;
        if (_minimumDuration) this->setDuration(MATH_EPSILON);
    }
    void step(float dt) override
    {
        if (!_minimumDuration) return Base::step(dt);
        if (this->_firstTick) { this->_firstTick = false; this->_elapsed = MATH_EPSILON; }
        else this->_elapsed += dt;
        const auto time = std::max(0.0f, std::min(1.0f, this->_elapsed / this->_duration));
        if (!this->sendUpdateEventToScript(time, this)) this->update(time);
        this->_done = this->_elapsed >= this->_duration;
    }
    Base* clone() const override
    {
        if constexpr (std::is_same_v<Base, ax::ActionInterval>) return Base::clone();
        else
        {
            const auto [first, second] = pair();
            return first && second ? makePair(first->clone(), second->clone()) : nullptr;
        }
    }
    Base* reverse() const override
    {
        if constexpr (std::is_same_v<Base, ax::ActionInterval>) return Base::reverse();
        else
        {
            const auto [first, second] = pair();
            if (!first || !second) return nullptr;
            if constexpr (std::is_same_v<Base, ax::Sequence>) return makePair(second->reverse(), first->reverse());
            else return makePair(first->reverse(), second->reverse());
        }
    }
    static Base* make(const ax::Vector<ax::FiniteTimeAction*>& actions)
    {
        auto* result = new LegacyMinimumInterval;
        if (!result->init(actions)) { delete result; return nullptr; }
        result->normalizeDuration(); result->autorelease(); return result;
    }
    static Base* makePair(ax::FiniteTimeAction* first, ax::FiniteTimeAction* second)
    {
        auto* result = new LegacyMinimumInterval;
        if (!result->initWithTwoActions(first, second)) { delete result; return nullptr; }
        result->normalizeDuration(); result->autorelease(); return result;
    }
private:
    auto pair() const
    {
        if constexpr (std::is_same_v<Base, ax::Sequence>) return std::pair(this->_actions[0], this->_actions[1]);
        else return std::pair(this->_one, this->_two);
    }
    bool _minimumDuration{};
};

template<class Base>
bool allocate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc) return error(cx, "Native action allocator expects no arguments");
    JS::RootedObject prototype(cx, getPrototype<Base>(cx));
    if (!prototype) return error(cx, "Native action prototype is unavailable");
    JS::RootedObject wrapper(cx, JS_NewObject(cx, const_cast<JSClass*>(JS_GetClass(prototype)), prototype, JS::NullPtr()));
    if (!wrapper) return false;
    auto* value = new LegacyMinimumInterval<Base>;
    if (!bindNative(cx, wrapper, value, NativeOwnership::Constructor)) { delete value; return false; }
    args.rval().setObject(*wrapper); return true;
}
template<class Base>
void normalize(Base* native)
{
    if (auto* legacy = dynamic_cast<LegacyMinimumInterval<Base>*>(native)) legacy->normalizeDuration();
    else if (std::abs(native->getDuration()) <= MATH_EPSILON) native->setDuration(MATH_EPSILON);
}
bool intervalInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* native = self<ax::ActionInterval>(cx, args);
    float duration{};
    if (!native || argc != 1 || !argument(cx, args, 0, duration)) return error(cx, "initWithDuration expects one finite duration");
    const bool ok = native->initWithDuration(duration);
    if (ok) normalize(native);
    args.rval().setBoolean(ok); return true;
}
bool readActions(JSContext* cx, const JS::CallArgs& args, ax::Vector<ax::FiniteTimeAction*>& actions)
{
    if (args.length() == 1 && args[0].isObject())
    {
        JS::RootedObject array(cx, &args[0].toObject());
        if (JS_IsArrayObject(cx, array))
        {
            if (!fromJsValue(cx, args[0], actions)) return error(cx, "Composite action array expects live finite-time actions");
            return true;
        }
    }
    for (unsigned index = 0; index < args.length(); ++index)
    {
        if (args[index].isNullOrUndefined()) continue;
        ax::FiniteTimeAction* action{};
        if (!argument(cx, args, index, action) || !action) return error(cx, "Composite action expects live finite-time actions");
        actions.pushBack(action);
    }
    return true;
}
template<class Base, bool Pair = false>
bool factory(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if constexpr (Pair) if (argc != 2) return error(cx, "createWithTwoActions expects two actions");
    ax::Vector<ax::FiniteTimeAction*> actions;
    if (!readActions(cx, args, actions)) return false;
    if constexpr (Pair)
        if (actions.size() != 2) return error(cx, "createWithTwoActions expects two live actions");
    return wrapNative(cx, LegacyMinimumInterval<Base>::make(actions), args.rval(), NativeOwnership::Factory);
}
template<class Base, bool Pair = false>
bool initialize(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* native = self<Base>(cx, args);
    if (!native) return false;
    if constexpr (Pair) if (argc != 2) return error(cx, "initWithTwoActions expects two actions");
    ax::Vector<ax::FiniteTimeAction*> actions;
    if (!readActions(cx, args, actions)) return false;
    bool ok;
    if constexpr (Pair)
    {
        if (actions.size() != 2) return error(cx, "initWithTwoActions expects two live actions");
        ok = native->initWithTwoActions(actions.at(0), actions.at(1));
    }
    else ok = native->init(actions);
    if (ok) normalize(native);
    args.rval().setBoolean(ok); return true;
}
template<class Base>
bool install(JSContext* cx)
{
    JS::RootedObject klass(cx, getClassObject<Base>(cx));
    JS::RootedObject prototype(cx, getPrototype<Base>(cx));
    // Generated Action::clone returns Action*. Register the private native
    // subtype against the existing public prototype so its dynamic type and
    // checked base casts survive native clone/reverse without a new JS class.
    if (!prototype || (!detail::getClassInfo(cx, typeid(LegacyMinimumInterval<Base>)) &&
        !registerType<LegacyMinimumInterval<Base>, Base>(cx, prototype, const_cast<JSClass*>(JS_GetClass(prototype))))) return false;
    return klass && function(cx, klass, "__allocate", allocate<Base>, 0) &&
           function(cx, klass, "create", factory<Base>, 1) &&
           function(cx, klass, "createWithTwoActions", factory<Base, true>, 2) &&
           method<Base>(cx, "init", initialize<Base>, 1) &&
           method<Base>(cx, "initWithTwoActions", initialize<Base, true>, 2);
}
}
ax::Sequence* createLegacySequence(const ax::Vector<ax::FiniteTimeAction*>& actions)
{
    return LegacyMinimumInterval<ax::Sequence>::make(actions);
}
ax::Spawn* createLegacySpawn(const ax::Vector<ax::FiniteTimeAction*>& actions)
{
    return LegacyMinimumInterval<ax::Spawn>::make(actions);
}
bool registerLegacyActionDurations(JSContext* cx)
{
    JS::RootedObject interval(cx, getClassObject<ax::ActionInterval>(cx));
    JS::RootedObject prototype(cx, getPrototype<ax::ActionInterval>(cx));
    if (!prototype || (!detail::getClassInfo(cx, typeid(LegacyMinimumInterval<ax::ActionInterval>)) &&
        !registerType<LegacyMinimumInterval<ax::ActionInterval>, ax::ActionInterval>(
            cx, prototype, const_cast<JSClass*>(JS_GetClass(prototype))))) return false;
    return interval && function(cx, interval, "__allocate", allocate<ax::ActionInterval>, 0) &&
           method<ax::ActionInterval>(cx, "initWithDuration", intervalInit, 1) &&
           install<ax::Sequence>(cx) && install<ax::Spawn>(cx);
}
}
