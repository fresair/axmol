// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "runtime/JsRuntime.h"
#include "manual/Bindings.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/base/UserDefault.h"
#include "axmol/platform/FileUtils.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
void check(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
std::string read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Cannot read unchanged v3 fixture");
    return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: verify-legacy-source axmol-source-root\n";
        return 2;
    }
    const auto sourceRoot = std::filesystem::absolute(argv[1]);
    const auto fixture = sourceRoot / "tools/js-tests/bindings/legacy";
    auto* engine = ax::JsEngine::getInstance();
    auto* files = ax::FileUtils::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(axjs::registerAll);
    engine->setErrorHandler([](std::string_view error) { std::cerr << error << '\n'; });
    std::filesystem::path storage;
    auto cleanup = [&] {
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        ax::UserDefault::destroyInstance();
        if (!storage.empty()) {
            std::error_code error;
            std::filesystem::remove_all(storage, error);
        }
    };
    try {
        const char* overrideDirectory = std::getenv("AX_JS_TEST_TEMP_DIR");
        auto pattern = ((overrideDirectory ? std::filesystem::path(overrideDirectory) :
            std::filesystem::temp_directory_path()) / "axmol-legacy-source-XXXXXX").string();
        std::vector<char> writablePattern(pattern.begin(), pattern.end());
        writablePattern.push_back('\0');
        const char* directory = mkdtemp(writablePattern.data());
        check(directory, "Cannot create isolated legacy test storage");
        storage = directory;
        files->setWritablePath(storage.string() + "/");
        ax::UserDefault::setFileName("legacy-source.bin");
        files->addSearchPath((sourceRoot / "extensions/scripting/js-bindings").string());
        files->addSearchPath(fixture.string());
        ax::AutoreleasePool pool("unchanged v3 js-tests");
        check(engine->start(), "Legacy test registration failed");
        check(engine->runScript("script/jsb_boot.js"), "Legacy compatibility bootstrap failed");
        auto main = read(fixture / "upstream/src/tests-main.js");
        const auto end = main.find("//Controller stuff");
        check(end == 2992, "Original TestScene declaration changed");
        check(engine->evalString(std::string_view(main).substr(0, end),
            "upstream/src/tests-main.js"), "Original TestScene declaration failed");
        check(engine->runScript("upstream/src/BaseTestLayer/BaseTestLayer.js"),
            "Original BaseTestLayer definitions failed");
        check(engine->runScript("upstream/src/UnitTest/UnitTest.js"),
            "Original UnitTest definitions failed");
        check(engine->runScript("upstream/src/SchedulerTest/SchedulerTest.js"),
            "Original SchedulerTest definitions failed");
        check(engine->runScript("Harness.js"), "Legacy selected-case harness failed");
        check(engine->executeGlobalFunction("legacySourcePrepare") == 0,
            "Original RectUnitTest or TestNode constructor failed");
        auto* scheduler = ax::Director::getInstance()->getScheduler();
        scheduler->update(0.016f);
        check(engine->executeGlobalFunction("legacySourceVerifyPriority") == 0,
            "Original TestNode priority dispatch failed");
        scheduler->update(0.016f);
        check(engine->executeGlobalFunction("legacySourceVerifyPaused") == 0,
            "Original TestNode native pause failed");
        scheduler->update(0.016f);
        check(engine->executeGlobalFunction("legacySourceVerifyResumed") == 0,
            "Original TestNode native resume failed");
        scheduler->update(0.016f);
        check(engine->executeGlobalFunction("legacySourceFinish") == 0,
            "Original SchedulerUpdate callback removal failed");
        pool.clear();
        cleanup();
        std::cout << "{\"originalRectAssertions\":11,\"originalRectResultValues\":12,"
            "\"originalTestNode\":true,\"nativeUpdatePriority\":true,"
            "\"nativePauseResume\":true,\"originalUnscheduleAll\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Original v3 js-tests verification failed: " << error.what() << '\n';
        cleanup();
        return 1;
    }
}
