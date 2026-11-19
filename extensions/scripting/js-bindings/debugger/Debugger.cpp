/*
 * Copyright (c) 2013-2016 Chukong Technologies Inc.
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Axmol Engine contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "Debugger.h"

#ifndef AX_ENABLE_JS_DEBUGGER
#    define AX_ENABLE_JS_DEBUGGER 0
#endif

#if AX_ENABLE_JS_DEBUGGER
#    include "axmol/platform/FileUtils.h"
#    include "js/OldDebugAPI.h"
#    include <atomic>
#    include <cerrno>
#    include <condition_variable>
#    include <deque>
#    include <memory>
#    include <mutex>
#    include <thread>
#    include <vector>
#    include <cstring>
#    include <chrono>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <netdb.h>
#    include <fcntl.h>
#    include <unistd.h>

namespace axjs::debugger
{
namespace
{
constexpr std::size_t maxPacketSize = 8 * 1024 * 1024;
constexpr std::size_t maxQueuedSize = 16 * 1024 * 1024;

JSClass debugGlobalClass = {"AxmolDebugger", JSCLASS_GLOBAL_FLAGS, JS_PropertyStub, JS_DeletePropertyStub,
                           JS_PropertyStub, JS_StrictPropertyStub, JS_EnumerateStub, JS_ResolveStub,
                           JS_ConvertStub, nullptr, nullptr, nullptr, nullptr, JS_GlobalObjectTraceHook};

struct Message
{
    std::uint64_t session;
    std::string text;
};

bool interruptCallback(JSContext* cx);

class Service
{
public:
    JSContext* cx = nullptr;
    JSRuntime* runtime = nullptr;
    JSInterruptCallback previousInterrupt = nullptr;
    std::unique_ptr<JS::PersistentRootedObject> global;
    std::thread::id owner;
    std::atomic<bool> stopping{false};
    std::thread transport;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Message> inputs;
    std::deque<Message> outputs;
    std::size_t inputBytes = 0;
    std::size_t outputBytes = 0;
    int listener = -1;
    int client = -1;
    std::uint64_t activeSession = 0;
    unsigned nestLevel = 0;
    bool startupReady = false;
    bool failed = false;

    bool listen(const Options& options)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        auto port = std::to_string(options.port);
        if (getaddrinfo(options.bindAddress.c_str(), port.c_str(), &hints, &addresses) != 0)
            return false;
        for (auto* address = addresses; address; address = address->ai_next)
        {
            int socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (socket < 0)
                continue;
            if (socket >= FD_SETSIZE)
            {
                ::close(socket);
                continue;
            }
            int yes = 1;
            setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#    ifdef SO_NOSIGPIPE
            setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#    endif
            if (::bind(socket, address->ai_addr, address->ai_addrlen) == 0 && ::listen(socket, 1) == 0)
            {
                listener = socket;
                break;
            }
            ::close(socket);
        }
        freeaddrinfo(addresses);
        if (listener < 0)
            return false;
        fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK);
        transport = std::thread([this] { run(); });
        return true;
    }

    bool enqueueInput(std::uint64_t session, std::string text)
    {
        std::lock_guard lock(mutex);
        const bool lifecycle = text == "connected" || text == "disconnected";
        if (!lifecycle && inputBytes + text.size() > maxQueuedSize)
            return false;
        inputBytes += text.size();
        inputs.push_back({session, std::move(text)});
        changed.notify_all();
        // The sole thread-safe JSAPI operation performed by the IO thread.
        // It only requests work; the interrupt handler runs on the VM thread.
        JS_RequestInterruptCallback(runtime);
        return true;
    }

    void write(std::string text)
    {
        std::lock_guard lock(mutex);
        if (outputBytes + text.size() > maxQueuedSize)
        {
            failed = true;
            if (client >= 0)
                ::shutdown(client, SHUT_RDWR);
            return;
        }
        outputBytes += text.size();
        outputs.push_back({activeSession, std::move(text)});
    }

    bool extract(std::uint64_t session, std::string& bytes)
    {
        for (;;)
        {
            auto colon = bytes.find(':');
            if (colon == std::string::npos)
                return bytes.size() < 16;
            if (colon == 0 || colon > 8)
                return false;
            std::size_t size = 0;
            for (std::size_t i = 0; i < colon; ++i)
            {
                if (bytes[i] < '0' || bytes[i] > '9')
                    return false;
                size = size * 10 + static_cast<unsigned>(bytes[i] - '0');
            }
            if (!size || size > maxPacketSize)
                return false;
            if (bytes.size() - colon - 1 < size)
                return true;
            if (!enqueueInput(session, bytes.substr(colon + 1, size)))
                return false;
            bytes.erase(0, colon + 1 + size);
        }
    }

    void closeClient()
    {
        std::lock_guard lock(mutex);
        if (client >= 0)
            ::close(client);
        client = -1;
    }

    void run()
    {
        std::uint64_t session = 0;
        while (!stopping)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listener, &readSet);
            timeval timeout{0, 20000};
            if (::select(listener + 1, &readSet, nullptr, nullptr, &timeout) <= 0)
                continue;
            int accepted = ::accept(listener, nullptr, nullptr);
            if (accepted < 0)
                continue;
            if (accepted >= FD_SETSIZE)
            {
                ::close(accepted);
                continue;
            }
            fcntl(accepted, F_SETFL, fcntl(accepted, F_GETFL) | O_NONBLOCK);
#    ifdef SO_NOSIGPIPE
            int yes = 1;
            setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#    endif
            {
                std::lock_guard lock(mutex);
                client = accepted;
            }
            ++session;
            if (!enqueueInput(session, "connected"))
            {
                closeClient();
                continue;
            }
            std::string received;
            std::string sending;
            std::size_t sent = 0;
            bool connected = true;
            while (connected && !stopping)
            {
                if (sending.empty())
                {
                    std::lock_guard lock(mutex);
                    while (!outputs.empty())
                    {
                        auto message = std::move(outputs.front());
                        outputBytes -= message.text.size();
                        outputs.pop_front();
                        if (message.session == session)
                        {
                            sending = std::move(message.text);
                            sent = 0;
                            break;
                        }
                    }
                }
                fd_set writes;
                FD_ZERO(&readSet);
                FD_ZERO(&writes);
                FD_SET(accepted, &readSet);
                if (!sending.empty())
                    FD_SET(accepted, &writes);
                timeout = {0, 20000};
                int ready = ::select(accepted + 1, &readSet, &writes, nullptr, &timeout);
                if (ready < 0 && errno != EINTR)
                    break;
                if (ready <= 0)
                    continue;
                if (FD_ISSET(accepted, &writes))
                {
#    ifdef MSG_NOSIGNAL
                    constexpr int sendFlags = MSG_NOSIGNAL;
#    else
                    constexpr int sendFlags = 0;
#    endif
                    auto count = ::send(accepted, sending.data() + sent, sending.size() - sent, sendFlags);
                    if (count > 0)
                    {
                        sent += static_cast<std::size_t>(count);
                        if (sent == sending.size())
                            sending.clear();
                    }
                    else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                        connected = false;
                }
                if (connected && FD_ISSET(accepted, &readSet))
                {
                    char chunk[8192];
                    auto count = ::recv(accepted, chunk, sizeof(chunk), 0);
                    if (count > 0)
                    {
                        received.append(chunk, static_cast<std::size_t>(count));
                        connected = extract(session, received);
                    }
                    else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                        connected = false;
                }
            }
            closeClient();
            enqueueInput(session, "disconnected");
        }
    }

    void stop()
    {
        stopping = true;
        changed.notify_all();
        {
            std::lock_guard lock(mutex);
            if (client >= 0)
                ::shutdown(client, SHUT_RDWR);
            if (listener >= 0)
                ::shutdown(listener, SHUT_RDWR);
        }
        if (transport.joinable())
            transport.join();
        if (listener >= 0)
            ::close(listener);
        listener = -1;
        global.reset();
    }

    bool call(const char* method, const std::string* input = nullptr)
    {
        JSAutoCompartment compartment(cx, *global);
        JS::RootedValue result(cx);
        if (!input)
            return JS_CallFunctionName(cx, *global, method, JS::HandleValueArray::empty(), &result);
        JS::RootedString text(cx, JS_NewStringCopyN(cx, input->data(), input->size()));
        if (!text)
            return false;
        JS::RootedValue arg(cx, JS::StringValue(text));
        return JS_CallFunctionName(cx, *global, method, JS::HandleValueArray(arg), &result);
    }
};

std::unique_ptr<Service> service;

bool interruptCallback(JSContext* cx)
{
    if (!service)
        return true;
    auto* runtime = service->runtime;
    auto previous = service->previousInterrupt;
    JS_SetInterruptCallback(runtime, nullptr);
    bool ok = !previous || previous(cx);
    if (ok)
        pump();
    if (service)
        JS_SetInterruptCallback(runtime, interruptCallback);
    return ok;
}

bool bufferWrite(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 1 || !args[0].isString())
    {
        JS_ReportError(cx, "_bufferWrite expects a string");
        return false;
    }
    JS::RootedString str(cx, args[0].toString());
    char* utf8 = JS_EncodeStringToUTF8(cx, str);
    if (!utf8)
        return false;
    service->write(utf8);
    JS_free(cx, utf8);
    args.rval().setUndefined();
    return true;
}

bool enterLoop(JSContext* cx, unsigned argc, JS::Value* vp)
{
    unsigned level = ++service->nestLevel;
    while (!service->stopping && service->nestLevel >= level)
    {
        pump();
        if (service->nestLevel >= level)
        {
            std::unique_lock lock(service->mutex);
            service->changed.wait_for(lock, std::chrono::milliseconds(20), [] {
                return service->stopping || !service->inputs.empty();
            });
        }
    }
    JS::CallArgsFromVp(argc, vp).rval().setNumber(service->nestLevel);
    return true;
}

bool exitLoop(JSContext*, unsigned argc, JS::Value* vp)
{
    if (service->nestLevel)
        --service->nestLevel;
    JS::CallArgsFromVp(argc, vp).rval().setUndefined();
    return true;
}

bool nestLevel(JSContext*, unsigned argc, JS::Value* vp)
{
    JS::CallArgsFromVp(argc, vp).rval().setNumber(service->nestLevel);
    return true;
}

bool startupReady(JSContext*, unsigned argc, JS::Value* vp)
{
    service->startupReady = true;
    JS::CallArgsFromVp(argc, vp).rval().setUndefined();
    return true;
}
}  // namespace

bool initialize(JSContext* cx, JS::HandleObject gameGlobal, const Options& options)
{
    if (service)
        return service->cx == cx && !service->stopping;
    if (!cx || !gameGlobal || !options.port || options.bindAddress.empty())
        return false;
    auto script = ax::FileUtils::getInstance()->getStringFromFile("script/jsb_debugger.js");
    if (script.empty())
    {
        JS_ReportError(cx, "Missing script/jsb_debugger.js debugger resource");
        return false;
    }
    service = std::make_unique<Service>();
    service->cx = cx;
    service->runtime = JS_GetRuntime(cx);
    service->owner = std::this_thread::get_id();
    JS::CompartmentOptions compartmentOptions;
    compartmentOptions.setVersion(JSVERSION_LATEST);
    JS::RootedObject global(cx, JS_NewGlobalObject(cx, &debugGlobalClass, nullptr, JS::DontFireOnNewGlobalHook,
                                                 compartmentOptions));
    bool ok = global != nullptr;
    if (ok)
    {
        service->global = std::make_unique<JS::PersistentRootedObject>(cx, global);
        JSAutoCompartment compartment(cx, global);
        ok = JS_InitStandardClasses(cx, global) && JS_DefineDebuggerObject(cx, global);
        if (ok)
            JS_FireOnNewGlobalObject(cx, global);
        ok = ok && JS_DefineFunction(cx, global, "_bufferWrite", bufferWrite, 1, 0) &&
             JS_DefineFunction(cx, global, "_enterNestedEventLoop", enterLoop, 0, 0) &&
             JS_DefineFunction(cx, global, "_exitNestedEventLoop", exitLoop, 0, 0) &&
             JS_DefineFunction(cx, global, "_getEventLoopNestLevel", nestLevel, 0, 0) &&
             JS_DefineFunction(cx, global, "_debuggerStartupReady", startupReady, 0, 0);
        JS::RootedValue result(cx);
        JS::CompileOptions compileOptions(cx);
        compileOptions.setFileAndLine("script/jsb_debugger.js", 1);
        ok = ok && JS::Evaluate(cx, global, compileOptions, script.data(), script.size(), &result);
        JS::RootedObject wrapped(cx, gameGlobal);
        ok = ok && JS_WrapObject(cx, &wrapped);
        JS::RootedValue arg(cx, wrapped ? JS::ObjectValue(*wrapped) : JS::UndefinedValue());
        ok = ok && JS_CallFunctionName(cx, global, "_prepareDebugger", JS::HandleValueArray(arg), &result);
    }
    if (ok)
    {
        service->previousInterrupt = JS_SetInterruptCallback(service->runtime, interruptCallback);
        ok = service->listen(options);
    }
    if (!ok)
    {
        if (JS_GetInterruptCallback(service->runtime) == interruptCallback)
            JS_SetInterruptCallback(service->runtime, service->previousInterrupt);
        service->stop();
        service.reset();
        return false;
    }
    if (options.waitForConnection)
    {
        while (!service->startupReady && !service->stopping)
        {
            pump();
            std::unique_lock lock(service->mutex);
            service->changed.wait_for(lock, std::chrono::milliseconds(20), [] {
                return service->startupReady || service->stopping || !service->inputs.empty();
            });
        }
    }
    return true;
}

void pump()
{
    if (!service || service->owner != std::this_thread::get_id())
        return;
    struct InterruptGuard
    {
        JSRuntime* runtime;
        JSInterruptCallback saved;
        ~InterruptGuard()
        {
            if (service)
                JS_SetInterruptCallback(runtime, saved);
        }
    } guard{service->runtime, JS_SetInterruptCallback(service->runtime, nullptr)};
    for (;;)
    {
        Message message;
        {
            std::lock_guard lock(service->mutex);
            if (service->inputs.empty())
                break;
            message = std::move(service->inputs.front());
            service->inputBytes -= message.text.size();
            service->inputs.pop_front();
        }
        if (message.text == "connected")
            service->activeSession = message.session;
        if (message.session != service->activeSession)
            continue;
        if (!service->call("processInput", &message.text))
        {
            JS_ClearPendingException(service->cx);
            // Do not leave a failed debugger holding the game at a pause.
            service->call("_shutdownDebugger");
            JS_ClearPendingException(service->cx);
            service->nestLevel = 0;
            service->startupReady = true;
        }
    }
}

void shutdown()
{
    if (!service || service->owner != std::this_thread::get_id())
        return;
    service->call("_shutdownDebugger");
    JS_ClearPendingException(service->cx);
    service->nestLevel = 0;
    JS_SetInterruptCallback(service->runtime, service->previousInterrupt);
    service->stop();
    service.reset();
}

bool isEnabled() { return service != nullptr; }
}  // namespace axjs::debugger
#else
namespace axjs::debugger
{
bool initialize(JSContext*, JS::HandleObject, const Options&) { return false; }
void pump() {}
void shutdown() {}
bool isEnabled() { return false; }
}  // namespace axjs::debugger
#endif
