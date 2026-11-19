// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include "AppDelegate.h"
#include "js-bindings/runtime/JsEngine.h"
#include "js-bindings/manual/Bindings.h"
#include "js-bindings/runtime/JsRuntime.h"
#include "axmol/ui/Widget.h"
#include "axmol/2d/Layer.h"
#include "axmol/2d/Sprite.h"
#include "axmol/rhi/ProgramState.h"
#include "axmol/renderer/ProgramManager.h"
#include "axmol/base/UserDefault.h"
#include "axmol/platform/Image.h"
#include "axmol/rhi/PixelBufferDesc.h"
#include "axmol/rhi/RenderTarget.h"
#ifndef AX_JS_TEST_NATIVE_DIAGNOSTICS
#define AX_JS_TEST_NATIVE_DIAGNOSTICS 0
#endif
#if AX_JS_TEST_NATIVE_DIAGNOSTICS
#include "tools/js-tests/runtime-audits/legacy-gl/material-texture-diagnostics.h"
#endif
#if AX_JS_TEST_PERFORMANCE_METRICS
#include "tools/js-benchmarks/jsb-comparison/process-stats.h"
#endif
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
#include "axmol/platform/android/jni/JniHelper.h"
#endif

namespace
{
struct V3SuiteLog
{
    std::ofstream output;
    std::vector<std::string> errors;
    std::atomic<unsigned> capturesPending{0};
    std::atomic<bool> captureWriteFailed{false};
    std::mutex captureMutex;
    std::vector<std::pair<std::string, bool>> capturesWritten;
};
std::shared_ptr<V3SuiteLog> v3SuiteLog;
std::shared_ptr<std::function<void(float)>> v3FrameMonitor;
uint64_t v3FrameMonitorGeneration{};

bool registerV3FrameMonitor(JSContext*, JS::HandleObject)
{
    auto* engine = ax::JsEngine::getInstance();
    if (v3FrameMonitor && v3FrameMonitorGeneration != engine->getGeneration())
    {
        // Keep one application-owned stateful monitor across VM generations.
        // JsEngine clears its update callbacks at stop; registration restores
        // this observer in the next VM without reviving any business task.
        auto retained = v3FrameMonitor;
        engine->addUpdateCallback([retained](float dt) { (*retained)(dt); });
        v3FrameMonitorGeneration = engine->getGeneration();
    }
    return true;
}

std::string optionString(const char* name)
{
    if (const char* value = std::getenv(name))
        return value;
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    return ax::JniHelper::callStaticStringMethod("dev/axmol/app/AppActivity", "jsSmokeOption", name);
#else
    return {};
#endif
}

bool v3Record(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string record;
    if (argc != 1 || !axjs::Converter<std::string>::read(cx, args[0], record) || !v3SuiteLog)
    {
        JS_ReportError(cx, "v3Record requires one JSON result string in the full suite");
        return false;
    }
    v3SuiteLog->output << record << '\n';
    v3SuiteLog->output.flush();
    // Dense particle samples stay complete in the private stream. Keep logcat
    // bounded without changing any original test result or capture record.
    if (record.starts_with("{\"kind\":\"original-particle3d\","))
        AXLOGI("V3_JS_TEST_PARTICLE3D_RECORD bytes={}", record.size());
    else if (record.starts_with("{\"kind\":\"pot-hit-native\","))
        AXLOGI("V3_JS_TEST_POT_HIT_RECORD bytes={}", record.size());
    else
        AXLOGI("V3_JS_TEST_RECORD {}", record);
    args.rval().setUndefined();
    return true;
}

bool v3Capture(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string name;
    if (argc != 1 || !axjs::Converter<std::string>::read(cx, args[0], name) || !v3SuiteLog ||
        name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_.") != std::string::npos ||
        name.find("..") != std::string::npos || !name.ends_with(".png"))
    {
        JS_ReportError(cx, "v3Capture requires a simple PNG filename in the full test suite");
        return false;
    }
    args.rval().setBoolean(false);
    auto* renderer = ax::Director::getInstance()->getRenderer();
    ax::rhi::PixelBufferDesc pixels;
    if (!renderer || !renderer->readPixelsSync(renderer->getRenderTarget(), pixels) ||
        pixels._width <= 0 || pixels._height <= 0 ||
        pixels._data.size() != static_cast<size_t>(pixels._width) * pixels._height * 4)
        return true;
    // Native default render targets return top-first data. Convert Vulkan's
    // BGRA swapchain format before PNG encoding; keep the original resolution.
    const auto* target = renderer->getRenderTarget();
    if (ax::rhi::GraphicsCore::isVulkan() && !target->_color.empty() && target->_color[0].texture &&
        target->_color[0].texture->getPixelFormat() == ax::rhi::PixelFormat::BGRA8)
    {
        for (size_t offset = 0; offset < pixels._data.size(); offset += 4)
            std::swap(pixels._data.data()[offset], pixels._data.data()[offset + 2]);
    }
    auto image = std::make_unique<ax::Image>();
    if (!image->initWithRawData(pixels._data.data(), pixels._data.size(),
                                pixels._width, pixels._height, 8))
        return true;
    auto* files = ax::FileUtils::getInstance();
    const std::string directory = files->getWritablePath() + "v3-captures/";
    if (!files->createDirectories(directory))
        return true;
    const std::string path = directory + name;
    auto log = v3SuiteLog;
    ++log->capturesPending;
    // Encoding happens away from the render thread. Completion is recorded
    // by the application monitor, and suite exit waits for these owned writes.
    try
    {
        std::thread([log, image = std::move(image), name = std::move(name), path] {
            const bool saved = image->saveToFile(path, false);
            if (!saved)
                log->captureWriteFailed = true;
            {
                std::lock_guard lock(log->captureMutex);
                log->capturesWritten.emplace_back(name, saved);
            }
            --log->capturesPending;
        }).detach();
    }
    catch (const std::exception&)
    {
        --log->capturesPending;
        log->captureWriteFailed = true;
        return true;
    }
    args.rval().setBoolean(true);
    return true;
}

bool v3ViewState(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* view = ax::Director::getInstance()->getRenderView();
    if (argc != 0 || !view)
    {
        JS_ReportError(cx, "v3ViewState requires the running test RenderView");
        return false;
    }
    JS::RootedObject state(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!state)
        return false;
    auto field = [&](const char* name, const auto& nativeValue) {
        JS::RootedValue value(cx);
        return axjs::toJsValue(cx, nativeValue, &value) &&
            JS_DefineProperty(cx, state, name, value, JSPROP_ENUMERATE);
    };
    if (!field("render_size", view->getRenderSize()) ||
        !field("design_resolution", view->getDesignResolutionSize()) ||
        !field("viewport", view->getViewportRect()) ||
        !field("resolution_policy", static_cast<int>(view->getResolutionPolicy())) ||
        !field("scale", view->getScale()))
        return false;
    args.rval().setObject(*state);
    return true;
}

bool v3Option(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string name;
    if (argc != 1 || !axjs::Converter<std::string>::read(cx, args[0], name))
    {
        JS_ReportError(cx, "v3Option requires one option name");
        return false;
    }
    const std::string value = optionString(name.c_str());
    JS::RootedString text(cx, JS_NewStringCopyN(cx, value.data(), value.size()));
    if (!text)
        return false;
    args.rval().setString(text);
    return true;
}

bool callV3(JSContext* cx, JS::HandleObject global, const char* name, std::string_view text)
{
    JS::RootedValue value(cx);
    JS::RootedValue result(cx);
    return axjs::toJsValue(cx, std::string(text), &value) &&
           JS_CallFunctionName(cx, global, name, JS::HandleValueArray(value), &result);
}

bool requested(const char* name)
{
    const char* value = std::getenv(name);
    if (value && value[0] == '1')
        return true;
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    return ax::JniHelper::callStaticBooleanMethod("dev/axmol/app/AppActivity", "jsSmokeRequested", name);
#else
    return false;
#endif
}

struct WidgetTestAccess : ax::ui::Widget
{
    static void emit(ax::ui::Widget* widget)
    {
        auto method = &WidgetTestAccess::releaseUpEvent;
        (widget->*method)();
    }
};

bool studioEmitClick(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::ui::Widget* widget = nullptr;
    if (argc != 1 || !axjs::toNative(cx, args[0], &widget) || !widget)
    {
        JS_ReportError(cx, "studioEmitClick requires a live native Widget");
        return false;
    }
    WidgetTestAccess::emit(widget);
    args.rval().setUndefined();
    return true;
}

bool gradientQuad(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::LayerGradient* layer = nullptr;
    if (argc != 1 || !axjs::toNative(cx, args[0], &layer) || !layer)
    {
        JS_ReportError(cx, "gradientQuad requires a live native LayerGradient");
        return false;
    }
    const auto& quad = layer->getQuad();
    std::vector<std::vector<unsigned>> colors;
    for (const auto* color : {&quad.bl.color, &quad.br.color, &quad.tl.color, &quad.tr.color})
        colors.push_back({color->r, color->g, color->b, color->a});
    return axjs::toJsValue(cx, colors, args.rval());
}

bool gpuCreateNativeSprite(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Texture2D* texture = nullptr;
    if (argc != 1 || !axjs::toNative(cx, args[0], &texture) || !texture)
    {
        JS_ReportError(cx, "gpuCreateNativeSprite requires a live Texture2D");
        return false;
    }
    // This control deliberately enters the C++ factory without JS preparation.
    auto* sprite = ax::Sprite::createWithTexture(texture);
    if (!sprite)
    {
        JS_ReportError(cx, "Native Sprite texture initialization failed");
        return false;
    }
    return axjs::toJsValue(cx, sprite, args.rval());
}

bool gpuSetNativeSpriteTexture(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Sprite* sprite = nullptr;
    ax::Texture2D* texture = nullptr;
    if (argc != 2 || !axjs::toNative(cx, args[0], &sprite) || !sprite ||
        !axjs::toNative(cx, args[1], &texture) || !texture)
    {
        JS_ReportError(cx, "gpuSetNativeSpriteTexture requires a live Sprite and Texture2D");
        return false;
    }
    sprite->setTexture(texture);
    args.rval().setUndefined();
    return true;
}

bool gpuSpriteProgramType(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Sprite* sprite = nullptr;
    if (argc != 1 || !axjs::toNative(cx, args[0], &sprite) || !sprite || !sprite->getProgramState())
    {
        JS_ReportError(cx, "gpuSpriteProgramType requires an initialized native Sprite");
        return false;
    }
    return axjs::toJsValue(cx, sprite->getProgramState()->getProgram()->getProgramType(), args.rval());
}

bool gpuSetSpriteAutoUpdatePS(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Sprite* sprite = nullptr;
    bool enabled = false;
    if (argc != 2 || !axjs::toNative(cx, args[0], &sprite) || !sprite || !axjs::fromJsValue(cx, args[1], enabled))
    {
        JS_ReportError(cx, "gpuSetSpriteAutoUpdatePS requires a live Sprite and bool");
        return false;
    }
    sprite->setAutoUpdatePS(enabled);
    args.rval().setUndefined();
    return true;
}

bool gpuClearProgramCache(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0)
    {
        JS_ReportError(cx, "gpuClearProgramCache takes no arguments");
        return false;
    }
    ax::ProgramManager::getInstance()->unloadAllPrograms();
    args.rval().setUndefined();
    return true;
}

