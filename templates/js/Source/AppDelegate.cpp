// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT

#include "AppDelegate.h"
#include "js-bindings/runtime/JsEngine.h"
#include "js-bindings/manual/Bindings.h"
#if AX_ENABLE_AUDIO
#    include "axmol/audio/AudioEngine.h"
#endif
#include <cstdlib>

AppDelegate::~AppDelegate() = default;

void AppDelegate::applicationWillLaunch()
{
    ax::setLogFmtFlag(ax::LogFmtFlag::Full);
    setContextAttrs({.powerPreference = ax::PowerPreference::HighPerformance});
}

bool AppDelegate::applicationDidFinishLaunching()
{
    auto director = ax::Director::getInstance();
    if (!director->getRenderView())
    {
#if AX_TARGET_PLATFORM == AX_PLATFORM_MAC
        auto view = ax::RenderView::createWithRect("Dummy", ax::Rect(0, 0, 960, 640));
#else
        auto view = ax::RenderView::create("Dummy");
#endif
        if (!view)
            std::exit(EXIT_FAILURE);
        director->setRenderView(view);
    }
    director->setAnimationInterval(1.0 / 60.0);
    auto files = ax::FileUtils::getInstance();
    files->addSearchPath("res");

    auto engine = ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(axjs::registerAll);
    if (!engine->start())
        std::exit(EXIT_FAILURE);

#if AX_ENABLE_JS_DEBUGGER
    if (const char* debug = std::getenv("AXMOL_JS_DEBUG"); debug && debug[0] == '1')
    {
        const char* wait = std::getenv("AXMOL_JS_DEBUG_WAIT");
        if (!engine->enableDebugger(5086, wait && wait[0] == '1'))
            std::exit(EXIT_FAILURE);
    }
#endif

    if (!engine->runScript("script/jsb_boot.js") || !engine->runScript("main.js"))
        std::exit(EXIT_FAILURE);
    return true;
}

void AppDelegate::applicationDidEnterBackground()
{
    ax::Director::getInstance()->deactivate();
#if AX_ENABLE_AUDIO
    ax::AudioEngine::pauseAll();
#endif
}

void AppDelegate::applicationWillEnterForeground()
{
    ax::Director::getInstance()->activate();
#if AX_ENABLE_AUDIO
    ax::AudioEngine::resumeAll();
#endif
}

void AppDelegate::applicationWillQuit()
{
    ax::JsEngine::getInstance()->stop();
}
