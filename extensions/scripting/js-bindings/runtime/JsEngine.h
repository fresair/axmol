/*
 * Copyright (c) 2026 Axmol contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "axmol/base/ScriptSupport.h"
#include <jsapi.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace axjs { struct RuntimeState; }
namespace ax
{
/** SpiderMonkey 33.1 script backend. Native objects retain their C++ ownership. */
class JsEngine final : public ScriptEngineProtocol
{
public:
    using RegisterCallback = bool (*)(JSContext*, JS::HandleObject);
    using ErrorHandler = std::function<void(std::string_view)>;
    JsEngine();
    ~JsEngine() override;
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;
    static JsEngine* getInstance();

    void addRegisterCallback(RegisterCallback callback);
    bool start();
    void stop();
    void cleanup() { stop(); }
    bool restart();
    bool isRunning() const;
    bool isOwnerThread() const;
    uint64_t getGeneration() const;
    JSContext* getContext() const;
    JSRuntime* getRuntime() const;
    JSObject* getGlobalObject() const;
    std::shared_ptr<axjs::RuntimeState> getRuntimeState() const;

    bool evalString(std::string_view code, std::string_view filename = "<eval>");
    bool evalString(std::string_view code, JS::MutableHandleValue result, std::string_view filename = "<eval>");
    bool runScript(std::string_view filename);
    bool runScript(std::string_view filename, JS::MutableHandleValue result);
    bool compileBytecode(std::string_view filename, std::string_view outputFilename);
    void cleanScript(std::string_view filename);
    void cleanAllScripts();
    void drainDeferredReleases();
    void update(float delta);
    /** Director-owned frame entry; explicit update() remains supported. */
    void updateFrame(float delta) override;
    /** Hooks apply to this VM only and are removed before runtime destruction. */
    void addCleanupCallback(std::function<void()> callback);
    void addUpdateCallback(std::function<void(float)> callback);
    bool enableDebugger(uint16_t port = 5086, bool waitForConnection = false,
                        std::string_view bindAddress = "127.0.0.1");
    void setErrorHandler(ErrorHandler handler);
    void reportException();

    ccScriptType getScriptType() override { return kScriptTypeJavascript; }
    int executeString(const char* codes) override;
    int executeScriptFile(const char* filename) override;
    int executeGlobalFunction(const char* functionName) override;
    int sendEvent(const ScriptEvent& event) override;
    bool handleAssert(const char* message) override;
    bool parseConfig(ConfigType type, std::string_view str) override;
    void setCalledFromScript(bool value) override;
    bool isCalledFromScript() override;
    void invalidateScriptObject(Object* object) override;
    void removeScriptObjectByObject(Object* object) override { invalidateScriptObject(object); }
    void removeObjectProxy(Object* object) override { invalidateScriptObject(object); }
    // NativeGC=0 roots wrappers throughout their native lifetime, so graph roots
    // never alter native reference counts or allow an alive wrapper to disappear.
    void rootObject(Object* object) override;
    void unrootObject(Object* object) override;
    void rootScriptObject(Object* object) override { rootObject(object); }
    void unrootScriptObject(Object* object) override { unrootObject(object); }
    void garbageCollect() override;
    bool dispatchNodeLifecycle(Node* node, const char* method) override;
    bool dispatchNodeUpdate(Node* node, float delta) override;
    bool dispatchActionUpdate(Action* action, float normalizedTime) override;
    bool invokeNodeBase(Node* node, std::string_view method, float delta = 0.f);
    void setNodeLifecycleNative(const char* method, JSNative function);
    void setEventHandledByNative(ScriptEventType type);
    bool callNativeMethod(Object* object, const char* method, const JS::HandleValueArray& args,
                          JS::MutableHandleValue result, bool* found = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace ax
