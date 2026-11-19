/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include "generated/axjs_register_gen.h"
#include "manual/Bindings.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/WeakPtr.h"
#include "axmol/platform/FileUtils.h"
#include "assets-manager/AssetsManagerEx.h"
#include "assets-manager/EventAssetsManagerEx.h"
#include "assets-manager/EventListenerAssetsManagerEx.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
unsigned checks, failures, scriptCallbacks;
void expect(bool value, std::string_view message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::cerr << "ASSETS_LISTENER_ASSERTION_FAILED " << message << '\n';
    }
}

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// This CLI has no JVM or AAssetManager. Use genuine POSIX FileUtils methods
// for its own temporary manifests; the original APK tests use real assets.
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(const std::string& root)
    {
        _defaultResRootPath = _writablePath = root;
        if (!init()) throw std::runtime_error("Native FileUtils initialization failed");
    }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat entry{};
        const std::string name(path);
        return stat(name.c_str(), &entry) == 0 && S_ISREG(entry.st_mode);
    }
};
#endif

// The allocator deterministically reuses the same storage after real Object
// release/destruction. Constructors, WeakPtr registry and native dispatch are
// the production implementations; no ID/generation or callback is simulated.
class ReusedManager final : public ax::ext::AssetsManagerEx
{
    static inline void* slot{};
    static inline bool occupied{};
public:
    ReusedManager(const std::string& manifest, const std::string& storage)
        : AssetsManagerEx(manifest, storage) {}
    static void* operator new(std::size_t size)
    {
        if (occupied) throw std::runtime_error("Manager slot is still occupied");
        if (!slot) slot = ::operator new(size);
        occupied = true;
        return slot;
    }
    static void operator delete(void*) noexcept { occupied = false; }
    static void freeStorage()
    {
        if (occupied) throw std::runtime_error("Manager survived test cleanup");
        ::operator delete(slot);
        slot = nullptr;
    }
    void emit()
    {
        dispatchUpdateEvent(ax::ext::EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION,
                            "real AssetsManager dispatch with a reused address");
    }
};

bool mark(JSContext*, unsigned argc, JS::Value* vp)
{
    ++scriptCallbacks;
    JS::CallArgsFromVp(argc, vp).rval().setUndefined();
    return true;
}
bool registration(JSContext* cx, JS::HandleObject global)
{
    return axjs::registerAllGenerated(cx, global) && axjs::registerCore(cx, global) &&
           axjs::registerInput(cx, global) && axjs::registerExtensions(cx, global) &&
           JS_DefineFunction(cx, global, "listenerLifetimeMark", mark, 0, 0);
}
void expose(ax::JsEngine& engine, const char* name, ReusedManager* manager)
{
    auto* cx = engine.getContext();
    JSAutoRequest request(cx);
    JSAutoCompartment compartment(cx, engine.getGlobalObject());
    JS::RootedObject global(cx, engine.getGlobalObject());
    JS::RootedValue value(cx);
    if (!axjs::wrapNative<ax::ext::AssetsManagerEx>(cx, manager, &value) || !JS_SetProperty(cx, global, name, value))
        throw std::runtime_error("Cannot expose the real native AssetsManager wrapper");
}
void eval(ax::JsEngine& engine, std::string_view source)
{
    if (!engine.evalString(source)) throw std::runtime_error("Assets listener JavaScript assertion failed");
}
}

int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    char temporary[] = "/data/local/tmp/axmol-assets-listener-XXXXXX";
#else
    char temporary[] = "/tmp/axmol-assets-listener-XXXXXX";
