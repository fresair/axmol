// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacyLabelBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/2d/Label.h"
#include <climits>
#include <cmath>

namespace axjs
{
namespace
{
using namespace manual;
bool configNumber(JSContext* cx, JS::HandleObject object, const char* name, double& result)
{
    JS::RootedValue value(cx);
    if (!JS_GetProperty(cx, object, name, &value)) return false;
    if (value.isUndefined()) return true;
    return JS::ToNumber(cx, value, &result) && std::isfinite(result);
}
bool configString(JSContext* cx, JS::HandleObject object, const char* name, std::string& result)
{
    JS::RootedValue value(cx);
    if (!JS_GetProperty(cx, object, name, &value)) return false;
    if (value.isNullOrUndefined()) { result.clear(); return true; }
    if (!value.isString() && !value.isNumber() && !value.isBoolean()) return false;
    JS::RootedString text(cx, JS::ToString(cx, value));
    if (!text) return false;
    JS::RootedValue string(cx, JS::StringValue(text));
    return fromJsValue(cx, string, result);
}
bool readConfig(JSContext* cx, JS::HandleValue value, ax::TTFConfig& config)
{
    if (!value.isObject()) return error(cx, "TTFConfig must be an object");
    JS::RootedObject object(cx, &value.toObject());
    double size = 12, outline = 0, glyphs = 0;
    if (!configString(cx, object, "fontFilePath", config.fontFilePath) ||
        !configString(cx, object, "customGlyphs", config.customGlyphs) ||
        !configNumber(cx, object, "fontSize", size) || !configNumber(cx, object, "outlineSize", outline) ||
        !configNumber(cx, object, "glyphs", glyphs) || size < 1 || size > INT_MAX ||
        outline < 0 || outline > INT_MAX || glyphs < 0 || glyphs > 3)
        return error(cx, "Invalid TTFConfig field");
    // v3's manual TTFConfig conversion truncates size/outline/glyph collection.
    config.fontSize = static_cast<int>(size);
    config.outlineSize = static_cast<int>(outline);
    config.glyphs = static_cast<ax::GlyphCollection>(static_cast<int>(glyphs));
    if (config.glyphs != ax::GlyphCollection::CUSTOM) config.customGlyphs.clear();
    JS::RootedValue flag(cx);
    if (!JS_GetProperty(cx, object, "distanceFieldEnable", &flag)) return false;
    if (flag.isUndefined() && !JS_GetProperty(cx, object, "distanceFieldEnabled", &flag)) return false;
    config.distanceFieldEnabled = JS::ToBoolean(flag);
    return true;
}
bool createTTF(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc < 2) return error(cx, "createWithTTF requires a config and text, or text, filename and size");
    if (args[0].isObject())
    {
        ax::TTFConfig config;
        std::string text;
        int alignment = 0, width = 0;
        if (argc > 4 || !readConfig(cx, args[0], config) || !argument(cx, args, 1, text) ||
            (argc > 2 && !argument(cx, args, 2, alignment)) || (argc > 3 && !argument(cx, args, 3, width))) return false;
        if (alignment < 0 || alignment > 2) return error(cx, "Invalid TTF text alignment");
        return wrapNative(cx, ax::Label::createWithTTF(config, text, static_cast<ax::TextHAlignment>(alignment), width),
                          args.rval(), NativeOwnership::Factory);
    }
    std::string text, file;
    float size;
    ax::Vec2 dimensions;
    int horizontal = 0, vertical = 0;
    if (argc < 3 || argc > 6 || !argument(cx, args, 0, text) || !argument(cx, args, 1, file) ||
        !argument(cx, args, 2, size) || (argc > 3 && !argument(cx, args, 3, dimensions)) ||
        (argc > 4 && !argument(cx, args, 4, horizontal)) || (argc > 5 && !argument(cx, args, 5, vertical))) return false;
    if (!std::isfinite(size) || size <= 0 || horizontal < 0 || horizontal > 2 || vertical < 0 || vertical > 2)
        return error(cx, "Invalid TTF size or alignment");
    return wrapNative(cx, ax::Label::createWithTTF(text, file, size, dimensions,
        static_cast<ax::TextHAlignment>(horizontal), static_cast<ax::TextVAlignment>(vertical)), args.rval(), NativeOwnership::Factory);
}
bool setConfig(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* label = self<ax::Label>(cx, args);
    ax::TTFConfig config;
    if (!label || argc != 1 || !readConfig(cx, args[0], config)) return false;
    label->setTTFConfig(config);
    args.rval().setUndefined();
    return true;
}
bool getConfig(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* label = self<ax::Label>(cx, args);
    if (!label || argc) return false;
    const auto& config = label->getTTFConfig();
    JS::RootedObject result(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!result || !property(cx,result,"fontFilePath",config.fontFilePath) ||
        !property(cx,result,"fontSize",config.fontSize) || !property(cx,result,"outlineSize",config.outlineSize) ||
        !property(cx,result,"glyphs",static_cast<int>(config.glyphs)) ||
        !property(cx,result,"customGlyphs",config.customGlyphs) ||
        !property(cx,result,"distanceFieldEnable",config.distanceFieldEnabled) ||
        !property(cx,result,"distanceFieldEnabled",config.distanceFieldEnabled)) return false;
    args.rval().setObject(*result);
    return true;
}
}
bool registerLegacyLabel(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject type(cx, getClassObject<ax::Label>(cx));
    return type && function(cx,type,"createWithTTF",createTTF,2) &&
        method<ax::Label>(cx,"setTTFConfig",setConfig,1) && method<ax::Label>(cx,"getTTFConfig",getConfig);
}
}
