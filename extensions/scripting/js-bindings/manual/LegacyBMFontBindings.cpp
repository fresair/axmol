/****************************************************************************
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2026 Axmol contributors.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

SPDX-License-Identifier: MIT
****************************************************************************/
// Adapted from cocos2d-x/v3 CCLabelBMFont.h/.cpp at e4b6a5ef8f.
#include "manual/LegacyBMFontBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/2d/Label.h"
#include "axmol/2d/Sprite.h"
#include "axmol/base/Protocols.h"
#include "axmol/base/RefPtr.h"

namespace axjs
{
namespace
{
using namespace manual;

// LabelProtocol and BlendProtocol are required by native MenuItemLabel and
// other C++ consumers. A JavaScript-only Node composition cannot provide them.
class LegacyBMFont final : public ax::Node, public ax::LabelProtocol, public ax::BlendProtocol
{
public:
    LegacyBMFont()
    {
        _label = ax::Label::create();
        if (!_label)
            throw std::runtime_error("Could not create the native BMFont Label");
        _label->setAnchorPoint(ax::Vec2::zero);
        addChild(_label);
        setAnchorPoint({.5f, .5f});
        setCascadeOpacityEnabled(true);
    }
    ~LegacyBMFont() override = default;

    bool initWithString(std::string_view text,
                        std::string_view file,
                        float width,
                        ax::TextHAlignment alignment,
                        const ax::Vec2& offset)
    {
        if (!_label->setBMFontFilePath(file, {offset.x, offset.y, 0.f, 0.f}, false))
            return false;
        _label->setMaxLineWidth(width);
        _label->setHorizontalAlignment(alignment);
        setString(text);
        return true;
    }
    void setString(std::string_view text) override
    {
        _label->setString(text);
        syncSize();
    }
    std::string_view getString() const override { return _label->getString(); }
    void setAlignment(ax::TextHAlignment alignment)
    {
        _label->setHorizontalAlignment(alignment);
        syncSize();
    }
    void setWidth(float width)
    {
        _label->setMaxLineWidth(width);
        syncSize();
    }
    void setLineBreakWithoutSpace(bool enabled)
    {
        _label->setLineBreakWithoutSpace(enabled);
        syncSize();
    }
    bool setFntFile(std::string_view file, const ax::Rect& rect, bool rotated)
    {
        if (file == _label->getBMFontFilePath())
            return true;
        const bool result = _label->setBMFontFilePath(file, rect, rotated);
        syncSize();
        return result;
    }
    std::string_view getFntFile() const { return _label->getBMFontFilePath(); }
    void setBlendFunc(const ax::BlendFunc& blend) override { _label->setBlendFunc(blend); }
    const ax::BlendFunc& getBlendFunc() const override { return _label->getBlendFunc(); }
    ax::Sprite* getLetter(int index) { return _label->getLetter(index); }
    ax::Node* getChildByTag(int tag) const override { return _label->getLetter(tag); }
    void setColor(const ax::Color32& value) override
    {
        // v3 accepted RGB here: keep the Label's own opacity independent from
        // the container opacity, whose native cascade is applied separately.
        auto color = value;
        color.a = _label->getOpacity();
        _label->setColor(color);
        // Native Tint actions bypass the JS RGB setter's opacity refresh.
        // Reapply the actual Label alpha through its parent-opacity cascade.
        _label->setOpacity(color.a);
    }
    // Intentional native API consistency extension: fixed v3 delegated only
    // setColor, leaving the inherited getter white. Native Tint actions must
    // instead begin at the actual Label color, as must the public JS getter.
    const ax::Color32& getColor() const override { return _label->getColor(); }
    const ax::Color32& getLabelDisplayedColor() const { return _label->getDisplayedColor(); }
    // Do not override Node::getDisplayedColor: the inner Label reads that
    // virtual getter as its parent cascade factor. Returning its own displayed
    // color there would multiply its tint twice on subsequent updates. Only
    // the public JS getDisplayedColor is bound to getLabelDisplayedColor.
    void setOpacityModifyRGB(bool enabled) override
    {
        _label->setOpacityModifyRGB(enabled);
        for (auto* child : getChildren())
            child->setOpacityModifyRGB(enabled);
    }
    bool isOpacityModifyRGB() const override { return _label->isOpacityModifyRGB(); }
    const ax::Vec2& getContentSize() const override
    {
        const_cast<LegacyBMFont*>(this)->syncSize();
        return Node::getContentSize();
    }
    ax::Rect getBoundingBox() const override
    {
        const_cast<LegacyBMFont*>(this)->syncSize();
        return Node::getBoundingBox();
    }
    std::string getDescription() const override
    {
        return "<LabelBMFont | Tag = " + std::to_string(getTag()) + ", Label = '" + std::string(getString()) + "'>";
    }
    ax::Label* getInnerLabel() const { return _label; }

private:
    void syncSize() { Node::setContentSize(_label->getContentSize()); }
    // Retain the actual implementation Label in addition to its scene-child
    // ownership so callers removing children cannot leave protocol pointers
    // dangling. Neither getter adds an implicit retain.
    ax::RefPtr<ax::Label> _label;
};

bool constructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0)
        return error(cx, "Native BMFont allocation requires no arguments");
    return construct<LegacyBMFont>(cx, args);
}
bool allocate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    return constructUninitialized<LegacyBMFont>(cx, args);
}
bool create(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0)
        return error(cx, "Native BMFont factory requires no arguments");
    try
    {
        auto* value = new LegacyBMFont();
        value->autorelease();
        return wrapNative(cx, value, args.rval(), NativeOwnership::Factory);
    }
    catch (const std::exception& failure)
    {
        return error(cx, failure.what());
    }
}
template<auto F> bool nativeMethod(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (invoke<F>(cx, args))
        return true;
    if (!JS_IsExceptionPending(cx))
        JS_ReportError(cx, "Invalid BMFont method arguments");
    return false;
}
JSClass nativeClass = [] {
    JSClass type{};
    type.name = "_LegacyBMFont";
    type.flags = JSCLASS_HAS_PRIVATE;
    type.addProperty = JS_PropertyStub;
    type.delProperty = JS_DeletePropertyStub;
    type.getProperty = JS_PropertyStub;
    type.setProperty = JS_StrictPropertyStub;
    type.enumerate = JS_EnumerateStub;
    type.resolve = JS_ResolveStub;
    type.convert = JS_ConvertStub;
    type.finalize = finalizeNative;
    return type;
}();
#define BMF_METHOD(name, arity) JS_FN(#name, (nativeMethod<&LegacyBMFont::name>), arity, JSPROP_ENUMERATE)
const JSFunctionSpec methods[] = {
    BMF_METHOD(initWithString, 5), BMF_METHOD(setString, 1), BMF_METHOD(getString, 0),
    BMF_METHOD(setAlignment, 1), BMF_METHOD(setWidth, 1), BMF_METHOD(setLineBreakWithoutSpace, 1),
    BMF_METHOD(setFntFile, 3), BMF_METHOD(getFntFile, 0), BMF_METHOD(setBlendFunc, 1), BMF_METHOD(getBlendFunc, 0),
    BMF_METHOD(getLetter, 1), BMF_METHOD(getChildByTag, 1), BMF_METHOD(setColor, 1), BMF_METHOD(getColor, 0),
    JS_FN("getDisplayedColor", (nativeMethod<&LegacyBMFont::getLabelDisplayedColor>), 0, JSPROP_ENUMERATE),
    BMF_METHOD(setOpacityModifyRGB, 1), BMF_METHOD(isOpacityModifyRGB, 0), BMF_METHOD(getContentSize, 0),
    BMF_METHOD(getBoundingBox, 0), BMF_METHOD(getDescription, 0), BMF_METHOD(getInnerLabel, 0), JS_FS_END};
#undef BMF_METHOD
const JSFunctionSpec statics[] = {JS_FN("create", create, 0, 0), JS_FN("__allocate", allocate, 0, 0), JS_FS_END};
}  // namespace

bool registerLegacyBMFont(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject cc(cx, namespaceObject(cx, global, "cc")), parent(cx, getPrototype<ax::Node>(cx));
    if (!cc || !parent)
        return false;
    JS::RootedObject prototype(cx, JS_InitClass(cx, cc, parent, &nativeClass, constructor, 0,
                                              nullptr, methods, nullptr, statics));
    return prototype && registerType<LegacyBMFont, ax::Node>(cx, prototype, &nativeClass);
}
}  // namespace axjs