[[noreturn]] void finish(ax::JsEngine* engine, int code)
{
    engine->stop();
    if (v3SuiteLog) v3SuiteLog->output.flush();
    AXLOGI("JS_TEST_RESULT={}", code);
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    // This test owns a JNI-hosted process. The VM is explicitly cleaned above;
    // global C++ exit destructors must not race Android's live ART threads.
    std::fflush(nullptr);
    std::_Exit(code);
#else
    std::exit(code);
#endif
}
}

void AppDelegate::applicationWillLaunch()
{
    ax::setLogFmtFlag(ax::LogFmtFlag::Full);
    setContextAttrs({.powerPreference = ax::PowerPreference::HighPerformance});
}

bool AppDelegate::applicationDidFinishLaunching()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_MAC
    auto files = ax::FileUtils::getInstance();
    const std::string storageRoot = std::string(AXJS_TEST_WRITABLE_ROOT) + "/";
    if (!files->createDirectories(storageRoot))
        std::exit(1);
    files->setWritablePath(storageRoot);
#endif
    if (const char* customWritable = std::getenv("AXMOL_JS_WRITABLE_PATH"); customWritable && *customWritable)
    {
        std::string wp(customWritable);
        if (!wp.ends_with('/'))
            wp.push_back('/');
        ax::FileUtils::getInstance()->setWritablePath(wp);
    }
    const bool v3Autorun = requested("AXMOL_JS_V3_AUTORUN");
    const bool v3MenuSmoke = requested("AXMOL_JS_V3_MENU_SMOKE");
    const bool v3RestartSmoke = requested("AXMOL_JS_V3_RESTART_SMOKE");
    const bool v3FullSuite = requested("AXMOL_JS_V3_FULL_SUITE") || v3Autorun || v3MenuSmoke || v3RestartSmoke;
    const bool v3Performance = requested("AXMOL_JS_V3_PERFORMANCE");
    const std::string gpuCoverage = optionString("AXMOL_JS_GPU_COVERAGE");
    const bool standaloneMode = requested("AXMOL_JS_BINDING_SMOKE") || requested("AXMOL_JS_NATIVE_SMOKE") ||
        requested("AXMOL_JS_FILE_ASSETS_SMOKE") || !gpuCoverage.empty() ||
        requested("AXMOL_JS_STUDIO_SMOKE") || requested("AXMOL_JS_MATERIAL_SMOKE") ||
        requested("AXMOL_JS_SCALE9_FRAME_PROBE") || requested("AXMOL_JS_BENCHMARK") ||
        requested("AXMOL_JS_LEGACY_HELLO") || requested("AXMOL_JS_SCENE_SMOKE") ||
        requested("AXMOL_JS_AUDIO_SMOKE");
    // A normal application launch opens the complete imported native menu.
    // Acceptance hooks and finite test monitors remain explicitly requested.
    const bool v3Project = v3FullSuite || v3Performance || !standaloneMode;
    const std::string applicationRoot = ax::FileUtils::getInstance()->getDefaultResourceRootPath();
    ax::UserDefault::setFileName(v3Project ? "js-tests-v3.bin" : "js-tests.bin");
    if (v3Project)
    {
        auto files = ax::FileUtils::getInstance();
        const auto engineSearchPaths = files->getSearchPaths();
        const std::string base = applicationRoot + "legacy/js-tests/";
        // Original relative paths such as res/Particle3D/scripts belong to
        // the imported project. Keep absolute application/framework paths
        // available after selecting that actual default resource root.
        files->setDefaultResourceRootPath(base);
        std::vector<std::string> searchPaths{
            base + "res/resjs/", base + "res/", base + "src/", base,
            applicationRoot + "axslc/", applicationRoot};
        // Director/ProgramManager can already have added a platform shader
        // directory. Preserve its resolved path as well as the packaged
        // application axslc directory when selecting the imported project.
        searchPaths.insert(searchPaths.end(), engineSearchPaths.begin(), engineSearchPaths.end());
        files->setSearchPaths(std::move(searchPaths));
        // The selected Spine4.3 runtime needs migrated skeleton JSON. The
        // overlay preserves original JS paths and leaves the source import
        // and original atlases/images available for provenance verification.
        files->addSearchPath(applicationRoot + "legacy-compat/", true);
        AXLOGI("V3_JS_RESOURCE_ROOT={}", files->getDefaultResourceRootPath());
        AXLOGI("V3_JS_RESOURCE_PROBE project={} framework={} ccb={} shader={}",
            files->fullPathForFilename("project.json"), files->fullPathForFilename("script/jsb_boot.js"),
            files->fullPathForFilename("ccb/HelloCocosBuilder.ccbi"),
            files->fullPathForFilename("positionTextureColor_vs"));
    }
    const bool legacyHello = requested("AXMOL_JS_LEGACY_HELLO");
    if (legacyHello)
        ax::FileUtils::getInstance()->addSearchPath("legacy/HelloWorld", true);
    if (!requested("AXMOL_JS_BINDING_SMOKE") && !requested("AXMOL_JS_NATIVE_SMOKE") &&
        !requested("AXMOL_JS_FILE_ASSETS_SMOKE"))
    {
        auto director = ax::Director::getInstance();
        if (!director->getRenderView())
        {
#if AX_TARGET_PLATFORM == AX_PLATFORM_MAC
            auto view = ax::RenderView::createWithRect("JavaScript Tests", ax::Rect(0, 0, 960, 640));
#else
            auto view = ax::RenderView::create("JavaScript Tests");
#endif
            if (!view)
                std::exit(1);
            director->setRenderView(view);
        }
        director->setAnimationInterval(1.0 / 60.0);
    }
    auto engine = ax::JsEngine::getInstance();
    const std::string sidecar = optionString("AXMOL_JS_V3_SIDECAR");
    if (!sidecar.empty() || requested("AXMOL_JS_V3_ASSETS_RESTART") || requested("AXMOL_JS_V3_SYS_RESTART"))
    {
        AXLOGE("Specialized suite observers and observer restart continuations are not bundled in js-tests");
        finish(engine, 69);
    }
