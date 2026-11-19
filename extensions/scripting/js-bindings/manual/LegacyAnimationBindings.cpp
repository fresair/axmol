/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyAnimationBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/2d/Animation.h"

namespace axjs
{
namespace
{
using namespace manual;

// The v3 ValueMap conversion accepts null/undefined as an empty dictionary.
// Keep that policy local to the old AnimationFrame API; its data is stored in
// the real native AnimationFrame and uses the common recursive Value converter.
bool userInfo(JSContext* cx, JS::HandleValue value, ax::ValueMap& result)
{
    return value.isNullOrUndefined() || fromJsValue(cx, value, result);
}
bool frameArguments(JSContext* cx,
                    const JS::CallArgs& args,
                    ax::SpriteFrame*& sprite,
                    float& delay,
                    ax::ValueMap& info)
{
    if (args.length() < 2 || args.length() > 3 || !argument(cx, args, 0, sprite) ||
        !argument(cx, args, 1, delay) || (args.length() == 3 && !userInfo(cx, args[2], info)))
        return error(cx, "AnimationFrame requires a SpriteFrame, delay units and optional userInfo dictionary");
    return true;
}
bool create(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::SpriteFrame* sprite{};
    float delay{};
    ax::ValueMap info;
    if (!frameArguments(cx, args, sprite, delay, info))
        return false;
    // Factory autoreleases exactly once. Wrapping does not add another release
    // or an implicit retain, matching the constructor/factory ownership policy.
    return wrapNative(cx, ax::AnimationFrame::create(sprite, delay, info), args.rval(), NativeOwnership::Factory);
}
bool initialize(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* frame = self<ax::AnimationFrame>(cx, args);
    ax::SpriteFrame* sprite{};
    float delay{};
    ax::ValueMap info;
    if (!frame || !frameArguments(cx, args, sprite, delay, info))
        return false;
    args.rval().setBoolean(frame->initWithSpriteFrame(sprite, delay, info));
    return true;
}
bool getUserInfo(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* frame = self<ax::AnimationFrame>(cx, args);
    if (!frame || argc != 0)
        return error(cx, "AnimationFrame.getUserInfo requires a live receiver and no arguments");
    return toJsValue(cx, frame->getUserInfo(), args.rval());
}
bool setUserInfo(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* frame = self<ax::AnimationFrame>(cx, args);
    ax::ValueMap info;
    if (!frame || argc != 1 || !userInfo(cx, args[0], info))
        return error(cx, "AnimationFrame.setUserInfo requires a userInfo dictionary");
    frame->setUserInfo(info);
    args.rval().setUndefined();
    return true;
}
}  // namespace

bool registerLegacyAnimation(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject constructor(cx, getClassObject<ax::AnimationFrame>(cx));
    return constructor && function(cx, constructor, "create", create, 3) &&
           method<ax::AnimationFrame>(cx, "initWithSpriteFrame", initialize, 3) &&
           method<ax::AnimationFrame>(cx, "getUserInfo", getUserInfo) &&
           method<ax::AnimationFrame>(cx, "setUserInfo", setUserInfo, 1);
}
}  // namespace axjs
