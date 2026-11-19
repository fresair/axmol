/****************************************************************************
 Copyright (c) 2015-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2026 Axmol contributors.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
// Legacy contract: cocos2d-x v3 CCComponentJS.{h,cpp}, reference e4b6a5ef8f.
// Script objects keep the canonical axjs wrapper; no Cocos proxy fields or
// private persistent roots are added to ax::Object.
#include "manual/LegacyComponentBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/scene/Component.h"
#include "axmol/base/RefPtr.h"

namespace axjs
{
namespace
{
using namespace manual;

class ScriptComponent final : public ax::Component
{
public:
    explicit ScriptComponent(const std::shared_ptr<RuntimeState>& state)
        : _state(state), _generation(state->generation)
    {}

    void onEnter() override { dispatch("onEnter", ax::kComponentOnEnter, 1.f); }
    void onExit() override { dispatch("onExit", ax::kComponentOnExit, 1.f); }
    void onAdd() override { dispatch("onAdd", ax::kComponentOnAdd, 1.f); }
    void onRemove() override { dispatch("onRemove", ax::kComponentOnRemove, 1.f); }
    void update(float delta) override { dispatch("update", ax::kComponentOnUpdate, delta); }
    void deactivate() { _state.reset(); }

private:
    void dispatch(const char* name, int event, float argument)
    {
        auto state = _state.lock();
        const auto bit = 1u << event;
        if (!state || !state->alive || state->generation != _generation || !state->onOwnerThread() ||
            (_dispatching & bit))
            return;
        // A script may detach and release this component during its callback.
        // Keep the current native virtual call alive through completion.
        ax::RefPtr<ScriptComponent> keepAlive(this);
        auto* cx = state->context;
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, state->engine->getGlobalObject());
        JS::RootedObject receiver(cx, detail::findNativeObject(cx, this));
        if (!receiver)
            return;
        JS::RootedValue function(cx), value(cx), result(cx);
        if (!JS_GetProperty(cx, receiver, name, &function))
        {
            state->engine->reportException();
            return;
        }
        if (!function.isObject() || !JS_ObjectIsCallable(cx, &function.toObject()))
            return;
        // Resolve dynamically so instance fields/overrides changed after load
        // remain visible. Both callable and receiver are rooted for the call.
        value.set(nativeNumberValue(argument));
        _dispatching |= bit;
        const bool ok = JS_CallFunctionValue(cx, receiver, function, JS::HandleValueArray(value), &result);
        _dispatching &= ~bit;
        if (!ok)
            state->engine->reportException();
    }

    std::weak_ptr<RuntimeState> _state;
    uint64_t _generation{};
    unsigned _dispatching{};
};

struct ComponentState
{
    std::vector<ax::WeakPtr<ScriptComponent>> components;
    void clear()
    {
        for (auto& component : components)
            if (auto* native = component.get())
                native->deactivate();
        components.clear();
    }
    void prune() { std::erase_if(components, [](const auto& component) { return !component.get(); }); }
};
std::unordered_map<JSContext*, std::shared_ptr<ComponentState>> states;

bool componentCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string filename;
    if (argc != 1 || !argument(cx, args, 0, filename) || filename.empty())
        return error(cx, "ComponentJS expects a nonempty script filename");
    JS::RootedValue scriptClass(cx);
    if (!engine(cx)->runScript(filename, &scriptClass))
        return error(cx, "ComponentJS could not execute its script");
    if (!scriptClass.isObject())
        return error(cx, "ComponentJS script must return a class with a prototype");
    JS::RootedObject klass(cx, &scriptClass.toObject());
    JS::RootedValue prototypeValue(cx);
    if (!JS_GetProperty(cx, klass, "prototype", &prototypeValue) || !prototypeValue.isObject())
        return error(cx, "ComponentJS script class requires an object prototype");
    JS::RootedObject scriptPrototype(cx, &prototypeValue.toObject());
    JS::RootedObject nativePrototype(cx, getPrototype<ScriptComponent>(cx));
    bool inherits = false;
    for (JS::RootedObject current(cx, scriptPrototype); current;)
    {
        if (current == nativePrototype)
        {
            inherits = true;
            break;
        }
        if (!JS_GetPrototype(cx, current, &current))
            return false;
    }
    if (!inherits)
        return error(cx, "ComponentJS script class must extend cc.ComponentJS");

    auto* native = new ScriptComponent(runtimeState(cx));
    // The reference loader makes an instance of the returned prototype without
    // calling its JS ctor. Initialize the actual native component once.
    if (!native->init() || !wrapNative(cx, native, args.rval(), NativeOwnership::Constructor))
    {
        delete native;
        return false;
    }
    JS::RootedObject wrapper(cx, &args.rval().toObject());
    if (!JS_SetPrototype(cx, wrapper, scriptPrototype))
        return false;
    auto& state = *states.at(cx);
    state.prune();
    state.components.emplace_back(native);
    return true;
}

bool scriptObject(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (!self<ScriptComponent>(cx, args) || argc)
        return error(cx, "ComponentJS.getScriptObject expects no arguments");
    args.rval().set(args.thisv());
    return true;
}

template <int Event>
bool componentBase(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* component = self<ScriptComponent>(cx, args);
    if (!component)
        return false;
    if constexpr (Event == ax::kComponentOnUpdate)
    {
        float delta{};
        if (argc != 1 || !argument(cx, args, 0, delta))
            return error(cx, "ComponentJS.update expects a frame delta");
        component->ax::Component::update(delta);
    }
    else
    {
        if (argc > 1)
            return error(cx, "ComponentJS lifecycle method expects no arguments");
        if constexpr (Event == ax::kComponentOnEnter) component->ax::Component::onEnter();
        if constexpr (Event == ax::kComponentOnExit) component->ax::Component::onExit();
        if constexpr (Event == ax::kComponentOnAdd) component->ax::Component::onAdd();
        if constexpr (Event == ax::kComponentOnRemove) component->ax::Component::onRemove();
    }
    args.rval().setUndefined();
    return true;
}

JSClass componentClass = [] {
    JSClass klass{};
    klass.name = "ComponentJS";
    klass.flags = JSCLASS_HAS_PRIVATE;
    klass.addProperty = JS_PropertyStub;
    klass.delProperty = JS_DeletePropertyStub;
    klass.getProperty = JS_PropertyStub;
    klass.setProperty = JS_StrictPropertyStub;
    klass.enumerate = JS_EnumerateStub;
    klass.resolve = JS_ResolveStub;
    klass.convert = JS_ConvertStub;
    klass.finalize = finalizeNative;
    return klass;
}();
const JSFunctionSpec methods[] = {
    JS_FN("getScriptObject", scriptObject, 0, JSPROP_ENUMERATE),
    JS_FN("onEnter", componentBase<ax::kComponentOnEnter>, 0, JSPROP_ENUMERATE),
    JS_FN("onExit", componentBase<ax::kComponentOnExit>, 0, JSPROP_ENUMERATE),
    JS_FN("onAdd", componentBase<ax::kComponentOnAdd>, 0, JSPROP_ENUMERATE),
    JS_FN("onRemove", componentBase<ax::kComponentOnRemove>, 0, JSPROP_ENUMERATE),
    JS_FN("update", componentBase<ax::kComponentOnUpdate>, 1, JSPROP_ENUMERATE),
    JS_FS_END
};
const JSFunctionSpec statics[] = {JS_FN("create", componentCreate, 1, JSPROP_ENUMERATE), JS_FS_END};
}

bool registerLegacyComponent(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject cc(cx, namespaceObject(cx, global, "cc"));
    JS::RootedObject parent(cx, getPrototype<ax::Component>(cx));
    if (!cc || !parent)
        return error(cx, "ComponentJS requires the native Component binding");
    JS::RootedObject prototype(cx, JS_InitClass(cx, cc, parent, &componentClass, componentCreate, 1,
                                              nullptr, methods, nullptr, statics));
    if (!prototype || !registerType<ScriptComponent, ax::Component>(cx, prototype, &componentClass))
        return false;
    auto state = std::make_shared<ComponentState>();
    states[cx] = state;
    engine(cx)->addCleanupCallback([cx, state] {
        state->clear();
        states.erase(cx);
    });
    engine(cx)->addUpdateCallback([state](float) { state->prune(); });
    return true;
}
}