#endif
    auto* directory = mkdtemp(temporary);
    if (!directory) return 2;
    const std::string root = std::string(directory) + '/';
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root));
#endif
    auto* files = ax::FileUtils::getInstance();
    auto* dispatcher = ax::Director::getInstance()->getEventDispatcher();
    auto* engine = ax::JsEngine::getInstance();
    std::vector<std::string> errors;
    engine->setErrorHandler([&](std::string_view message) { errors.emplace_back(message); });
    engine->addRegisterCallback(registration);
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    dispatcher->setEnabled(true);
    ax::AutoreleasePool pool("real AssetsManager listener lifetime");
    const auto manifest = root + "local.manifest";
    try
    {
        if (!files->writeStringToFile("{\"version\":\"1.0.0\",\"assets\":{},\"searchPaths\":[]}", manifest))
            throw std::runtime_error("Cannot write the temporary native manifest");
        unsigned originalCalls{}, replacementCalls{};
        std::string replacementEventName;
        auto* first = new ReusedManager(manifest, root + "native/");
        auto* firstAddress = first;
        ax::WeakPtr<ReusedManager> firstLifetime(first);
        expect(!ax::ext::EventListenerAssetsManagerEx::create(nullptr, [](auto*) {}),
               "null manager cannot initialize a listener");
        expect(!ax::ext::EventListenerAssetsManagerEx::create(first, {}),
               "empty callback cannot initialize a listener");
        auto* listener = ax::ext::EventListenerAssetsManagerEx::create(first,
            [&](ax::ext::EventAssetsManagerEx* event) {
                ++originalCalls;
                expect(event && firstLifetime.get() == event->getAssetsManagerEx(), "original listener manager generation identity");
            });
        expect(listener && listener->checkAvailable(), "live listener available");
        dispatcher->addEventListenerWithFixedPriority(listener, 1);
        auto* clone = listener->clone();
        expect(clone && clone->checkAvailable(), "live listener clone available");
        dispatcher->addEventListenerWithFixedPriority(clone, 2);
        first->emit();
        expect(originalCalls == 2 && first->getReferenceCount() == 1,
               "original and clone dispatch normally without retaining the manager");
        first->release();
        expect(firstLifetime.expired(), "real release destroys the original manager");
        expect(!listener->checkAvailable() && !clone->checkAvailable(), "destroyed manager invalidates both listeners");
        auto* replacement = new ReusedManager(manifest, root + "native/");
        expect(replacement == firstAddress, "manager address was actually reused");
        auto* replacementListener = ax::ext::EventListenerAssetsManagerEx::create(replacement,
            [&](ax::ext::EventAssetsManagerEx* event) {
                ++replacementCalls;
                if (event) replacementEventName = event->getEventName();
                expect(event && event->getAssetsManagerEx() == replacement, "replacement listener manager identity");
            });
        dispatcher->addEventListenerWithFixedPriority(replacementListener, 3);
        replacement->emit();
        expect(originalCalls == 2 && replacementCalls == 1,
               "reused address dispatches only to the new generation listener");
        auto* expiredClone = listener->clone();
        expect(!expiredClone, "cloning an expired listener cannot bind the replacement manager");
        auto* other = new ax::ext::AssetsManagerEx(manifest, root + "other/");
        ax::ext::EventAssetsManagerEx unrelated(replacementEventName, other,
            ax::ext::EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION);
        dispatcher->dispatchEvent(&unrelated);
        expect(replacementCalls == 1, "same event name cannot deliver a different manager");
        other->release();
        replacement->release();
        dispatcher->removeAllEventListeners();
        pool.clear();
        std::cout << "ASSETS_LISTENER_NATIVE_REUSE_CHECKS " << checks << " failures=" << failures << '\n';

        for (unsigned vm = 0; vm < 2; ++vm)
        {
            const auto failuresBeforeVM = failures;
            expect(engine->start(), "start a real SpiderMonkey VM");
            auto* original = new ReusedManager(manifest, root + "vm/");
            auto* originalAddress = original;
            expose(*engine, "originalManager", original);
            eval(*engine, R"JS(
                function requireLifetime(value, message) { if (!value) throw Error(message); }
                var oldCalls=0, newCalls=0, oldEvent;
                var dispatcher=cc.Director.getInstance().getEventDispatcher();
                var oldListener=jsb.EventListenerAssetsManagerEx.create(originalManager,function(event) {
                    requireLifetime(event.getAssetsManagerEx()===originalManager,'old manager wrapper identity');
                    oldEvent=event; oldCalls++; listenerLifetimeMark();
                });
                dispatcher.addEventListenerWithFixedPriority(oldListener,1);
            )JS");
            original->emit();
            eval(*engine, "requireLifetime(oldCalls===1,'real original callback');var expiredEvent=false;"
                          "try{oldEvent.getEventCode();}catch(e){expiredEvent=true;}"
                          "requireLifetime(expiredEvent,'temporary Event invalidates after callback');");
            expect(original->getReferenceCount() == 1, "JS listeners do not retain their native manager");
            original->release();
            auto* current = new ReusedManager(manifest, root + "vm/");
            expect(current == originalAddress, "JS callback manager native address was actually reused");
            expose(*engine, "replacementManager", current);
            eval(*engine, R"JS(
                requireLifetime(replacementManager!==originalManager,'replacement has a new wrapper');
                var expired=false;try{originalManager.getState();}catch(e){expired=true;}
                requireLifetime(expired,'old manager wrapper remains invalid');
                var newListener=jsb.EventListenerAssetsManagerEx.create(replacementManager,function(event) {
                    requireLifetime(event.getAssetsManagerEx()===replacementManager,'new manager wrapper identity');
                    newCalls++; listenerLifetimeMark();
                });
                dispatcher.addEventListenerWithFixedPriority(newListener,2);
            )JS");
            current->emit();
            eval(*engine, "requireLifetime(oldCalls===1&&newCalls===1,'dead generation listener never enters live callback');");
            engine->garbageCollect();
            expect(errors.empty(), "GC and native reuse preserve callback identity without exceptions");
            expect(current->getReferenceCount() == 1, "GC does not add hidden manager ownership");
            auto callbacksBeforeStop = scriptCallbacks;
            engine->stop();
            current->emit();
            expect(scriptCallbacks == callbacksBeforeStop && errors.empty(), "VM stop prevents old callbacks from entering a closed VM");
            current->release();
            // Retain the fixed-priority listener in the dispatcher across the
            // next VM. Both VM generation and native generation must expire.
            pool.clear();
            std::cout << "ASSETS_LISTENER_VM_" << vm + 1
                      << (failures == failuresBeforeVM ? "_PASS" : "_FAIL")
                      << " callbacks=" << scriptCallbacks << '\n';
        }
        dispatcher->removeAllEventListeners();
        pool.clear();
        ReusedManager::freeStorage();
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cout << "ASSETS_LISTENER_LIFETIME_RESULT assertions=" << checks << " failures=" << failures
                  << " callbacks=" << scriptCallbacks << '\n';
        return failures ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ASSETS_LISTENER_LIFETIME_ERROR " << error.what() << '\n';
        for (const auto& message : errors) std::cerr << message << '\n';
        engine->stop();
        return 1;
    }
}
