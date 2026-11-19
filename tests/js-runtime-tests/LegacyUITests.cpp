/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "LegacyUITests.h"
#include "runtime/JsRuntime.h"
#include "axmol/ui/Widget.h"
#include "axmol/ui/InputField.h"
#include "axmol/ui/RichText.h"
#include "axmol/ui/Slider.h"
#include "axmol/base/RefPtr.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/FocusEvent.h"
#include "axmol/base/PointerEvent.h"
#include "axmol/scene/Camera.h"

namespace axjs::tests
{
static std::shared_ptr<unsigned> nativeFocusCalls;
unsigned replacedNativeFocusCalls() { return nativeFocusCalls ? *nativeFocusCalls : 0; }
namespace
{
// Exercise the real InputField event dispatcher without a graphics device or
// platform IME. Label/cursor initialization and actual typing remain APK cases.
class NativeInputFixture final : public ax::ui::InputField
{
public:
    using ax::ui::InputField::dispatchEvent;
};
class NativePointerFixture final : public ax::ui::Widget
{
public:
    using ax::ui::Widget::onPointerHitTest;
    using ax::ui::Widget::onPointerDown;
};
class NativeSliderFixture final : public ax::ui::Slider
{
public:
    using ax::ui::Slider::dispatchEvent;
};
}
bool runLegacyUISmoke(ax::JsEngine& engine)
{
    auto input = ax::RefPtr<NativeInputFixture>(new NativeInputFixture, tlx::adopt_object);
    auto pointer = ax::RefPtr<NativePointerFixture>(new NativePointerFixture, tlx::adopt_object);
    auto slider = ax::RefPtr<NativeSliderFixture>(new NativeSliderFixture, tlx::adopt_object);
    if (!pointer->init())
        return false;
    engine.addCleanupCallback([input, pointer, slider] {});
    auto* cx = engine.getContext();
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue value(cx);
        if (!wrapNative(cx, static_cast<ax::ui::InputField*>(input.get()), &value) ||
            !JS_SetProperty(cx, global, "__LegacyNativeInputField", value) ||
            !wrapNative(cx, static_cast<ax::ui::Widget*>(pointer.get()), &value) ||
            !JS_SetProperty(cx, global, "__LegacyNativePointerWidget", value) ||
            !wrapNative(cx, static_cast<ax::ui::Slider*>(slider.get()), &value) ||
            !JS_SetProperty(cx, global, "__LegacyNativeSlider", value))
            return false;
    }
    if (!engine.runScript("LegacyUISmoke.js"))
        return false;
    ax::ui::Widget* widget{};
    ax::ui::Widget* other{};
    ax::ui::RichText* rich{};
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue value(cx);
        if (!JS_GetProperty(cx, global, "LegacyUIProbe", &value) || !value.isObject())
            return false;
        JS::RootedObject probe(cx, &value.toObject());
        if (!JS_GetProperty(cx, probe, "widget", &value) || !toNative(cx, value, &widget) || !widget ||
            !JS_GetProperty(cx, probe, "other", &value) || !toNative(cx, value, &other) || !other ||
            !JS_GetProperty(cx, probe, "rich", &value) || !toNative(cx, value, &rich) || !rich)
            return false;
    }
    if (!widget->onFocusChanged || !widget->onNextFocusedWidget)
        return false;
    widget->onFocusChanged(other, widget);
    if (widget->onNextFocusedWidget(ax::ui::Widget::FocusDirection::RIGHT) != other)
        return false;
    ax::FocusEvent event(other, widget);
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    dispatcher->setEnabled(true);
    dispatcher->dispatchEvent(&event);
    input->dispatchEvent(ax::ui::InputField::EventType::INSERT_TEXT);
    slider->dispatchEvent(ax::ui::Slider::EventType::ON_PERCENTAGE_CHANGED);
    // Use the actual Widget hit test and PointerDown implementation. The ray
    // intersects the widget's real bounds without a GPU or synthetic hit hook.
    auto camera = ax::RefPtr<ax::Camera>(new ax::Camera, tlx::adopt_object);
    const auto point = pointer->convertToWorldSpace(pointer->getContentSize() * 0.5f);
    for (bool swallow : {false, true})
    {
        if (!engine.evalString(swallow ? "LegacyUIProbe.swallowOnPress(true);" : "LegacyUIProbe.swallowOnPress(false);"))
            return false;
        // The callback must affect this press, not only the following press.
        pointer->setSwallowPointerEvents(!swallow);
        ax::PointerEvent press;
        ax::PointerInputState inputState;
        inputState.type = ax::PointerType::Touch;
        press.setPointerInfo(ax::InputPhase::PointerDown, point, inputState);
        press.setCamera(camera.get());
        press.setRay(ax::Ray({point.x, point.y, 1.f}, {0.f, 0.f, -1.f}));
        if (!pointer->onPointerHitTest(&press, nullptr) || !pointer->onPointerDown(&press) ||
            press.isStopped() != swallow || pointer->isSwallowPointerEvents() != swallow)
            return false;
    }
    rich->openUrl("axmol-test://example");
    if (!engine.evalString("LegacyUIProbe.pointerWidget=__LegacyNativePointerWidget;"
                           "__LegacyNativePointerWidget.onFocusChanged=function(){throw Error('replaced native callback entered JS');};"))
        return false;
    // A native caller replaces the bridge after JS registration. VM cleanup
    // must leave this native replacement intact rather than clobbering it.
    nativeFocusCalls = std::make_shared<unsigned>(0);
    pointer->onFocusChanged = [calls = nativeFocusCalls](ax::ui::Widget* previous, ax::ui::Widget* current) {
        if (previous) previous->setFocused(false);
        if (current) current->setFocused(true);
        ++*calls;
    };
    return engine.evalString("var uiCounts=LegacyUIProbe.counts();if(uiCounts.focus!==1||uiCounts.next!==1||uiCounts.events!==1||uiCounts.input!==1||uiCounts.pointer!==2||uiCounts.url!==1||uiCounts.slider!==1||uiCounts.audioSliderUpdating!==false)"
                             "throw Error('native UI focus callback counts');"
                             "cc.eventManager.removeListener(LegacyUIProbe.listener);"
                             "LegacyUIProbe.widget.onFocusChanged=null;LegacyUIProbe.widget.requestFocus();"
                             "if(!LegacyUIProbe.widget.isFocused()||LegacyUIProbe.widget.onFocusChanged!==null)"
                             "throw Error('native default focus after clearing JS callback');"
                             "LegacyUIProbe.widget.setFocused(false);"
                             "LegacyUIProbe.widget.onFocusChanged=function(){throw Error('expired UI callback');};"
                             "LegacyUIProbe.widget.release();LegacyUIProbe.other.release();LegacyUIProbe.rich.release();");
}
}
