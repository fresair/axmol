/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include "generated/axjs_audio_gen.h"
#include "manual/LegacyAudioBindings.h"
#include "axmol/audio/AudioEngine.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/platform/FileUtils.h"
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

namespace
{
unsigned checks{};
void check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}

// Seed the actual native containers without opening an audio device. This
// exercises storage and wrapper lifetimes, not play/decoder/output behavior;
// the original APK AudioProfileTest covers the real playback route separately.
struct ProfileFixture : ax::AudioEngine
{
    static ax::AudioProfile* add(const std::string& name)
    {
        auto& helper = _audioPathProfileHelperMap[name];
        helper.profile.name = name;
        helper.profile.maxInstances = 3;
        helper.profile.minDelay = 1;
        return &helper.profile;
    }
    static void attach(int id, const std::string& name)
    {
        _audioIDInfoMap.try_emplace(id).first->second.profileHelper = &_audioPathProfileHelperMap.at(name);
    }
    static void nullProfile(int id) { _audioIDInfoMap.try_emplace(id); }
    static void detach(int id) { _audioIDInfoMap.erase(id); }
};

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils
{
public:
    NativeFiles() { _defaultResRootPath = _writablePath = "/data/local/tmp/"; check(init(), "POSIX files init"); }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat value{};
        return stat(std::string(path).c_str(), &value) == 0 && S_ISREG(value.st_mode);
    }
};
#endif

bool assertion(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ++checks;
    if (argc != 1 || !JS::ToBoolean(args[0]))
    {
        JS_ReportError(cx, "Audio profile regression assertion failed");
        return false;
    }
    args.rval().setUndefined();
    return true;
}
bool registration(JSContext* cx, JS::HandleObject global)
{
    return axjs::register_all_ax_audio(cx, global) && axjs::registerLegacyAudio(cx, global) &&
           JS_DefineFunction(cx, global, "assertProfile", assertion, 1, 0);
}
void eval(ax::JsEngine* vm, std::string_view script)
{
    check(vm->evalString(script), "actual SpiderMonkey audio-profile script failed");
}
}

int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles());
#endif
    ax::AutoreleasePool pool("Audio profile lifetime regression");
    auto* vm = ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(vm);
    vm->addRegisterCallback(registration);
    vm->setErrorHandler([](std::string_view error) { std::cerr << error.substr(0, 600) << '\n'; });
    try
    {
        check(vm->start(), "start real SpiderMonkey");
        ProfileFixture::nullProfile(4001);
        eval(vm, R"JS(
            var audio = cc.AudioEngine;
            function rejects(fn) { var caught=false; try { fn(); } catch(e) { caught=true; } assertProfile(caught); }
            assertProfile(audio.getProfile(4001) === null);
            assertProfile(audio.getProfile(-1) === null);
            assertProfile(audio.getProfile('missing-profile') === null);
            [NaN, Infinity, -Infinity, 1.5, 2147483648, {}, null, true].forEach(function(v) {
                rejects(function() { audio.getProfile(v); });
            });
            rejects(function() { audio.getProfile(); });
            rejects(function() { audio.getDefaultProfile(1); });
            var defaultProfile = audio.getDefaultProfile();
            assertProfile(defaultProfile instanceof cc.AudioProfile);
            assertProfile(defaultProfile === audio.getDefaultProfile());
            defaultProfile.maxInstances = 7;
            defaultProfile.minDelay = .25;
        )JS");
        check(ax::AudioEngine::getDefaultProfile()->maxInstances == 7, "default setter changes actual native");
        check(ax::AudioEngine::getDefaultProfile()->minDelay == .25f, "default delay changes actual native");
        auto* first = ProfileFixture::add("fixture-first");
        ProfileFixture::attach(4002, "fixture-first");
        const auto generation = ax::AudioEngine::getProfileGeneration();
        eval(vm, R"JS(
            var named = audio.getProfile('fixture-first');
            assertProfile(named === audio.getProfile(4002));
            assertProfile(named instanceof cc.AudioProfile);
            named.maxInstances = 5;
            named.minDelay = .5;
            named.name = 'changed-field-only';
            assertProfile(named === audio.getProfile('fixture-first'));
            assertProfile(audio.getProfile('changed-field-only') === null);
            var owned = new cc.AudioProfile(); owned.name='owned-input'; owned.maxInstances=2;
        )JS");
        check(first->name == "changed-field-only" && first->maxInstances == 5 && first->minDelay == .5f,
              "named setters mutate actual fields without reindexing native key");
        for (unsigned i = 0; i != 8192; ++i) ProfileFixture::add("fixture-growth-" + std::to_string(i));
        check(first == ax::AudioEngine::getProfile("fixture-first"), "node address survives extensive native insertion");
        check(first == ax::AudioEngine::getProfile(4002), "AudioInfo helper remains valid after native insertion");
        check(ax::AudioEngine::isProfileValid(first, generation), "original lease remains native-valid after insertion");
        vm->garbageCollect();
        eval(vm, "assertProfile(named === audio.getProfile(4002)); assertProfile(named.maxInstances===5); assertProfile(owned.maxInstances===2);");
        ProfileFixture::detach(4001);
        ProfileFixture::detach(4002);
        eval(vm, "assertProfile(audio.getProfile(4002)===null); assertProfile(named.maxInstances===5);");
        ax::AudioEngine::stopAll();
        eval(vm, "assertProfile(named===audio.getProfile('fixture-first')); assertProfile(defaultProfile.maxInstances===7);");
        ax::AudioEngine::end();
        check(!ax::AudioEngine::isProfileValid(first, generation), "end invalidates original native epoch");
        eval(vm, R"JS(
            rejects(function() { return named.maxInstances; });
            rejects(function() { defaultProfile.maxInstances=11; });
            var nextNamed=audio.getProfile('fixture-first'), nextDefault=audio.getDefaultProfile();
            assertProfile(nextNamed!==named); assertProfile(nextDefault!==defaultProfile);
            assertProfile(nextNamed.maxInstances===5);
            rejects(function() { return named.name; });
            rejects(function() { return defaultProfile.name; });
            assertProfile(owned.name==='owned-input');
        )JS");
        check(first == ax::AudioEngine::getProfile("fixture-first"), "same retained native address cannot revive old wrapper");
        vm->garbageCollect();
        eval(vm, "assertProfile(nextNamed.name==='changed-field-only'); assertProfile(nextDefault===audio.getDefaultProfile());");
        vm->stop();
        check(vm->start(), "restart actual VM with borrowed profiles still native-live");
        eval(vm, "var audio=cc.AudioEngine; assertProfile(audio.getProfile('fixture-first').maxInstances===5); assertProfile(audio.getDefaultProfile().minDelay===0);");
        vm->stop();
        ax::AudioEngine::end();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        std::cout << "AUDIO_PROFILE_LIFETIME_PASS checks=" << checks
                  << " scope=native-storage-and-real-JS-lifetime,no-playback-output\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "AUDIO_PROFILE_LIFETIME_FAIL " << error.what() << '\n';
        vm->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        return 1;
    }
}