#if !AX_JS_TEST_NATIVE_DIAGNOSTICS
    if (gpuCoverage == "materialTexture")
    {
        AXLOGE("GPU materialTexture requires configuring js-tests with -DAX_JS_TEST_NATIVE_DIAGNOSTICS=ON");
        finish(engine, 69);
    }
#endif
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(axjs::registerAll);
#if AX_JS_TEST_PERFORMANCE_METRICS
    engine->addRegisterCallback(registerJsbComparisonProcessStats);
#endif
#if AX_JS_TEST_NATIVE_DIAGNOSTICS
    engine->addRegisterCallback(axjs::tests::registerLegacyMaterialTextureDiagnostics);
#endif
    if (v3FullSuite)
    {
        v3SuiteLog = std::make_shared<V3SuiteLog>();
        const std::string resultPath = ax::FileUtils::getInstance()->getWritablePath() + "v3-suite-results.jsonl";
        v3SuiteLog->output.open(resultPath, std::ios::out | std::ios::trunc);
        if (!v3SuiteLog->output)
            finish(engine, 40);
        AXLOGI("V3_JS_SUITE_REPORT={}", resultPath);
        auto log = v3SuiteLog;
        engine->setErrorHandler([log](std::string_view error) {
            AXLOGE("{}", error);
            log->errors.emplace_back(error);
        });
    }
    if (!engine->start() || !engine->runScript("script/jsb_boot.js"))
        finish(engine, 1);
    ax::FileUtils::getInstance()->addSearchPath("res");

    if (requested("AXMOL_JS_NATIVE_SMOKE"))
    {
        if (!engine->runScript("src/NativeReflection.js") ||
            !engine->evalString("function jsNativeSmokeResult(){return runNativeReflectionSample() ? 0 : 1;}"))
            finish(engine, 8);
        finish(engine, engine->executeGlobalFunction("jsNativeSmokeResult"));
    }

    if (requested("AXMOL_JS_BINDING_SMOKE"))
    {
        if (!engine->runScript("src/BindingSmoke.js") || !engine->runScript("src/LifecycleSmoke.js") ||
            !engine->runScript("src/AssetsSmoke.js"))
            finish(engine, 2);
        finish(engine, engine->executeGlobalFunction("jsBindingSmokeResult"));
    }
    if (requested("AXMOL_JS_FILE_ASSETS_SMOKE"))
    {
        if (!engine->runScript("src/FileAssetsSmoke.js"))
            finish(engine, 55);
        const int result = engine->executeGlobalFunction("runJsFileAssetsSmoke");
        finish(engine, result < 0 ? 55 : result);
    }
    if (!gpuCoverage.empty())
    {
        auto* view = ax::Director::getInstance()->getRenderView();
        const auto& renderSize = view->getRenderSize();
        const std::string configuration =
            "var AXMOL_GPU_PHYSICAL_SIZE={width:" + std::to_string(renderSize.width) +
            ",height:" + std::to_string(renderSize.height) + "};var AXMOL_GPU_RESOLUTION_POLICY=" +
            std::to_string(static_cast<int>(view->getResolutionPolicy())) + ";";
        if (!engine->evalString(configuration) || !engine->runScript("src/GpuCoverageSmoke.js"))
            finish(engine, 65);
        {
            auto cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JSAutoCompartment compartment(cx, global);
            if (gpuCoverage == "spriteTextureFormats" &&
                (!JS_DefineFunction(cx, global, "gpuCreateNativeSprite", gpuCreateNativeSprite, 1, 0) ||
                 !JS_DefineFunction(cx, global, "gpuSetNativeSpriteTexture", gpuSetNativeSpriteTexture, 2, 0) ||
                 !JS_DefineFunction(cx, global, "gpuSpriteProgramType", gpuSpriteProgramType, 1, 0) ||
                 !JS_DefineFunction(cx, global, "gpuSetSpriteAutoUpdatePS", gpuSetSpriteAutoUpdatePS, 2, 0) ||
                 !JS_DefineFunction(cx, global, "gpuClearProgramCache", gpuClearProgramCache, 0, 0)))
                finish(engine, 65);
            if (!callV3(cx, global, "jsGpuCoverageStart", gpuCoverage))
                finish(engine, 65);
        }
        engine->addUpdateCallback([engine, elapsed = 0.0F](float dt) mutable {
            elapsed += dt;
            const int result = engine->executeGlobalFunction("jsGpuCoverageResult");
            if (result != 1)
                finish(engine, result < 0 ? 65 : result);
            if (elapsed > 30.0F)
                finish(engine, 67);
        });
        return true;
    }
    if (requested("AXMOL_JS_STUDIO_SMOKE"))
    {
        {
            auto cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JSAutoCompartment compartment(cx, global);
            if (!JS_DefineFunction(cx, global, "studioEmitClick", studioEmitClick, 1, 0) ||
                !JS_DefineFunction(cx, global, "studioGradientQuad", gradientQuad, 1, 0))
                finish(engine, 6);
        }
        if (!engine->runScript("studio/StudioSmoke.js"))
            finish(engine, 6);
        finish(engine, engine->executeGlobalFunction("jsStudioSmokeResult"));
    }
    if (requested("AXMOL_JS_MATERIAL_SMOKE"))
    {
        if (!engine->runScript("src/MaterialSmoke.js") ||
            engine->executeGlobalFunction("jsMaterialSmokeStart") != 0)
            finish(engine, 60);
        engine->addUpdateCallback([engine, elapsed = 0.0F](float dt) mutable {
            elapsed += dt;
            const int result = engine->executeGlobalFunction("jsMaterialSmokeResult");
            if (result != 1)
                finish(engine, result < 0 ? 60 : result);
            if (elapsed > 120.0F)
                finish(engine, 62);
        });
        return true;
    }
    if (requested("AXMOL_JS_SCALE9_FRAME_PROBE"))
    {
        // Diagnostic output reports initialization/validity for original
        // NinePatch resources. Exit 0 only means the probe was collected.
        const bool collected = engine->runScript("src/Scale9FrameSmoke.js") &&
            engine->evalString("axmolScale9FrameProbe();");
        finish(engine, collected ? 0 : 54);
    }

