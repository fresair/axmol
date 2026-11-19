// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Isolated real native update-phase proof. FramePhaseAdapter.h is generated
// byte-for-byte from the candidate Director block; no platform/GPU frame runs.
#include "manual/Bindings.h"
#include "runtime/JsRuntime.h"
#include "FramePhaseAdapter.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/CustomEventListener.h"
#include "axmol/base/UserDefault.h"
#include "axmol/platform/FileUtils.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <thread>

namespace {
unsigned checks{}, failures{};
void require(bool value, const char* name)
{
    ++checks; failures += !value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}
bool near(float a, float b) { return std::abs(a - b) < 1e-6f; }
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils {
    std::string storage;
public:
    NativeFiles(std::string resource, std::string writable) : storage(std::move(writable)) {
        _defaultResRootPath = std::move(resource);
        if (!init()) throw std::runtime_error("Native POSIX FileUtils init failed");
    }
    std::string getWritablePath() const override { return storage; }
    std::string getNativeWritableAbsolutePath() const override { return storage; }
    bool isFileExistInternal(std::string_view value) const override {
        struct stat info{}; const std::string path(value);
        return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }
};
#endif
std::shared_ptr<axjs::Callback> makeCallback(ax::JsEngine* engine, const char* code)
{
    auto* cx = engine->getContext();
    JS::RootedValue function(cx);
    if (!engine->evalString(code, &function)) return {};
    JS::RootedObject global(cx, engine->getGlobalObject());
    JS::RootedValue receiver(cx);
    if (!JS_GetProperty(cx, global, "frameReceiver", &receiver) || !receiver.isObject()) return {};
    JS::RootedObject object(cx, &receiver.toObject());
    return axjs::Callback::create(cx, function, object);
}
bool expression(ax::JsEngine* engine, const char* code)
{
    JS::RootedValue result(engine->getContext());
    return engine->evalString(code, &result) && result.isTrue();
}
struct ScaleMutator {
    ax::Scheduler* scheduler;
    float received{};
    void update(float delta) { received = delta; scheduler->setTimeScale(7.f); }
};
class RecordingBackend final : public ax::ScriptEngineProtocol {
    unsigned* calls;
    unsigned* destroyed;
public:
    RecordingBackend(unsigned* count, unsigned* deaths) : calls(count), destroyed(deaths) {}
    ~RecordingBackend() override { ++*destroyed; }
    int executeString(const char*) override { return 0; }
    int executeScriptFile(const char*) override { return 0; }
    int executeGlobalFunction(const char*) override { return 0; }
    int sendEvent(const ax::ScriptEvent&) override { return 0; }
    bool handleAssert(const char*) override { return false; }
    bool parseConfig(ConfigType, std::string_view) override { return false; }
    void updateFrame(float) override { ++*calls; }
};
class PassiveBackend final : public ax::ScriptEngineProtocol {
public:
    int executeString(const char*) override { return 0; }
    int executeScriptFile(const char*) override { return 0; }
    int executeGlobalFunction(const char*) override { return 0; }
    int sendEvent(const ax::ScriptEvent&) override { return 0; }
    bool handleAssert(const char*) override { return false; }
    bool parseConfig(ConfigType, std::string_view) override { return false; }
    // Deliberately inherits the default no-op frame virtual.
};
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) return 2;
    try {
        const auto root = std::filesystem::absolute(argv[1]);
        const auto storage = argc == 3 ? std::filesystem::path(argv[2])
                                      : root / "tools/js-tests/bindings/scheduler/.cache/storage";
        std::filesystem::create_directories(storage);
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
        ax::FileUtils::setDelegate(new NativeFiles(root.string() + '/', storage.string() + '/'));
#endif
        auto* files = ax::FileUtils::getInstance();
        files->setWritablePath(storage.string() + "/");
        files->addSearchPath((root / "extensions/scripting/js-bindings").string());
        ax::UserDefault::setFileName("scheduler-frame-hook.bin");
        auto* director = ax::HeadlessFrameDirector::create();
        auto* scheduler = director->getScheduler();
        auto* events = director->getEventDispatcher();
        auto* engine = ax::JsEngine::getInstance();
        ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
        engine->addRegisterCallback(axjs::registerAll);
        std::vector<std::string> errors;
        engine->setErrorHandler([&](std::string_view value) { errors.emplace_back(value); });
        ax::AutoreleasePool pool("Isolated native Director frame phase");
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            scheduler->setTimeScale(1.f);
            events->setEnabled(true);
            require(engine->start(), "real runtime starts");
            require(engine->runScript("script/jsb_boot.js"), "complete shipping bootstrap");
            for (const auto* source : {"tests_resources.js", "BaseTestLayer/BaseTestLayer.js", "tests-main.js", "SchedulerTest/SchedulerTest.js"})
                require(engine->runScript((root / "tests/js-runtime-tests/fixtures/v3/src" / source).string()), "unchanged original definition loads");
            require(engine->evalString("director = cc.director; var schedulerCancelQueuedAfter = 0; if (!director) throw Error('Native Director');"), "real native director/application global and JS completion counter");
            int businessOwner{}, cancelOwner{};
            unsigned business{}, updates{}, cancellation{}, before{}, after{};
            bool callbackSucceeded = false, ownerThread = false;
            engine->addUpdateCallback([&](float) { ++updates; });
            scheduler->schedule([&](float) { ++business; }, &businessOwner, 0.f, false, "audit.original-business");
            scheduler->schedule([&](float) {
                ++cancellation;
                callbackSucceeded = engine->evalString("SchedulerUnscheduleAllHard.prototype.onUnscheduleAll(4);");
            }, &cancelOwner, 4.f, false, "audit.original-hard-four-seconds");
            auto runtime = engine->getRuntimeState();
            std::thread([runtime, &before] { runtime->enqueue([&before] { ++before; }); }).join();
            for (unsigned frame = 0; frame < 245; ++frame) director->advanceFrame(1.f / 60.f);
            require(before == 1, "worker queue really pumped before global cancellation");
            require(business > 200 && updates > 200, "real business timer and runtime pump initially active");
            require(cancellation == 1 && callbackSucceeded, "unchanged original callback runs once at native four-second interval");
            const auto businessAtCancel = business, updatesAtCancel = updates;
            std::thread([&, runtime] { runtime->enqueue([&] {
                ++after; ownerThread = engine->isOwnerThread();
                engine->evalString("++schedulerCancelQueuedAfter;");
            }); }).join();
            for (unsigned frame = 0; frame < 60; ++frame) director->advanceFrame(1.f / 60.f);
            require(business == businessAtCancel && cancellation == 1, "global cancellation still removes ordinary original timers");
            require(after == 1 && ownerThread, "queued native completion still reaches real JS on owner thread");
            require(updates > updatesAtCancel, "runtime housekeeping continues after business global cancellation");
            std::cout << "OBSERVATION generation=" << engine->getGeneration() << " business=" << business
                      << " runtime_before=" << updatesAtCancel << " runtime_after=" << updates
                      << " worker_before=" << before << " worker_after=" << after << '\n';

            const auto explicitBefore = updates;
            engine->update(.125f);
            require(updates == explicitBefore + 1, "explicit standalone update remains supported");
            unsigned pausedCompletion{}, nextTasks{}, beforeEvents{}, afterEvents{};
            auto* beforeListener = ax::CustomEventListener::create(ax::Director::EVENT_BEFORE_UPDATE,
                [&](ax::CustomEvent*) { ++beforeEvents; });
            auto* afterListener = ax::CustomEventListener::create(ax::Director::EVENT_AFTER_UPDATE,
                [&](ax::CustomEvent*) { ++afterEvents; });
            events->addEventListenerWithFixedPriority(beforeListener, -2);
            events->addEventListenerWithFixedPriority(afterListener, -2);
            std::thread([&] { runtime->enqueue([&] { ++pausedCompletion; }); }).join();
            const auto pausedUpdates = updates;
            director->postTask([&] { ++nextTasks; require(pausedCompletion == 1, "runtime completion precedes actual NextUpdate queue"); });
            director->pause();
            for (unsigned frame = 0; frame < 3; ++frame) director->advanceFrame(.25f);
            require(director->isPaused() && updates == pausedUpdates && pausedCompletion == 0 && nextTasks == 0,
                    "native Director pause holds housekeeping, scheduler and NextUpdate together");
            require(beforeEvents == 0 && afterEvents == 0, "paused native phase emits no update events");
            director->resume();
            director->advanceFrame(.125f);
            require(!director->isPaused() && updates == pausedUpdates + 1 && pausedCompletion == 1 && nextTasks == 1,
                    "native Director resume pumps one pending completion exactly once");

            float observedDelta{};
            engine->addUpdateCallback([&](float delta) { observedDelta = delta; });
            ScaleMutator mutator{scheduler};
            scheduler->scheduleUpdate(&mutator, -100, false);
            scheduler->setTimeScale(2.f);
            director->advanceFrame(.125f);
            require(near(observedDelta, .25f) && near(mutator.received, .25f) && near(scheduler->getTimeScale(), 7.f),
                    "frame hook uses Scheduler entry scale despite same-tick timer mutation");
            auto* scaleEvent = ax::CustomEventListener::create(ax::Director::EVENT_BEFORE_UPDATE,
                [&](ax::CustomEvent*) { scheduler->setTimeScale(3.f); });
            events->addEventListenerWithFixedPriority(scaleEvent, -5);
            director->advanceFrame(.125f);
            require(near(observedDelta, .375f) && near(mutator.received, .375f),
                    "before-update scale change applies to both native Scheduler and hook");
            events->removeEventListener(scaleEvent);
            scheduler->unscheduleUpdate(&mutator);
            scheduler->setTimeScale(0.f);
            const auto zeroUpdates = updates;
            director->advanceFrame(.125f);
            require(near(observedDelta, 0.f) && updates == zeroUpdates + 1,
                    "zero scaled delta still performs engine housekeeping");
            scheduler->setTimeScale(1.f);

            const auto disabledBeforeEvents = beforeEvents, disabledAfterEvents = afterEvents;
            events->setEnabled(false);
            unsigned disabledCompletion{};
            std::thread([&] { runtime->enqueue([&] { ++disabledCompletion; }); }).join();
            const auto disabledUpdates = updates;
            director->advanceFrame(.1f);
            require(disabledCompletion == 1 && updates == disabledUpdates + 1,
                    "disabled EventDispatcher does not suppress native frame housekeeping");
            require(beforeEvents == disabledBeforeEvents && afterEvents == disabledAfterEvents,
                    "disabled native input/update event contract remains unchanged");
            events->setEnabled(true);
            events->removeEventListener(beforeListener);
            events->removeEventListener(afterListener);

            std::vector<unsigned> mutableState;
            unsigned nested{};
            engine->addUpdateCallback([&, count = 0U](float) mutable {
                mutableState.push_back(++count);
                if (count == 1) engine->addUpdateCallback([&](float) { ++nested; });
            });
            director->advanceFrame(.1f);
            require(mutableState == std::vector<unsigned>{1} && nested == 0,
                    "callback addition does not mutate the current shared-handle snapshot");
            director->advanceFrame(.1f);
            require(mutableState == std::vector<unsigned>({1, 2}) && nested == 1,
                    "mutable update state persists and the addition runs next frame");

            require(engine->evalString("var frameReceiver={sum:0};"), "real callback receiver is created");
            auto callback = makeCallback(engine, "(function(){this.sum += 2;})");
            require(callback && callback->isValid(), "real rooted native callback is created");
            engine->garbageCollect();
            bool invoked{}, nativeOwner{};
            std::thread([&, callback] { runtime->enqueue([&, callback] {
                nativeOwner = engine->isOwnerThread(); invoked = callback->call(JS::HandleValueArray::empty());
            }); }).join();
            require(expression(engine, "frameReceiver.sum===0"), "worker does not enter the VM directly");
            director->advanceFrame(.1f);
            require(invoked && nativeOwner && expression(engine, "frameReceiver.sum===2"),
                    "real callback receiver survives GC and runs through frame owner queue");
            std::thread([callback] { callback->cancel(); }).join();
            require(!callback->isValid(), "cross-thread callback cancellation becomes inactive immediately");
            director->advanceFrame(.1f);
            { std::lock_guard lock(runtime->queueMutex);
              require(runtime->releaseQueue.empty(), "frame hook drains foreign callback root release ownership"); }
            callback.reset();

            const auto oldGeneration = engine->getGeneration();
            unsigned staleUpdates{}, oldCleanup{}, newUpdates{};
            engine->addCleanupCallback([&] { ++oldCleanup; });
            engine->addUpdateCallback([&](float) {
                require(engine->restart(), "update callback can restart the real VM");
                engine->addUpdateCallback([&](float) { ++newUpdates; });
            });
            engine->addUpdateCallback([&](float) { ++staleUpdates; });
            director->advanceFrame(.1f);
            require(engine->getGeneration() > oldGeneration && oldCleanup == 1 && !runtime->alive,
                    "restart invalidates the old generation and runs cleanup once");
            require(staleUpdates == 0 && newUpdates == 0,
                    "restart aborts remaining old snapshot and defers new-generation updates");
            director->advanceFrame(.1f);
            require(newUpdates == 1, "new generation pumps exactly once on its next native frame");
            require(engine->runScript("script/jsb_boot.js"), "new generation restores the real bootstrap");
            require(engine->evalString("var frameReceiver={sum:0};"), "new-generation callback receiver created");
            auto stoppedCallback = makeCallback(engine, "(function(){this.sum += 1;})");
            require(stoppedCallback && stoppedCallback->isValid(), "new-generation callback is initially valid");
            auto stopRuntime = engine->getRuntimeState();
            unsigned stopCleanup{}, stopFollower{};
            engine->addCleanupCallback([&] { ++stopCleanup; });
            engine->addUpdateCallback([&](float) { engine->stop(); });
            engine->addUpdateCallback([&](float) { ++stopFollower; });
            director->advanceFrame(.1f);
            require(!engine->isRunning() && stopCleanup == 1 && stopFollower == 0,
                    "update callback stop invalidates snapshot and runs cleanup once");
            require(!stoppedCallback->isValid() && stopRuntime->context == nullptr,
                    "VM stop releases real callback roots before destroying context");
            const auto finalUpdates = newUpdates;
            director->advanceFrame(.1f);
            require(newUpdates == finalUpdates, "stopped backend frame entry is safely inactive");
            stoppedCallback.reset();
            require(after == 1, "VM stop safely drains the pending native ownership transfer");
            require(errors.empty(), "native frame lifecycle raises no JS error");
            pool.clear();
        }
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        require(ax::ScriptEngineManager::getScriptEngineIfExists() == nullptr,
                "empty backend lookup does not recreate a singleton backend");
        director->advanceFrame(.1f);
        unsigned firstCalls{}, secondCalls{}, destroyed{};
        auto* first = new RecordingBackend(&firstCalls, &destroyed);
        ax::ScriptEngineManager::getInstance()->setScriptEngine(first);
        int replacementOwner{};
        // The real Scheduler calls this update target before the backend lookup.
        struct Replace {
            unsigned* calls; unsigned* destroyed; ax::Scheduler* scheduler;
            void update(float) {
                ax::ScriptEngineManager::getInstance()->setScriptEngine(new RecordingBackend(calls, destroyed));
                scheduler->unscheduleUpdate(this);
            }
        } replacement{&secondCalls, &destroyed, scheduler};
        scheduler->scheduleUpdate(&replacement, -100, false);
        director->advanceFrame(.1f);
        require(firstCalls == 0 && secondCalls == 1 && destroyed == 1,
                "backend is resolved after Scheduler replacement rather than dereferencing destroyed entry");
        ax::ScriptEngineManager::getInstance()->setScriptEngine(new PassiveBackend());
        director->advanceFrame(.1f);
        require(secondCalls == 1 && destroyed == 2, "other native script backends inherit a true no-op frame hook");
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        std::cout << "SCHEDULER_FRAME_HOOK checks=" << checks << " failures=" << failures
                  << " scope=headless-extracted-native-phase cycles=2 vm_generations=4\n";
        return failures ? 65 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Frame hook fixture failure: " << error.what() << '\n';
        return 70;
    }
}
