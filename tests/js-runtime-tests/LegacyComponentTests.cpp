/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "LegacyComponentTests.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/base/RefPtr.h"
#include "axmol/base/Scheduler.h"
#include "axmol/scene/Scene.h"
#include "axmol/scene/Component.h"

namespace axjs::tests
{
bool runLegacyComponentSmoke(ax::JsEngine& engine)
{
    auto scene = ax::RefPtr<ax::Scene>(new ax::Scene, tlx::adopt_object);
    if (!scene->initWithSize({128.f, 64.f}))
        return false;
    engine.addCleanupCallback([scene] {});
    auto* cx = engine.getContext();
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue value(cx);
        if (!wrapNative(cx, scene.get(), &value) || !JS_SetProperty(cx, global, "__LegacyComponentScene", value))
            return false;
    }
    if (!engine.runScript("LegacyComponentSmoke.js"))
        return false;
    ax::Node* owner{};
    ax::Component* first{};
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue value(cx);
        if (!JS_GetProperty(cx, global, "LegacyComponentProbe", &value) || !value.isObject())
            return false;
        JS::RootedObject probe(cx, &value.toObject());
        if (!JS_GetProperty(cx, probe, "owner", &value) || !toNative(cx, value, &owner) || !owner ||
            !JS_GetProperty(cx, probe, "first", &value) || !toNative(cx, value, &first) || !first)
            return false;
    }
    // Keep the detached component alive to test reattachment and its JS state.
    auto retain = ax::RefPtr<ax::Component>(first);
    auto* scheduler = ax::Director::getInstance()->getScheduler();
    scene->onEnter();
    scheduler->update(0.125f);
    scheduler->update(0.25f);
    if (!engine.evalString("LegacyComponentProbe.verify('updated');")) return false;
    owner->pause();
    scheduler->update(0.125f);
    if (!engine.evalString("LegacyComponentProbe.verify('paused');")) return false;
    owner->resume();
    scheduler->update(0.125f);
    if (!engine.evalString("LegacyComponentProbe.verify('resumed');")) return false;
    scene->onExit();
    scheduler->update(0.25f);
    if (!engine.evalString("LegacyComponentProbe.verify('exited');")) return false;
    scene->onEnter();
    if (!owner->removeComponent(first)) return false;
    scheduler->update(0.25f);
    if (!engine.evalString("LegacyComponentProbe.verify('detached');")) return false;
    if (!owner->addComponent(first)) return false;
    scheduler->update(0.25f);
    if (!engine.evalString("LegacyComponentProbe.verify('reattached');")) return false;
    scene->onExit();
    return true;
}
}
