/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyAudioBindings.h"
#include "runtime/JsRuntime.h"
#if defined(AX_ENABLE_AUDIO) && (AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID || AX_TARGET_PLATFORM == AX_PLATFORM_IOS || \
    AX_TARGET_PLATFORM == AX_PLATFORM_MAC || defined(_WIN32) || AX_TARGET_PLATFORM == AX_PLATFORM_LINUX || \
    AX_TARGET_PLATFORM == AX_PLATFORM_WASM)
#include "axmol/audio/AudioEngine.h"

namespace axjs
{
namespace
{
bool profileResult(JSContext* cx, ax::AudioProfile* profile, JS::MutableHandleValue result)
{
    if (!profile)
    {
        result.setNull();
        return true;
    }
    const auto generation = ax::AudioEngine::getProfileGeneration();
    // Retain the actual native profile and its writable field semantics. The
    // native map uses stable nodes; end() invalidates the generation before
    // releasing default storage. Runtime also checks its own VM generation.
    LifetimeToken lease([profile, generation] {
        return ax::AudioEngine::isProfileValid(profile, generation);
    });
    return wrapBorrowed(cx, profile, result, lease);
}

bool getProfile(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc == 1 && args[0].isString())
    {
        std::string name;
        if (!fromJsValue(cx, args[0], name))
            return false;
        return profileResult(cx, ax::AudioEngine::getProfile(name), args.rval());
    }
    if (argc == 1 && args[0].isNumber())
    {
        int id{};
        if (!fromJsValue(cx, args[0], id))
        {
            if (!JS_IsExceptionPending(cx))
                JS_ReportError(cx, "AudioEngine.getProfile audio ID must be a finite 32-bit integer");
            return false;
        }
        return profileResult(cx, ax::AudioEngine::getProfile(id), args.rval());
    }
    JS_ReportError(cx, "AudioEngine.getProfile requires an audio ID or profile name");
    return false;
}

bool getDefaultProfile(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc)
    {
        JS_ReportError(cx, "AudioEngine.getDefaultProfile takes no arguments");
        return false;
    }
    return profileResult(cx, ax::AudioEngine::getDefaultProfile(), args.rval());
}
}

bool registerLegacyAudio(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject audio(cx, detail::getClassObjectInfo(cx, typeid(ax::AudioEngine)));
    if (!audio)
        return false;
    return JS_DefineFunction(cx, audio, "getProfile", getProfile, 1, JSPROP_ENUMERATE) &&
           JS_DefineFunction(cx, audio, "getDefaultProfile", getDefaultProfile, 0, JSPROP_ENUMERATE);
}
}
#else
namespace axjs
{
bool registerLegacyAudio(JSContext*, JS::HandleObject) { return true; }
}
#endif
