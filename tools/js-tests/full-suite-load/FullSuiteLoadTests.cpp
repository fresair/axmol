// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "runtime/JsRuntime.h"
#include "manual/Bindings.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
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

namespace
{
void check(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Cannot read the complete original js-tests fixture");
    return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()};
}
bool putString(ax::JsEngine* engine, const char* name, const std::string& value)
{
    JSContext* cx = engine->getContext();
    JS::RootedObject global(cx, engine->getGlobalObject());
    JS::RootedValue converted(cx);
    return axjs::toJsValue(cx, value, &converted) && JS_SetProperty(cx, global, name, converted);
}
std::string jsonString(std::string_view value)
{
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32)
        { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    result += '"';
    return result;
}
}

// This stage loads EVERY file in the unchanged project.json, in its original
// order, with the shipping bootstrap and native bindings. It does not claim to
// execute the graphical scenes. The application fixture runs the original
// main.js and scene menu on an actual render device in a separate test stage.
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--run-main"))
    {
        std::cerr << "Usage: js-full-suite-load-tests axmol-source-root [--run-main]\n";
        return 2;
    }
    const auto sourceRoot = std::filesystem::absolute(argv[1]);
    const auto project = sourceRoot / "tests/js-tests/Content/legacy/js-tests";
    auto* engine = ax::JsEngine::getInstance();
    auto* files = ax::FileUtils::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(axjs::registerAll);
    std::string lastError;
    engine->setErrorHandler([&](std::string_view error) {
        if(!lastError.empty()) lastError+='\n';
        lastError += error;
        std::cerr << error << '\n';
    });
    std::filesystem::path storage;
    std::vector<std::string> scripts;
    size_t loaded = 0;
    std::string current = "native registration";
    auto cleanup = [&] {
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        ax::UserDefault::destroyInstance();
        if (!storage.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(storage, error);
        }
    };
    try
    {
        const char* overrideDirectory = std::getenv("AX_JS_TEST_TEMP_DIR");
        auto pattern = ((overrideDirectory ? std::filesystem::path(overrideDirectory) :
            std::filesystem::temp_directory_path()) / "axmol-js-full-suite-XXXXXX").string();
        std::vector<char> writablePattern(pattern.begin(), pattern.end());
        writablePattern.push_back('\0');
        const char* directory = mkdtemp(writablePattern.data());
        check(directory, "Cannot create isolated js-tests storage");
        storage = directory;
        files->setWritablePath(storage.string() + "/");
        ax::UserDefault::setFileName("full-original-js-tests.bin");
        files->addSearchPath((sourceRoot / "extensions/scripting/js-bindings").string());
        files->addSearchPath(project.string());
        files->addSearchPath((project / "res/resjs").string());
        files->addSearchPath((project / "res").string());

        ax::AutoreleasePool pool("complete original v3 js-tests definitions");
        check(engine->start(), "Full native binding registration failed");
        current = "script/jsb_boot.js";
        lastError.clear();
        check(engine->runScript(current), "Full compatibility bootstrap failed");
        current = "project.json";
        check(putString(engine, "__fullSuiteProjectJson", readFile(project / current)), "Project JSON transfer failed");
        check(engine->evalString("cc.game.config=JSON.parse(__fullSuiteProjectJson);delete __fullSuiteProjectJson;",
            "full-suite-driver/config.js"), "Original project configuration failed");
        {
            JSContext* cx = engine->getContext();
            JS::RootedValue list(cx);
            check(engine->evalString("cc.game.config.jsList", &list, "full-suite-driver/config.js") &&
                axjs::fromJsValue(cx, list, scripts) && !scripts.empty(), "Original project jsList is invalid");
        }
        for (const auto& script : scripts)
        {
            current = script;
            lastError.clear();
            check(putString(engine, "__fullSuiteCurrentScript", script), "Script filename transfer failed");
            check(engine->evalString("require(__fullSuiteCurrentScript);", "full-suite-driver/load.js"),
                "An unchanged original project script failed");
            ++loaded;
            std::cout << "Loaded " << loaded << '/' << scripts.size() << ": " << script << '\n';
        }
        check(engine->evalString("delete __fullSuiteCurrentScript;", "full-suite-driver/cleanup.js"), "Driver cleanup failed");
        // Read the real top-level menu and callable scene factories after every
        // original definition has been installed. No replacement menu is used.
        std::string menu;
        {
            JSContext* cx = engine->getContext();
            JS::RootedValue value(cx);
            check(engine->evalString(
                "JSON.stringify(testNames.map(function(test){return {title:test.title,platforms:test.platforms,"
                "native:!!(test.platforms&(PLATFORM_JSB|(cc.sys.os===cc.sys.OS_OSX?PLATFORM_MAC:cc.sys.os===cc.sys.OS_ANDROID?PLATFROM_ANDROID:cc.sys.os===cc.sys.OS_IOS?PLATFROM_IOS:0))),sceneFactory:typeof test.testScene==='function',"
                "source:test.linksrc};}))", &value, "full-suite-driver/menu.js") &&
                axjs::fromJsValue(cx, value, menu), "Original test menu inventory failed");
        }
        if (argc == 3)
        {
            current = "main.js";
            check(engine->runScript(current), "Original main.js startup failed");
        }
        std::cout << "{\"phase\":" << jsonString(argc == 3 ? "main-startup" : "definitions")
            << ",\"loadedScripts\":" << loaded << ",\"totalScripts\":" << scripts.size()
            << ",\"completeOriginalProject\":true,\"graphicalCasesExecuted\":false,\"menu\":" << menu << "}\n";
        pool.clear();
        cleanup();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Complete original js-tests load failed: " << error.what() << '\n';
        std::cout << "{\"phase\":\"definitions\",\"loadedScripts\":" << loaded
            << ",\"totalScripts\":" << scripts.size() << ",\"failedScript\":" << jsonString(current)
            << ",\"error\":" << jsonString(lastError.empty() ? error.what() : lastError) << "}\n";
        cleanup();
        return 1;
    }
}
