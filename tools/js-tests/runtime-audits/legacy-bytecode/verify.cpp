// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Link against the current axjs and rebuilt runtime, never the historical lib.
#include "js-bindings/runtime/JsRuntime.h"
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
void write(const std::filesystem::path& path, const std::string& data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(data.data(), data.size());
    check(output.good(), "Cannot write fixture");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: verify-legacy-bytecode legacy-sample.jsc\n";
        return 2;
    }
    auto* engine = ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    std::vector<std::string> errors;
    engine->setErrorHandler([&](std::string_view error) { errors.emplace_back(error); });
    std::filesystem::path directory;
    try {
        std::ifstream input(argv[1], std::ios::binary);
        check(bool(input), "Cannot read historical fixture");
        const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        check(bytes.size() > 4, "Historical fixture is empty");
        check(engine->start(), "Current JsEngine did not start");
        bool decodes = false;
        {
            auto* cx = engine->getContext();
            JSAutoCompartment compartment(cx, engine->getGlobalObject());
            JS::RootedScript script(cx, JS_DecodeScript(cx, bytes.data(), uint32_t(bytes.size()), nullptr));
            decodes = bool(script);
            JS_ClearPendingException(cx);
        }
        check(decodes, "Known historical .jsc fixture no longer decodes");
        const char* overrideDirectory = std::getenv("AX_JS_TEST_TEMP_DIR");
        auto pattern = ((overrideDirectory ? std::filesystem::path(overrideDirectory) :
            std::filesystem::temp_directory_path()) / "axmol-legacy-bytecode-XXXXXX").string();
        std::vector<char> writablePattern(pattern.begin(), pattern.end());
        writablePattern.push_back('\0');
        const char* temporary = mkdtemp(writablePattern.data());
        check(temporary != nullptr, "Cannot create temporary directory");
        directory = temporary;
        const auto valid = directory / "legacy.jsc";
        write(valid, bytes);
        bool standalone = engine->runScript(valid.string());
        check(standalone == decodes, "JsEngine disagrees with JS_DecodeScript");
        if (decodes) {
            check(engine->executeGlobalFunction("legacyBytecodeValue") == 240, "Historical execution result differs");
            check(engine->evalString("if (legacyBytecodeText !== '旧字节码 / SpiderMonkey 33.1') throw Error('UTF8');"),
                  "Historical Unicode constant differs");
            engine->cleanAllScripts();
            write(directory / "legacy.js", "throw new Error('Source must not run when historical bytecode is compatible');");
            check(engine->runScript((directory / "legacy.js").string()), "Compatible historical .jsc was not preferred");
        }
        engine->cleanAllScripts();
        write(directory / "fallback.jsc", "broken bytecode");
        write(directory / "fallback.js", "function fallbackBytecodeValue(){return 91;}");
        check(engine->runScript((directory / "fallback.js").string()), "Corrupt .jsc did not fall back to source");
        check(engine->executeGlobalFunction("fallbackBytecodeValue") == 91, "Fallback source did not execute");
        engine->cleanAllScripts();
        std::filesystem::remove(directory / "fallback.js");
        const auto before = errors.size();
        check(!engine->runScript((directory / "fallback.jsc").string()), "Corrupt .jsc without source was accepted");
        check(errors.size() > before && errors.back().find("no compatible bytecode or source") != std::string::npos,
              "Missing fallback did not produce an explicit error");
        engine->stop();
        check(engine->start() && engine->runScript(valid.string()) == decodes, "Historical load changed after VM restart");
        if (decodes) check(engine->executeGlobalFunction("legacyBytecodeValue") == 240, "Restart execution differs");
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        std::filesystem::remove_all(directory);
        std::cout << "{\"legacyDecode\":" << (decodes ? "true" : "false")
                  << ",\"legacyStandaloneExecution\":" << (standalone ? "true" : "false")
                  << ",\"sourceFallback\":true,\"missingFallbackError\":true,\"vmRestart\":true}\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "Legacy bytecode verification failed: " << failure.what() << '\n';
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        if (!directory.empty()) std::filesystem::remove_all(directory);
        return 1;
    }
}
