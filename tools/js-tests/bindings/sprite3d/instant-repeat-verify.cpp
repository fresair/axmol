// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Extracts the original NormalMapping callback recipe unchanged, drives real
// native actions without a GPU and checks duration/clone/reverse/VM ownership.
#include "manual/Bindings.h"
#include "runtime/JsEngine.h"
#include "runtime/JsRuntime.h"
#include "axmol/2d/ActionInterval.h"
#include "axmol/2d/ActionInstant.h"
#include "axmol/2d/ActionManager.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/UserDefault.h"
#include "axmol/base/RefPtr.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/base/Director.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
unsigned checks{}, failures{}, lateCalls{};
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// This command-line fixture has no JVM or APK AssetManager. Read the pushed
// original script and bootstrap through FileUtils' actual POSIX file streams.
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(std::string root)
    {
        _defaultResRootPath = std::move(root);
        _writablePath = _defaultResRootPath;
        if (!init()) throw std::runtime_error("Native action filesystem init failed");
    }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat status{};
        return stat(std::string(path).c_str(), &status) == 0 && S_ISREG(status.st_mode);
    }
};
#endif
bool late(JSContext*, unsigned argc, JS::Value* vp)
{
    ++lateCalls;
    JS::CallArgsFromVp(argc, vp).rval().setUndefined();
    return true;
}
bool registration(JSContext* context, JS::HandleObject global)
{
    return axjs::registerAll(context, global) && JS_DefineFunction(context, global, "auditLateCall", late, 0, 0);
}
void require(bool ok, const std::string& name)
{
    ++checks;
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
}
template<class T>
ax::RefPtr<T> member(ax::JsEngine* engine, const char* property)
{
    auto* context = engine->getContext();
    JS::RootedObject global(context, engine->getGlobalObject());
    JSAutoCompartment compartment(context, global);
    JS::RootedValue audit(context), value(context);
    T* native{};
    if (JS_GetProperty(context, global, "normalAudit", &audit) && audit.isObject())
    {
        JS::RootedObject object(context, &audit.toObject());
        if (JS_GetProperty(context, object, property, &value) && axjs::toNative(context, value, &native) && native)
            return ax::RefPtr<T>(native);
    }
    return {};
}
double number(ax::JsEngine* engine, const char* expression)
{
    auto* context = engine->getContext();
    JS::RootedObject global(context, engine->getGlobalObject());
    JSAutoCompartment compartment(context, global);
    JS::RootedValue result(context);
    if (!engine->evalString(expression, &result) || !result.isNumber()) return -1;
    return result.toNumber();
}
void tick(unsigned count, ax::AutoreleasePool& pool)
{
    for (unsigned frame = 0; frame < count; ++frame)
    {
        ax::Director::getInstance()->getActionManager()->update(1.0f / 60.0f);
        pool.clear();
    }
}
void nativeControl(bool epsilon)
{
    ax::AutoreleasePool pool("Pure C++ zero-duration control");
    auto* rawTarget = new ax::Node;
    auto target = ax::RefPtr<ax::Node>(rawTarget);
    rawTarget->release();
    unsigned called{};
    ax::Vector<ax::FiniteTimeAction*> actions;
    actions.pushBack(ax::CallFunc::create([&called] { ++called; }));
    auto* sequence = ax::Sequence::create(actions);
    if (epsilon) sequence->setDuration(MATH_EPSILON);
    auto* repeated = ax::RepeatForever::create(sequence);
    ax::ActionManager manager;
    manager.addAction(repeated, target, false);
    for (unsigned frame = 0; frame < 60; ++frame) manager.update(1.0f / 60.0f);
    std::cout << "NATIVE_CONTROL epsilon=" << epsilon << " duration=" << sequence->getDuration()
              << " callbacks=" << called << '\n';
    require(epsilon ? called >= 59 : called == 1 && sequence->getDuration() == 0,
            epsilon ? "Explicit old epsilon permits native repetition" : "Modern C++ zero-duration semantics remain unchanged");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto root = std::filesystem::absolute(argv[1]);
    std::ifstream original(root / "tests/js-runtime-tests/fixtures/v3/src/Sprite3DTest/Sprite3DTest.js");
    const std::string source((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
    auto first = source.find("var angleReverseDir = cc.callFunc", source.find("var Sprite3DNormalMappingTest"));
    auto end = source.find("_light.runAction(cc.repeatForever(seq1));", first);
    if (first == std::string::npos || end == std::string::npos) return 2;
    end += std::string_view("_light.runAction(cc.repeatForever(seq1));").size();
    const auto recipe = source.substr(first, end - first);
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    std::string pattern = "/data/local/tmp/axmol-instant-repeat-XXXXXX";
#else
    std::string pattern = (std::filesystem::temp_directory_path() / "axmol-instant-repeat-XXXXXX").string();
#endif
    std::vector<char> storage(pattern.begin(), pattern.end()); storage.push_back(0);
    const auto* dir = mkdtemp(storage.data());
    if (!dir) return 2;
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(std::string(dir) + "/"));
#endif
    auto* files = ax::FileUtils::getInstance();
    files->setWritablePath(std::string(dir) + "/"); ax::UserDefault::setFileName("instant-repeat.bin");
    files->addSearchPath((root / "extensions/scripting/js-bindings").string(), true);
    nativeControl(false); nativeControl(true);
    auto* engine = ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(registration);
    engine->setErrorHandler([](std::string_view error) { std::cerr << error << '\n'; });
    ax::RefPtr<ax::CallFunc> previousCallback;
    for (unsigned generation = 0; generation < 2; ++generation)
    {
        ax::AutoreleasePool pool("Legacy JS zero-duration VM");
        require(engine->start() && engine->runScript("script/jsb_boot.js"), "Real native bindings/bootstrap VM " + std::to_string(generation));
        if (!engine->getContext()) break;
        const auto beforeOld = lateCalls;
        if (previousCallback) previousCallback->execute();
        require(lateCalls == beforeOld, "Previous VM callback cannot run in new VM");
        const std::string setup =
            "var normalAudit={layer:new cc.Node()};(function(){this._angle=0;this._reverseDir=false;this._radius=100;this._TAG_GROSSINI=10;"
            "_light=new jsb.PointLight(cc.math.vec3(0,0,0),cc.color(255,255,255),1000);this.addChild(_light,1,10);" + recipe +
            "normalAudit.inner=seq1;normalAudit.target=_light;this.onEnter();}).call(normalAudit.layer);";
        require(engine->evalString(setup), "Unchanged original NormalMapping callback/Sequence/RepeatForever recipe");
        auto layer = member<ax::Node>(engine, "layer");
        auto target = member<ax::Node>(engine, "target");
        auto inner = member<ax::Sequence>(engine, "inner");
        require(layer && target && inner, "Actual native layer, PointLight and Sequence receivers");
        if (!layer || !target || !inner) { engine->stop(); break; }
        tick(60, pool);
        const auto angle = number(engine, "normalAudit.layer._angle");
        std::cout << "ORIGINAL_RECIPE vm=" << generation << " duration=" << inner->getDuration() << " angle=" << angle
                  << " nativePosition=" << target->getPosition3D().x << ',' << target->getPosition3D().z
                  << " runningActions=" << target->getNumberOfRunningActions() << '\n';
        require(inner->getDuration() == MATH_EPSILON, "Legacy single-instant Sequence returns original epsilon duration");
        require(inner->getReferenceCount() == 2, "Factory autoreleases once; only native repeat and explicit observer own Sequence");
        require(angle >= .59 && angle <= .62, "Original light callback advances through sixty real ActionManager ticks");
        require(std::abs(target->getPosition3D().x - 100 * std::cos(angle)) < .001 &&
                std::abs(target->getPosition3D().z - 100 * std::sin(angle)) < .001,
                "Native PointLight follows unchanged original callback orbit");
        target->stopAllActions();
        for (const char* transformation : {"source", "source.clone()", "source.reverse()", "cc.repeat(source,3).clone()", "cc.repeat(source,3).reverse()"})
        {
            const std::string base =
                "normalAudit.count=0;(function(){var source=cc.sequence(cc.callFunc(function(){normalAudit.count++;}));var selected=";
            const auto selection = std::string(transformation);
            const auto tail = selection.starts_with("cc.repeat") ?
                ";normalAudit.selected=selected;normalAudit.target.runAction(selected);})();" :
                ";normalAudit.selected=selected;normalAudit.target.runAction(cc.repeat(selected,3));})();";
            require(engine->evalString(base + selection + tail), "Real finite repeat setup " + selection);
            auto selected = member<ax::ActionInterval>(engine, "selected");
            require(bool(selected), "Finite selected native interval can be held for observation " + selection);
            tick(10, pool);
            const auto count = number(engine, "normalAudit.count");
            std::cout << "FINITE_REPEAT vm=" << generation << " path=" << selection << " callbacks=" << count
                      << " duration=" << number(engine, "normalAudit.selected.getDuration()") << '\n';
            require(count == 3, "Finite repeat executes exactly three callbacks " + selection);
            require(number(engine, "normalAudit.selected.getDuration()") > 0, "Clone/reverse keeps native legacy duration " + selection);
            target->stopAllActions();
        }
        require(engine->evalString(
            "(function(){var interval=cc.ActionInterval.__allocate();if(!interval.initWithDuration(0)||interval.getDuration()!==Math.fround(.000001))throw Error('direct interval epsilon');"
            "var array=cc.Sequence.create([cc.callFunc(function(){})]);if(array.getDuration()!==Math.fround(.000001))throw Error('static sequence epsilon');"
            "var ctor=new cc.Sequence(cc.callFunc(function(){}));if(ctor.getDuration()<=0)throw Error('constructor sequence epsilon');"
            "var pair=cc.Sequence.createWithTwoActions(cc.callFunc(function(){}),cc.callFunc(function(){}));if(pair.getDuration()<=0)throw Error('two-action sequence epsilon');"
            "var rejected=false;try{cc.Sequence.createWithTwoActions(null,cc.callFunc(function(){}));}catch(e){rejected=true;}if(!rejected)throw Error('null pair action accepted');})()"),
            "Direct interval, static, constructor and two-action legacy duration paths");
        require(engine->evalString(
            "(function(){var source=cc.spawn(cc.callFunc(function(){}));if(source.getDuration()!==Math.fround(.000001))throw Error('spawn epsilon');"
            "if(!(source.clone() instanceof cc.Spawn)||source.clone().getDuration()!==Math.fround(.000001)||source.reverse().getDuration()!==Math.fround(.000001))throw Error('spawn clone/reverse');"
            "var ctor=new cc.Spawn(cc.callFunc(function(){}));if(ctor.getDuration()<=0)throw Error('spawn constructor');"
            "var explicitZero=cc.sequence(cc.callFunc(function(){}));explicitZero.setDuration(0);if(explicitZero.getDuration()!==0)throw Error('explicit setter semantics');})()"),
            "Spawn clone/constructor and explicit duration setter contracts");
        require(engine->evalString(
            "normalAudit.times=[];var Tiny=cc.ActionInterval.extend({ctor:function(){this.initWithDuration(0);},update:function(t){normalAudit.times.push(t);}});"
            "normalAudit.tiny=new Tiny();normalAudit.target.runAction(normalAudit.tiny);"), "Real zero-duration JS custom interval created");
        auto tiny = member<ax::ActionInterval>(engine, "tiny");
        require(bool(tiny), "Custom interval has an actual native allocation");
        tick(2, pool);
        require(engine->evalString("if(normalAudit.times.length!==1||normalAudit.times[0]!==1||!normalAudit.tiny.isDone())throw Error('old epsilon first-step custom action');"),
                "Custom update receives original normalized first step and completes");
        require(engine->evalString("normalAudit.late=cc.callFunc(function(){auditLateCall();});"), "Late native callback created");
        auto lateCallback = member<ax::CallFunc>(engine, "late");
        require(bool(lateCallback), "Late callback has a real native CallFunc");
        pool.clear();
        require(lateCallback && lateCallback->getReferenceCount() == 1, "Factory callback has only its explicit native owner after frame pool clear");
        const auto beforeLive = lateCalls;
        if (lateCallback) lateCallback->execute();
        require(lateCalls == beforeLive + 1, "Callback runs in its live owner VM");
        layer->onExit(); target->stopAllActions(); engine->stop();
        const auto beforeStopped = lateCalls;
        if (lateCallback) lateCallback->execute();
        require(lateCalls == beforeStopped, "Stopped VM callback does not execute");
        previousCallback = std::move(lateCallback);
        require(ax::rhi::GraphicsCore::device() == nullptr, "Native action verification initializes no graphics device");
    }
    previousCallback = nullptr;
    ax::ScriptEngineManager::getInstance()->removeScriptEngine();
    ax::UserDefault::destroyInstance();
    std::error_code error; std::filesystem::remove_all(dir, error);
    std::cout << "LEGACY_INSTANT_REPEAT_RESULT checks=" << checks << " failures=" << failures << " vmLifetimes=2\n";
    return failures ? 65 : 0;
}
