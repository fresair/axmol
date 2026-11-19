// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
// A C++23 embedder probe: runtime creation, eval, exception, GC and VM restart.
#include <js/RequiredDefines.h>
#include <jsapi.h>
#include <jsfriendapi.h>
#include <cstdio>
#include <cstring>

static const JSClass globalClass = {
    "AxmolRuntimeSmoke", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, nullptr,
    nullptr, nullptr, nullptr, JS_GlobalObjectTraceHook
};

static void errorReporter(JSContext*, const char* message, JSErrorReport* report)
{
    std::fprintf(stderr, "%s:%u: %s\n", report && report->filename ? report->filename : "<runtime>", report ? report->lineno : 0, message);
}

static bool runOnce()
{
    JSRuntime* runtime = JS_NewRuntime(32 * 1024 * 1024);
    if (!runtime) return false;
    JS::RuntimeOptionsRef(runtime).setBaseline(AX_SM_JIT).setIon(AX_SM_JIT);
    JS_SetNativeStackQuota(runtime, 1024 * 1024);
    JSContext* context = JS_NewContext(runtime, 8192);
    bool passed = false;
    if (context)
    {
        JS_SetErrorReporter(context, errorReporter);
        JS::ContextOptionsRef(context).setDontReportUncaught(true);
#if AX_SM_JIT
        JS_SetOffthreadIonCompilationEnabled(runtime, false);
        JS_SetGlobalJitCompilerOption(runtime, JSJITCOMPILER_BASELINE_USECOUNT_TRIGGER, 0);
        JS_SetGlobalJitCompilerOption(runtime, JSJITCOMPILER_ION_USECOUNT_TRIGGER, 10);
#endif
        JSAutoRequest request(context);
        JS::CompartmentOptions options;
        options.setVersion(JSVERSION_LATEST);
        JS::RootedObject global(context, JS_NewGlobalObject(context, &globalClass, nullptr,
                                                          JS::FireOnNewGlobalHook, options));
        if (global)
        {
            JSAutoCompartment compartment(context, global);
            if (JS_InitStandardClasses(context, global))
            {
                JS::RootedValue result(context);
                const char* source = "var a=[]; for(var i=0;i<10000;++i)a.push({n:i}); a[9999].n+1;";
                passed = JS_EvaluateScript(context, global, source, std::strlen(source),
                                          "smoke.js", 1, &result) && result.isNumber() && result.toNumber() == 10000;
                JS_GC(runtime);
                source = "a[9999].n;";
                passed = passed && JS_EvaluateScript(context, global, source, std::strlen(source),
                                                     "after-gc.js", 1, &result) && result.isNumber() && result.toNumber() == 9999;
                source = "function hot(n){var sum=0;for(var i=0;i<n;++i)sum+=i;return sum;} var answer=0;for(var n=0;n<2000;++n)answer=hot(200);answer;";
                passed = passed && JS_EvaluateScript(context, global, source, std::strlen(source),
                                                     "hot-loop.js", 1, &result) && result.isNumber() && result.toNumber() == 19900;
                JS::RootedScript compiled(context);
                JS::CompileOptions compileOptions(context);
                compileOptions.setUTF8(true).setFileAndLine("encoded.js", 1);
                source = "21*2;";
                passed = passed && JS::Compile(context, global, compileOptions, source, std::strlen(source), &compiled);
                uint32_t encodedSize = 0;
                void* encoded = compiled ? JS_EncodeScript(context, compiled, &encodedSize) : nullptr;
                if (encoded)
                {
                    JS::RootedScript decoded(context, JS_DecodeScript(context, encoded, encodedSize, nullptr));
                    passed = passed && decoded && JS_ExecuteScript(context, global, decoded, &result) && result.toNumber() == 42;
                    JS_free(context, encoded);
                }
                else passed = false;
                source = "throw new Error('expected');";
                passed = passed && !JS_EvaluateScript(context, global, source, std::strlen(source),
                                                      "exception.js", 1, &result) && JS_IsExceptionPending(context);
                JS_ClearPendingException(context);
            }
        }
    }
    if (context) JS_DestroyContext(context);
    JS_DestroyRuntime(runtime);
    return passed;
}

int main()
{
    if (!JS_Init()) return 1;
    bool passed = runOnce() && runOnce();
    JS_ShutDown();
    std::printf("SpiderMonkey 33.1 C++23 smoke: %s (JIT=%d)\n", passed ? "PASS" : "FAIL", AX_SM_JIT);
    return passed ? 0 : 1;
}