#if AX_ENABLE_JS_DEBUGGER
    if (requested("AXMOL_JS_DEBUG") && !engine->enableDebugger(5086, requested("AXMOL_JS_DEBUG_WAIT")))
        finish(engine, 3);
#endif
    if (v3FullSuite)
    {
        {
            auto cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JSAutoCompartment compartment(cx, global);
            if (!JS_DefineFunction(cx, global, "v3Record", v3Record, 1, 0) ||
                !JS_DefineFunction(cx, global, "v3Capture", v3Capture, 1, 0) ||
                !JS_DefineFunction(cx, global, "v3ViewState", v3ViewState, 0, 0) ||
                !JS_DefineFunction(cx, global, "v3GradientQuad", gradientQuad, 1, 0) ||
                !JS_DefineFunction(cx, global, "v3Option", v3Option, 1, 0) ||
                !engine->runScript("src/V3NestedMenus.js") ||
                !engine->runScript("src/V3RegistryNavigation.js") ||
                !engine->runScript("src/V3FullSuiteHarness.js") ||
                !callV3(cx, global, "jsV3Configure", optionString("AXMOL_JS_V3_CATEGORY")) ||
                engine->executeGlobalFunction("jsV3InstallLoaderHooks") != 0)
                finish(engine, 41);
            if (requested("AXMOL_JS_V3_CAPTURE") &&
                !engine->evalString("V3SuiteState.captureEnabled = true;"))
                finish(engine, 41);
        }
    }
    if (!engine->runScript(v3Project ? "legacy/js-tests/main.js" : "main.js"))
    {
        if (v3FullSuite)
        {
            auto cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JSAutoCompartment compartment(cx, global);
            for (const auto& error : v3SuiteLog->errors)
                callV3(cx, global, "jsV3HandleException", error);
        }
        finish(engine, 4);
    }
    if (v3FullSuite)
    {
        {
            auto* view = ax::Director::getInstance()->getRenderView();
            const auto& physicalSize = view->getRenderSize();
            const std::string physicalConfiguration =
                "var AXMOL_V3_PHYSICAL_SIZE={width:" + std::to_string(physicalSize.width) +
                ",height:" + std::to_string(physicalSize.height) + "};";
            auto cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JSAutoCompartment compartment(cx, global);
            if (!engine->evalString(physicalConfiguration) ||
                !callV3(cx, global, "jsV3ApplyProfile", optionString("AXMOL_JS_V3_PROFILE")))
                finish(engine, 42);
        }
        if (v3Autorun && engine->executeGlobalFunction("jsV3PrepareAutorun") != 0)
            finish(engine, 42);
        {
            auto log = v3SuiteLog;
            auto monitor = [engine, log, v3Autorun, v3MenuSmoke, v3RestartSmoke, applicationRoot,
                 generation = engine->getGeneration(), elapsed = 0.0F](float dt) mutable {
                elapsed += dt;
                if (elapsed < 0.5F || !engine->isRunning())
                    return;
                auto cx = engine->getContext();
                JS::RootedObject global(cx, engine->getGlobalObject());
                JSAutoCompartment compartment(cx, global);
                if (engine->getGeneration() != generation)
                {
                    const std::string record = "{\"kind\":\"vm-restart\",\"old_generation\":" +
                        std::to_string(generation) + ",\"new_generation\":" +
                        std::to_string(engine->getGeneration()) + "}";
                    log->output << record << '\n';
                    log->output.flush();
                    AXLOGI("V3_JS_TEST_RECORD {}", record);
                    generation = engine->getGeneration();
                    if (v3Autorun)
                    {
                        // An unexpected restart during autorun cannot resume an omitted observer.
                        finish(engine, 51);
                    }
                    else
                    {
                        // The separate Sys restart smoke check rebuilds its
                        // menu recorder. Other unexpected restarts still fail.
                        if (!JS_DefineFunction(cx, global, "v3Record", v3Record, 1, 0) ||
                            !JS_DefineFunction(cx, global, "v3ViewState", v3ViewState, 0, 0) ||
                            !JS_DefineFunction(cx, global, "v3Option", v3Option, 1, 0) ||
                            !engine->runScript(applicationRoot + "src/V3NestedMenus.js") ||
                            !engine->runScript(applicationRoot + "src/V3RegistryNavigation.js") ||
                            !engine->runScript(applicationRoot + "src/V3FullSuiteHarness.js") ||
                            !callV3(cx, global, "jsV3Configure", optionString("AXMOL_JS_V3_CATEGORY")) ||
                            !engine->evalString("V3SuiteState.restartCompleted = true;"))
                            finish(engine, 50);
                    }
                }
                auto errors = std::move(log->errors);
                log->errors.clear();
                {
                    std::lock_guard lock(log->captureMutex);
                    for (const auto& [name, saved] : log->capturesWritten)
                    {
                        const std::string record = "{\"kind\":\"capture-written\",\"file\":\"v3-captures/" +
                            name + "\",\"success\":" + (saved ? "true}" : "false}");
                        log->output << record << '\n';
                        AXLOGI("V3_JS_TEST_RECORD {}", record);
                    }
                    if (!log->capturesWritten.empty())
                        log->output.flush();
                    log->capturesWritten.clear();
                }
                for (const auto& error : errors)
                    if (!callV3(cx, global, "jsV3HandleException", error))
                        finish(engine, 43);
                if (!v3Autorun && !v3MenuSmoke && !v3RestartSmoke)
                    return;
                const int result = engine->executeGlobalFunction(v3Autorun ? "jsV3AutorunTick" :
                    v3RestartSmoke ? "jsV3RestartSmokeTick" : "jsV3MenuSmokeResult");
                bool capturesFlushed;
                {
                    std::lock_guard lock(log->captureMutex);
                    capturesFlushed = log->capturesPending == 0 && log->capturesWritten.empty();
                }
                if (result != 1 && capturesFlushed)
                    finish(engine, log->captureWriteFailed ? 53 : result < 0 ? 43 : result);
                if (elapsed > (v3Autorun ? 1800.0F : 90.0F))
                    finish(engine, 44);
            };
            // All test monitoring belongs outside the business Scheduler:
            // global cancellation/time scaling must remain observable. The
            // native registration callback reattaches it after VM restart.
            v3FrameMonitor = std::make_shared<std::function<void(float)>>(std::move(monitor));
            v3FrameMonitorGeneration = 0;
            engine->addRegisterCallback(registerV3FrameMonitor);
            {
                JS::RootedObject global(engine->getContext(), engine->getGlobalObject());
                registerV3FrameMonitor(engine->getContext(), global);
            }
            if (!engine->evalString(
                    "v3Record(JSON.stringify({kind:'native-monitor',category:V3SuiteState.filter,"
                    "sidecar:V3SuiteState.sidecar,entry:'script-update-frame',"
                    "business_scheduler_entry_registered:false,retained_across_vm_restart:true,"
                    "validation_variant:V3SuiteState.validationVariant,validation_revision:V3SuiteState.validationRevision}));"))
                finish(engine, 41);
        }
        return true;
    }
    if (v3Performance)
    {
        if (!engine->runScript("src/JsbComparisonBenchmark.js") ||
            !engine->evalString("JsbComparisonBenchmark.startIfRequested();"))
            finish(engine, 7);
        return true;
    }
    if (requested("AXMOL_JS_BENCHMARK"))
    {
        if (!engine->runScript("src/SceneBenchmark.js") ||
            !engine->evalString("function jsSceneBenchmarkDone(){return AxmolBenchmarkResult ? 0 : 1;} "
                                "cc.director.runScene(new AxmolBenchmarkScene());"))
            finish(engine, 7);
        engine->addUpdateCallback([engine, elapsed = 0.0F](float dt) mutable {
            elapsed += dt;
            const int result = engine->executeGlobalFunction("jsSceneBenchmarkDone");
            if (result == 0)
                finish(engine, 0);
            if (result < 0 || elapsed > 120.0F)
                finish(engine, 7);
        });
        return true;
    }
    if (legacyHello && !engine->runScript("src/LegacySmoke.js"))
        finish(engine, 5);
    const bool sceneSmoke = requested("AXMOL_JS_SCENE_SMOKE");
    const bool audioSmoke = sceneSmoke || requested("AXMOL_JS_AUDIO_SMOKE");
    if (audioSmoke && (!engine->runScript("src/AudioSmoke.js") || !engine->evalString("startJsAudioSmoke();")))
        finish(engine, 31);
    if (sceneSmoke || audioSmoke)
    {
        engine->addUpdateCallback([engine, legacyHello, sceneSmoke, audioSmoke, elapsed = 0.0F,
                                   sceneResult = -1](float dt) mutable {
            elapsed += dt;
            if (elapsed < 1.0F)
                return;
            if (sceneResult < 0)
                sceneResult = sceneSmoke
                                  ? engine->executeGlobalFunction(legacyHello ? "jsLegacyHelloResult" : "jsSceneSmokeResult")
                                  : 0;
            if (sceneResult != 0)
                finish(engine, sceneResult);
            if (audioSmoke)
            {
                const int result = engine->executeGlobalFunction("jsAudioSmokeResult");
                if (result == 32 && elapsed < 5.0F)
                    return;
                finish(engine, result);
            }
            finish(engine, 0);
        });
    }
    return true;
}

void AppDelegate::applicationDidEnterBackground() { ax::Director::getInstance()->deactivate(); }
void AppDelegate::applicationWillEnterForeground() { ax::Director::getInstance()->activate(); }
void AppDelegate::applicationWillQuit() { ax::JsEngine::getInstance()->stop(); }

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
extern "C" JNIEXPORT jlong JNICALL
Java_org_cocos2dx_js_1tests_AppActivity_nativeGeneration(JNIEnv*, jclass)
{
    return static_cast<jlong>(ax::JsEngine::getInstance()->getGeneration());
}

extern "C" JNIEXPORT void JNICALL
Java_org_cocos2dx_js_1tests_AppActivity_nativeEvalString(JNIEnv*, jclass, jlong generation, jstring source)
{
    auto* engine = ax::JsEngine::getInstance();
    if (engine->isRunning() && engine->getGeneration() == static_cast<uint64_t>(generation))
        engine->evalString(ax::JniHelper::jstring2string(source), "original-native-reflection/java-callback.js");
}
#endif
