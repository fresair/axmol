// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
// Same-runtime .jsc compiler. This produces SpiderMonkey XDR, not encrypted JS.
#include <js/RequiredDefines.h>
#include <jsapi.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static const JSClass globalClass = {
    "AxmolBytecodeCompiler", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, nullptr,
    nullptr, nullptr, nullptr, JS_GlobalObjectTraceHook
};

static void report(JSContext*, const char* message, JSErrorReport* error)
{
    std::fprintf(stderr, "%s:%u: %s\n", error && error->filename ? error->filename : "<compiler>",
                 error ? error->lineno : 0, message);
}

static bool compileFile(JSContext* context, const char* input, const char* output)
{
    std::ifstream stream(input, std::ios::binary);
    if (!stream)
    {
        std::fprintf(stderr, "Cannot open source: %s\n", input);
        return false;
    }
    std::string source(std::istreambuf_iterator<char>{stream}, {});
    JSAutoRequest request(context);
    JS::CompartmentOptions compartmentOptions;
    compartmentOptions.setVersion(JSVERSION_LATEST);
    JS::RootedObject global(context, JS_NewGlobalObject(context, &globalClass, nullptr,
                                                      JS::FireOnNewGlobalHook, compartmentOptions));
    if (!global) return false;
    JSAutoCompartment compartment(context, global);
    if (!JS_InitStandardClasses(context, global)) return false;
    JS::CompileOptions options(context);
    options.setUTF8(true).setFileAndLine(input, 1);
    JS::RootedScript script(context);
    if (!JS::Compile(context, global, options, source.data(), source.size(), &script)) return false;
    uint32_t size = 0;
    void* encoded = JS_EncodeScript(context, script, &size);
    if (!encoded) return false;
    auto temporary = std::filesystem::path(output);
    temporary += ".axmol-tmp";
    bool passed = false;
    {
        std::ofstream destination(temporary, std::ios::binary | std::ios::trunc);
        destination.write(static_cast<const char*>(encoded), size);
        destination.flush();
        passed = destination.good();
    }
    JS_free(context, encoded);
    if (passed)
    {
        std::error_code error;
        std::filesystem::rename(temporary, output, error);
        passed = !error;
        if (error) std::fprintf(stderr, "Cannot replace output %s: %s\n", output, error.message().c_str());
    }
    if (!passed)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::fprintf(stderr, "Cannot write bytecode: %s\n", output);
    }
    return passed;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: axmol-jscompile input.js output.jsc\n");
        return 2;
    }
    if (!JS_Init()) return 1;
    JSRuntime* runtime = JS_NewRuntime(64 * 1024 * 1024);
    JSContext* context = runtime ? JS_NewContext(runtime, 8192) : nullptr;
    bool passed = false;
    if (context)
    {
        JS_SetErrorReporter(context, report);
        passed = compileFile(context, argv[1], argv[2]);
        JS_DestroyContext(context);
    }
    if (runtime) JS_DestroyRuntime(runtime);
    JS_ShutDown();
    return passed ? 0 : 1;
}
