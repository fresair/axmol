/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "LegacyExtensionsTests.h"
#include "runtime/JsRuntime.h"
#include "axmol/ui/EditBox/EditBox.h"
#include "axmol/base/RefPtr.h"
#include "axmol/2d/Label.h"
#include "axmol/ui/Scale9Sprite.h"
#if AX_ENABLE_EXT_GUI
#    include "GUI/ScrollView/TableView.h"
#endif

namespace axjs::tests
{
bool runLegacyExtensionsSmoke(ax::JsEngine& engine)
{
#if AX_ENABLE_EXT_GUI
    // This fixture tests native delegates without creating a platform keyboard
    // or graphics resources. The complete EditBox UI remains an APK test.
    auto box = ax::RefPtr<ax::ui::EditBox>(new ax::ui::EditBox, tlx::adopt_object);
    // Actual native Label/Scale9Sprite objects exercise typed ControlButton
    // initialization without font/texture upload. Their original rendered
    // resource case remains part of the application sweep.
    auto label = ax::RefPtr<ax::Label>(new ax::Label, tlx::adopt_object);
    auto background = ax::RefPtr<ax::ui::Scale9Sprite>(new ax::ui::Scale9Sprite, tlx::adopt_object);
    // Keep this native host fixture alive through VM cleanup, allowing the
    // caller to verify its borrowed delegate was detached before VM destruction.
    engine.addCleanupCallback([box, label, background] {});
    JSContext* cx = engine.getContext();
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue value(cx);
        if (!wrapNative(cx, box.get(), &value) || !JS_SetProperty(cx, global, "__LegacyNativeEditBox", value) ||
            !wrapNative(cx, label.get(), &value) || !JS_SetProperty(cx, global, "__LegacyNativeControlLabel", value) ||
            !wrapNative(cx, background.get(), &value) || !JS_SetProperty(cx, global, "__LegacyNativeControlBackground", value))
            return false;
    }
    if (!engine.runScript("LegacyExtensionsSmoke.js"))
        return false;
    ax::ext::TableView* table{};
    {
        JSAutoRequest request(cx);
        JSAutoCompartment compartment(cx, engine.getGlobalObject());
        JS::RootedObject global(cx, engine.getGlobalObject());
        JS::RootedValue probe(cx), value(cx);
        if (!JS_GetProperty(cx, global, "LegacyExtensionsProbe", &probe) || !probe.isObject())
            return false;
        JS::RootedObject object(cx, &probe.toObject());
        if (!JS_GetProperty(cx, object, "table", &value) || !toNative(cx, value, &table) || !table)
            return false;
    }
    auto* delegate = table->getDelegate();
    auto* cell = table->cellAtIndex(0);
    if (!cell)
        cell = table->cellAtIndex(29);
    if (!delegate || !cell || !box->getDelegate())
        return false;
    delegate->tableCellTouched(table, cell);
    box->getDelegate()->editBoxEditingDidBegin(box.get());
    box->getDelegate()->editBoxTextChanged(box.get(), "中文😀");
    box->getDelegate()->editBoxEditingDidEndWithAction(box.get(), ax::ui::EditBoxDelegate::EditBoxEndAction::RETURN);
    box->getDelegate()->editBoxReturn(box.get());
    engine.garbageCollect();
    return engine.evalString(
        "var lc=LegacyExtensionsProbe.counts();if(lc.touched!==1||lc.edit.begin!==1||lc.edit.changed!==1||"
        "lc.edit.end!==1||lc.edit.action!==1||lc.edit.returned!==1)throw Error('native legacy delegate counts');"
        "LegacyExtensionsProbe.control.release();LegacyExtensionsProbe.scroll.release();LegacyExtensionsProbe.table.release();");
#else
    return true;
#endif
}
}
