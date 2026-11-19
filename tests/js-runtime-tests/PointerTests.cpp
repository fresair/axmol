/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "PointerTests.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/PointerEvent.h"
#include "axmol/base/PointerEventListener.h"
#include "axmol/base/RefPtr.h"

namespace axjs::tests
{
bool runPointerSmoke(ax::JsEngine* engine)
{
    if (!engine->runScript("PointerSmoke.js")) return false;
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    auto dispatch = [&](ax::InputPhase phase, intptr_t id, ax::PointerType type = ax::PointerType::Touch) {
        ax::PointerEvent event;
        ax::PointerInputState input;
        input.id = id;
        input.type = type;
        if(type == ax::PointerType::Mouse) { input.pressedButtons = phase==ax::InputPhase::PointerUp ? 0 : 1; input.button = ax::InputButton::Primary; }
        event.setPointerInfo(phase, {40, 60}, input);
        dispatcher->dispatchEvent(&event);
    };
    for (int test = 0; test < 5; ++test)
    {
        if (!engine->evalString("pointerSmokeSetup(" + std::to_string(test) + ");")) return false;
        // Hover/unclaimed move must never reach a one-by-one callback.
        if (test != 3) dispatch(ax::InputPhase::PointerMove, 42);
        dispatch(ax::InputPhase::PointerDown, 42);
        dispatch(ax::InputPhase::PointerMove, 42);
        if (test != 3) dispatch(ax::InputPhase::PointerMove, 43);
        dispatch(test == 2 ? ax::InputPhase::PointerCancel : ax::InputPhase::PointerUp, 42);
        if (test != 3) dispatch(ax::InputPhase::PointerMove, 42);
        if (engine->executeGlobalFunction("pointerSmokeResult") != 0) return false;
        if (!engine->evalString("pointerSmokeDispose();")) return false;
    }
    for (bool cancelled : {false,true})
    {
        if(engine->executeGlobalFunction("pointerCloneSetup")!=0) return false;
        dispatch(ax::InputPhase::PointerDown,42);
        dispatch(ax::InputPhase::PointerMove,42);
        dispatch(cancelled?ax::InputPhase::PointerCancel:ax::InputPhase::PointerUp,42);
        if(!engine->evalString(cancelled?"pointerCloneResult(true);":"pointerCloneResult(false);")) return false;
    }
    if(!engine->evalString("pointerCloneSetup();pointerSmokeDispose();pointerCloneCounts={began:0,moved:0,ended:0,cancelled:0};")) return false;
    ax::PointerEventListener* source{};
    {
        auto* cx=engine->getContext();
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx,engine->getGlobalObject());
        JS::RootedObject global(cx,engine->getGlobalObject());
        JS::RootedValue value(cx);
        // Create the native source through the actual JS descriptor factory.
        // Attach it before Down, then call its C++ virtual clone with a live claim.
        if(!engine->evalString("pointerCloneSetup();var pointerNativeCloneSource=pointerSmokeListeners[0];pointerSmokeDispose();pointerCloneCounts={began:0,moved:0,ended:0,cancelled:0};") ||
           !JS_GetProperty(cx,global,"pointerNativeCloneSource",&value) || !axjs::toNative(cx,value,&source) || !source) return false;
    }
    ax::PointerEvent down,move,up;
    ax::PointerInputState touch;touch.id=73;touch.type=ax::PointerType::Touch;
    down.setPointerInfo(ax::InputPhase::PointerDown,{40,60},touch);
    move.setPointerInfo(ax::InputPhase::PointerMove,{40,60},touch);
    up.setPointerInfo(ax::InputPhase::PointerUp,{40,60},touch);
    dispatcher->addEventListenerWithFixedPriority(source,-30);
    struct NativeCloneRegistrationGuard {
        ax::EventDispatcher* dispatcher;
        ax::PointerEventListener* source;
        ax::PointerEventListener* clone{};
        ~NativeCloneRegistrationGuard() {
            if (clone) dispatcher->removeEventListener(clone);
            if (source) dispatcher->removeEventListener(source);
        }
    } cloneRegistration{dispatcher,source};
    dispatcher->dispatchEvent(&down);
    auto* clone=source->clone();
    if(!clone) return false;
    dispatcher->addEventListenerWithFixedPriority(clone,-29);
    cloneRegistration.clone=clone;
    clone->onPointerMove(&move);
    if(!clone->onPointerDown(&down)) return false;
    source->onPointerUp(&up);
    clone->onPointerUp(&up);
    if(engine->executeGlobalFunction("pointerCloneFreshResult")!=0) return false;
    dispatcher->removeEventListener(source);
    dispatcher->removeEventListener(clone);
    cloneRegistration.source=cloneRegistration.clone=nullptr;

    unsigned nativePresses{};
    auto native=ax::RefPtr<ax::PointerEventListener>(ax::PointerEventListener::create());
    native->onPointerDown=[&](ax::PointerEvent*){++nativePresses;return true;};
    dispatcher->addEventListenerWithFixedPriority(native.get(),-5);
    struct RemoveNativeListener
    {
        ax::EventDispatcher* dispatcher;
        ax::EventListener* listener;
        ~RemoveNativeListener(){dispatcher->removeEventListener(listener);}
    } guard{dispatcher,native.get()};
    if(engine->executeGlobalFunction("pointerRemovalSetup")!=0) return false;
    dispatch(ax::InputPhase::PointerDown,100);
    dispatch(ax::InputPhase::PointerUp,100);
    dispatch(ax::InputPhase::PointerDown,200,ax::PointerType::Mouse);
    dispatch(ax::InputPhase::PointerUp,200,ax::PointerType::Mouse);
    if(!engine->evalString("cc.eventManager.dispatchCustomEvent('pointer-type-sentinel');pointerRemovalResult(0);cc.eventManager.removeListeners(cc.EventListener.MOUSE);")) return false;
    dispatch(ax::InputPhase::PointerDown,201,ax::PointerType::Mouse);
    dispatch(ax::InputPhase::PointerUp,201,ax::PointerType::Mouse);
    if(!engine->evalString("cc.eventManager.dispatchCustomEvent('pointer-type-sentinel');pointerRemovalResult(1);cc.eventManager.removeListeners(cc.EventListener.TOUCH_ALL_AT_ONCE);cc.eventManager.removeListeners(cc.EventListener.CUSTOM);")) return false;
    dispatch(ax::InputPhase::PointerDown,101);
    dispatch(ax::InputPhase::PointerUp,101);
    if(!engine->evalString("cc.eventManager.dispatchCustomEvent('pointer-type-sentinel');pointerRemovalResult(2);pointerSmokeDispose();")) return false;
    return nativePresses==4;
}
}  // namespace axjs::tests
