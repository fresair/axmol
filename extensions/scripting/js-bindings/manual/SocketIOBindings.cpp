/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Legacy Socket.IO 0.9 / 1.x native JS API and wire protocol. The compatibility
// contract follows cocos2d-x v3 cocos/network/SocketIO.cpp and the MIT licensed
// manual/network/jsb_socketio.cpp by Chris Hannon / Chukong / Xiamen Yaji.
// This implementation uses Axmol's current HttpClient and yasio WebSocket;
// no browser, Node.js runtime, or second socket/STL implementation is needed.
#include "SocketIOBindings.h"
#include "ManualUtils.h"
#if AX_ENABLE_HTTP && AX_ENABLE_WEBSOCKET
#include "axmol/network/HttpClient.h"
#include "axmol/network/WebSocket.h"
#include "yasio/errc.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#endif

namespace axjs {
#if AX_ENABLE_HTTP && AX_ENABLE_WEBSOCKET
namespace {
using namespace manual;
using Clock = std::chrono::steady_clock;
struct Client;
struct Manager {
    std::weak_ptr<RuntimeState> vm;
    std::atomic<bool> alive{true};
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    std::vector<std::weak_ptr<Client>> clients;
    void post(std::function<void()> job)
    {
        if (!alive)
            return;
        std::lock_guard lock(mutex);
        if (alive)
            pending.emplace_back(std::move(job));
    }
    void update();
    void shutdown();
};
constexpr unsigned ManagerSlot = 0;
constexpr unsigned HandlerSlot = 1;
std::string jsonText(const rapidjson::Value& value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}
std::string argumentText(const rapidjson::Value& value)
{
    return value.IsString() ? std::string(value.GetString(), value.GetStringLength()) : jsonText(value);
}
bool identifier(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    });
}

struct Client : ax::network::WebSocket::Delegate, std::enable_shared_from_this<Client> {
    std::weak_ptr<Manager> manager;
    std::unique_ptr<JS::PersistentRootedObject> active;
    std::unique_ptr<ax::network::WebSocket> socket;
    std::string authority, endpoint, query, ca;
    bool secure{}, version1{}, connected{}, stopped{}, upgraded{};
    unsigned pingInterval{25000}, pingTimeout{60000};
    Clock::time_point lastPacket{Clock::now()}, nextPing{Clock::now()}, started{Clock::now()};
    JSContext* context() const
    {
        auto owner = manager.lock();
        auto runtime = owner ? owner->vm.lock() : nullptr;
        return owner && owner->alive && runtime && runtime->alive && runtime->onOwnerThread()
                   ? runtime->context : nullptr;
    }
    ~Client() override { shutdown(); }
    void shutdown()
    {
        stopped = true;
        connected = false;
        // WebSocket destruction stops its service, removes scheduler callbacks,
        // and purges queued native events before this delegate is destroyed.
        socket.reset();
        active.reset();
    }
    void event(const char* name, std::string_view data = {}, JSObject* methodReceiver = nullptr)
    {
        auto* cx = context();
        if (!cx || (!active && !methodReceiver))
            return;
        JSAutoRequest request(cx);
        JS::RootedObject receiver(cx, active ? active->get() : methodReceiver);
        JSAutoCompartment compartment(cx, receiver);
        JS::RootedValue handlers(cx, JS_GetReservedSlot(receiver, HandlerSlot)), fn(cx), value(cx), result(cx);
        if (!handlers.isObject())
            return;
        JS::RootedObject table(cx, &handlers.toObject());
        if (!JS_GetProperty(cx, table, name, &fn))
        {
            engine(cx)->reportException();
            return;
        }
        if (!fn.isObject() || !JS_ObjectIsCallable(cx, &fn.toObject()))
            return;
        if (data.empty())
            value.setNull();
        else if (!toJsValue(cx, data, &value))
        {
            engine(cx)->reportException();
            return;
        }
        if (!JS_CallFunctionValue(cx, receiver, fn, JS::HandleValueArray(value), &result))
            engine(cx)->reportException();
    }
    void disconnected(std::string_view reason = {})
    {
        if (stopped)
            return;
        stopped = true;
        connected = false;
        event("disconnect", reason);
        active.reset();
    }
    void failure(std::string_view message)
    {
        if (stopped)
            return;
        event("error", message);
        if (!stopped)
            disconnected();
        if (socket)
            socket->closeAsync();
    }
    void sendPacket(std::string_view packet)
    {
        if (!stopped && socket && socket->getReadyState() == ax::network::WebSocket::State::OPEN)
            socket->send(packet);
    }
    void connectNamespace()
    {
        if (endpoint.empty())
            return; // The server sends the default namespace connect packet.
        sendPacket(version1 ? "40" + endpoint + "," : "1::" + endpoint);
    }
    void begin()
    {
        auto* request = new ax::network::HttpRequest;
        std::string url = (secure ? "https://" : "http://") + authority +
                          "/socket.io/1/?EIO=2&transport=polling&b64=true";
        if (!query.empty())
            url += "&" + query;
        request->setUrl(url);
        request->setRequestType(ax::network::HttpRequest::Type::GET);
        if (secure && !ca.empty())
            ax::network::HttpClient::getInstance()->setSSLVerification(ca);
        std::weak_ptr<Client> weak = shared_from_this();
        std::weak_ptr<Manager> owner = manager;
        request->setCompleteCallback([weak, owner](auto*, ax::network::HttpResponse* response) {
            auto* bytes = response->getResponseData();
            const auto body = bytes->as_chars();
            std::string data(body.empty() ? "" : body.data(), body.size());
            const int code = response->getResponseCode(), nativeError = response->getInternalCode();
            if (auto state = owner.lock())
                state->post([weak, data = std::move(data), code, nativeError] {
                    if (auto client = weak.lock(); client && client->context() && !client->stopped)
                        client->handshake(data, code, nativeError);
                });
        });
        ax::network::HttpClient::getInstance()->send(request);
        request->release();
    }
    void handshake(std::string_view response, int code, int nativeError)
    {
        if (code != 200 || (nativeError && nativeError != yasio::errc::eof))
        {
            failure("Socket.IO HTTP handshake failed");
            return;
        }
        std::string sid;
        const auto jsonBegin = response.find('{');
        if (jsonBegin != std::string_view::npos)
        {
            rapidjson::Document json;
            const auto jsonEnd = response.find('}', jsonBegin);
            if (jsonEnd == std::string_view::npos)
            {
                failure("Malformed Socket.IO handshake");
                return;
            }
            json.Parse(response.data() + jsonBegin, jsonEnd - jsonBegin + 1);
            if (json.HasParseError() || !json.IsObject() || !json.HasMember("sid") || !json["sid"].IsString() ||
                !json.HasMember("pingInterval") || !json["pingInterval"].IsUint() ||
                !json.HasMember("pingTimeout") || !json["pingTimeout"].IsUint())
            {
                failure("Malformed Socket.IO handshake");
                return;
            }
            version1 = true;
            sid.assign(json["sid"].GetString(), json["sid"].GetStringLength());
            pingInterval = json["pingInterval"].GetUint();
            pingTimeout = json["pingTimeout"].GetUint();
        }
        else
        {
            const auto first = response.find(':'), second = response.find(':', first + 1),
                       third = response.find(':', second + 1);
            if (first == std::string_view::npos || second == std::string_view::npos ||
                third == std::string_view::npos || response.substr(third + 1).find("websocket") == std::string_view::npos)
            {
                failure("Malformed Socket.IO 0.9 handshake");
                return;
            }
            sid = response.substr(0, first);
            try
            {
                pingInterval = static_cast<unsigned>(std::stoul(std::string(response.substr(first + 1, second - first - 1))) * 1000);
                pingTimeout = static_cast<unsigned>(std::stoul(std::string(response.substr(second + 1, third - second - 1))) * 1000);
            }
            catch (...)
            {
                failure("Malformed Socket.IO heartbeat intervals");
                return;
            }
        }
        if (!identifier(sid) || !pingInterval || !pingTimeout || pingInterval > 3600000 || pingTimeout > 3600000)
        {
            failure("Invalid Socket.IO session or heartbeat intervals");
            return;
        }
        std::string url = (secure ? "wss://" : "ws://") + authority + "/socket.io/1/websocket/" +
                          (version1 ? "?EIO=2&transport=websocket&sid=" + sid : sid);
        if (!query.empty())
            url += (version1 ? "&" : "?") + query;
        socket = std::make_unique<ax::network::WebSocket>();
        if (!socket->open(this, url, ca))
            failure("Socket.IO WebSocket could not open");
    }
    void update()
    {
        if (stopped)
            return;
        const auto now = Clock::now();
        if (!socket)
        {
            if (now - started > std::chrono::seconds(30))
                failure("Socket.IO handshake timed out");
            return;
        }
        if (socket->getReadyState() != ax::network::WebSocket::State::OPEN)
            return;
        if (now - lastPacket > std::chrono::milliseconds(static_cast<uint64_t>(pingInterval) + pingTimeout))
        {
            failure("Socket.IO heartbeat timed out");
            return;
        }
        if (now >= nextPing && (!version1 || upgraded))
        {
            sendPacket(version1 ? "2" : "2::");
            nextPing = now + std::chrono::milliseconds(pingInterval * 9 / 10);
        }
    }
    void payload(std::string_view wire)
    {
        if (stopped || wire.empty())
            return;
        lastPacket = Clock::now();
        if (!version1)
        {
            const auto first = wire.find(':'), second = wire.find(':', first + 1), third = wire.find(':', second + 1);
            if (first == std::string_view::npos || second == std::string_view::npos)
                return;
            const auto ns = wire.substr(second + 1, (third == std::string_view::npos ? wire.size() : third) - second - 1);
            const auto data = third == std::string_view::npos ? std::string_view{} : wire.substr(third + 1);
            if (wire[0] == '2')
            {
                sendPacket("2::");
                return;
            }
            if (ns != endpoint && !(ns == "/" && endpoint.empty()))
                return;
            switch (wire[0])
            {
            case '0': disconnected(data); if (socket) socket->closeAsync(); break;
            case '1': if (!connected) { connected = true; event("connect"); } break;
            case '3': event("message", data); break;
            case '4': event("json", data); break;
            case '5':
            {
                rapidjson::Document json;
                json.Parse(data.data(), data.size());
                if (json.HasParseError() || !json.IsObject() || !json.HasMember("name") || !json["name"].IsString() ||
                    !json.HasMember("args") || !json["args"].IsArray())
                {
                    failure("Malformed Socket.IO event");
                    return;
                }
                auto& args = json["args"];
                event(json["name"].GetString(), args.Empty() ? "" : args.Size() == 1 ? argumentText(args[0]) : jsonText(args));
                break;
            }
            case '7': event("error", data); break;
            default: break;
            }
            return;
        }
        if (wire[0] == '2') { sendPacket("3" + std::string(wire.substr(1))); return; }
        if (wire == "3probe")
        {
            sendPacket("5");
            upgraded = true;
            connectNamespace();
            return;
        }
        if (wire.size() < 2 || wire[0] != '4')
            return;
        auto data = wire.substr(2);
        std::string_view ns;
        if (!data.empty() && data[0] == '/')
        {
            const auto comma = data.find(',');
            ns = data.substr(0, comma);
            data = comma == std::string_view::npos ? std::string_view{} : data.substr(comma + 1);
        }
        if (ns != endpoint)
            return;
        switch (wire[1])
        {
        case '0': if (!connected) { connected = true; event("connect"); } break;
        case '1': disconnected(data); if (socket) socket->closeAsync(); break;
        case '2':
        {
            // Packet IDs, when present, precede the array. The old JS API does
            // not expose acknowledgement callbacks; event delivery is retained.
            const auto array = data.find('[');
            if (array == std::string_view::npos) { failure("Malformed Socket.IO event"); return; }
            data.remove_prefix(array);
            rapidjson::Document json;
            json.Parse(data.data(), data.size());
            if (json.HasParseError() || !json.IsArray() || json.Empty() || !json[0].IsString())
            {
                failure("Malformed Socket.IO event");
                return;
            }
            std::string argument;
            if (json.Size() == 2)
                argument = argumentText(json[1]);
            else if (json.Size() > 2)
            {
                rapidjson::Value values(rapidjson::kArrayType);
                for (rapidjson::SizeType i = 1; i < json.Size(); ++i)
                    values.PushBack(json[i], json.GetAllocator());
                argument = jsonText(values);
            }
            event(json[0].GetString(), argument);
            break;
        }
        case '4': event("error", data); break;
        default: break;
        }
    }
    void onOpen(ax::network::WebSocket*) override
    {
        auto client = shared_from_this();
        if (auto owner = manager.lock())
            owner->post([client] {
                if (!client->context() || client->stopped)
                    return;
                client->lastPacket = Clock::now();
                client->nextPing = Clock::now() + std::chrono::milliseconds(client->pingInterval * 9 / 10);
                if (client->version1)
                    client->sendPacket("2probe");
                else
                    client->connectNamespace();
            });
    }
    void onMessage(ax::network::WebSocket*, const ax::network::WebSocket::Data& data) override
    {
        auto client = shared_from_this();
        std::string copied(data.len ? data.bytes : "", data.len);
        if (auto owner = manager.lock())
            owner->post([client, copied = std::move(copied), binary = data.isBinary] {
                if (client->context() && !client->stopped)
                {
                    if (binary) client->failure("Legacy Socket.IO text protocol received a binary packet");
                    else client->payload(copied);
                }
            });
    }
    void onClose(ax::network::WebSocket*, uint16_t, std::string_view reason) override
    {
        auto client = shared_from_this();
        if (auto owner = manager.lock())
            owner->post([client, reason = std::string(reason)] {
                if (client->context()) client->disconnected(reason);
            });
    }
    void onError(ax::network::WebSocket*, const ax::network::WebSocket::ErrorCode&) override
    {
        auto client = shared_from_this();
        if (auto owner = manager.lock())
            owner->post([client] {
                if (client->context()) client->failure("Socket.IO WebSocket transport failed");
            });
    }
};

void Manager::update()
{
    auto runtime = vm.lock();
    if (!alive || !runtime || !runtime->alive || !runtime->onOwnerThread())
        return;
    std::deque<std::function<void()>> jobs;
    { std::lock_guard lock(mutex); jobs.swap(pending); }
    for (auto& job : jobs)
    {
        if (!alive || !runtime->alive) break;
        job();
    }
    if (!alive || !runtime->alive) return;
    std::vector<std::shared_ptr<Client>> current;
    std::erase_if(clients, [&](const auto& item) {
        if (auto client = item.lock()) { current.emplace_back(std::move(client)); return false; }
        return true;
    });
    for (auto& client : current)
    {
        if (!alive || !runtime->alive) break;
        client->update();
    }
}
void Manager::shutdown()
{
    alive = false;
    std::deque<std::function<void()>> jobs;
    { std::lock_guard lock(mutex); jobs.swap(pending); }
    for (auto& item : clients)
        if (auto client = item.lock()) client->shutdown();
    clients.clear();
}
void finalize(JSFreeOp*, JSObject* object)
{
    delete static_cast<std::shared_ptr<Client>*>(JS_GetPrivate(object));
    const auto manager = JS_GetReservedSlot(object, ManagerSlot);
    if (manager.isDouble()) delete static_cast<std::shared_ptr<Manager>*>(manager.toPrivate());
}
JSClass clientClass = {"SocketIO", JSCLASS_HAS_PRIVATE | JSCLASS_HAS_RESERVED_SLOTS(2),
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, finalize, nullptr, nullptr, nullptr, nullptr};
std::shared_ptr<Client> selfClient(JSContext* cx, const JS::CallArgs& args)
{
    if (!args.thisv().isObject() || JS_GetClass(&args.thisv().toObject()) != &clientClass)
    {
        error(cx, "Invalid Socket.IO receiver");
        return {};
    }
    auto* holder = static_cast<std::shared_ptr<Client>*>(JS_GetPrivate(&args.thisv().toObject()));
    if (!holder || !(*holder)->context())
    {
        error(cx, "Socket.IO object belongs to a stopped VM");
        return {};
    }
    return *holder;
}
bool constructor(JSContext* cx, unsigned, JS::Value*)
{
    return error(cx, "Use SocketIO.connect(url) to create a client");
}
bool connect(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string url, ca;
    if (!argc || argc > 3 || !argument(cx, args, 0, url))
        return error(cx, "SocketIO.connect requires a URL, optional options and a CA certificate path");
    if (argc > 1 && args[1].isString() && !fromJsValue(cx, args[1], ca)) return false;
    if (argc > 2 && !args[2].isNullOrUndefined() && !argument(cx, args, 2, ca)) return false;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    if (url.find_first_of("\r\n\0#", 0, 4) != std::string::npos)
        return error(cx, "Invalid Socket.IO URL");
    auto uri = ax::network::Uri::parse(url);
    if (!uri.isValid() || (uri.getScheme() != "http" && uri.getScheme() != "https" &&
                          uri.getScheme() != "ws" && uri.getScheme() != "wss") || uri.getHost().empty() ||
        !uri.getUserName().empty() || !uri.getPassword().empty())
        return error(cx, "Invalid Socket.IO URL");
    if (!args.thisv().isObject()) return error(cx, "Use SocketIO.connect(url) to create a client");
    JS::RootedObject ctor(cx, &args.thisv().toObject()), prototype(cx);
    JS::RootedValue prototypeValue(cx);
    if (!JS_GetProperty(cx, ctor, "prototype", &prototypeValue) || !prototypeValue.isObject())
        return error(cx, "Socket.IO prototype is unavailable");
    prototype = &prototypeValue.toObject();
    if (JS_GetClass(prototype) != &clientClass)
        return error(cx, "Invalid Socket.IO factory receiver");
    const auto state = JS_GetReservedSlot(prototype, ManagerSlot);
    auto* holder = state.isDouble() ? static_cast<std::shared_ptr<Manager>*>(state.toPrivate()) : nullptr;
    if (!holder || !(*holder)->alive) return error(cx, "Socket.IO runtime is unavailable");
    JS::RootedObject wrapper(cx, JS_NewObject(cx, &clientClass, prototype, JS::NullPtr()));
    JS::RootedObject handlers(cx, JS_NewObjectWithGivenProto(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!wrapper || !handlers) return false;
    auto client = std::make_shared<Client>();
    client->manager = *holder;
    client->authority = uri.getAuthority();
    client->endpoint = uri.getPath() == "/" ? "" : std::string(uri.getPath());
    client->query = uri.getQuery();
    client->secure = uri.isSecure();
    client->ca = std::move(ca);
    JS_SetReservedSlot(wrapper, HandlerSlot, JS::ObjectValue(*handlers));
    JS_SetPrivate(wrapper, new std::shared_ptr<Client>(client));
    client->active = std::make_unique<JS::PersistentRootedObject>(cx, wrapper);
    (*holder)->clients.emplace_back(client);
    if (!property(cx, wrapper, "tag", "")) return false;
    client->begin();
    args.rval().setObject(*wrapper);
    return true;
}
bool on(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto client = selfClient(cx, args);
    if (!client) return false;
    std::string name;
    if (argc != 2 || !argument(cx, args, 0, name) || !args[1].isObject() ||
        !JS_ObjectIsCallable(cx, &args[1].toObject())) return error(cx, "on requires an event name and a function");
    JS::RootedValue handlers(cx, JS_GetReservedSlot(&args.thisv().toObject(), HandlerSlot));
    JS::RootedObject table(cx, &handlers.toObject());
    JS::RootedValue handler(cx, args[1]);
    if (!property(cx, table, name.c_str(), handler)) return false;
    args.rval().set(args.thisv());
    return true;
}
bool emitPacket(JSContext* cx, const JS::CallArgs& args, bool message)
{
    auto client = selfClient(cx, args);
    if (!client) return false;
    if (args.length() < (message ? 1u : 2u)) return error(cx, "Socket.IO send/emit requires string arguments");
    std::string name = "message";
    if (!message && !argument(cx, args, 0, name)) return false;
    std::vector<std::string> values;
    for (unsigned i = message ? 0u : 1u; i < args.length(); ++i)
    {
        std::string value;
        if (!argument(cx, args, i, value)) return false;
        values.emplace_back(std::move(value));
    }
    if (!client->connected)
    {
        client->event("error", "Client not yet connected", &args.thisv().toObject());
        args.rval().setUndefined();
        return true;
    }
    std::string wire;
    if (message && !client->version1)
        wire = "3::" + client->endpoint + ":" + values.front();
    else
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        if (client->version1) writer.StartArray();
        else { writer.StartObject(); writer.Key("name"); }
        writer.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
        if (!client->version1) { writer.Key("args"); writer.StartArray(); }
        for (const auto& value : values)
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        writer.EndArray();
        if (!client->version1) writer.EndObject();
        wire = client->version1 ? "42" + (client->endpoint.empty() ? "" : client->endpoint + ",")
                                : "5::" + client->endpoint + ":";
        wire.append(buffer.GetString(), buffer.GetSize());
    }
    client->sendPacket(wire);
    args.rval().setUndefined();
    return true;
}
bool send(JSContext* cx, unsigned argc, JS::Value* vp)
{
    return emitPacket(cx, JS::CallArgsFromVp(argc, vp), true);
}
bool emit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    return emitPacket(cx, JS::CallArgsFromVp(argc, vp), false);
}
bool disconnect(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto client = selfClient(cx, args);
    if (!client) return false;
    if (argc) return error(cx, "disconnect takes no arguments");
    if (!client->stopped)
    {
        client->sendPacket(client->version1 ? "41" + (client->endpoint.empty() ? "" : client->endpoint + ",")
                                            : "0::" + client->endpoint);
        if (client->socket) client->socket->closeAsync();
        client->disconnected();
    }
    args.rval().setUndefined();
    return true;
}
bool close(JSContext* cx, unsigned argc, JS::Value* vp)
{
    // Cocos2d-x v3 kept SocketIO.close() as a backwards-compatible no-op.
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc) return error(cx, "SocketIO.close takes no arguments");
    args.rval().setUndefined();
    return true;
}
const JSFunctionSpec methods[] = {JS_FN("on", on, 2, 0), JS_FN("send", send, 1, 0),
    JS_FN("emit", emit, 2, 0), JS_FN("disconnect", disconnect, 0, 0), JS_FS_END};
const JSFunctionSpec statics[] = {JS_FN("connect", connect, 1, 0), JS_FN("close", close, 0, 0), JS_FS_END};
}  // namespace
#endif

bool registerSocketIO(JSContext* cx, JS::HandleObject global)
{
#if AX_ENABLE_HTTP && AX_ENABLE_WEBSOCKET
    auto state = std::make_shared<Manager>();
    state->vm = runtimeState(cx);
    auto* runtime = engine(cx);
    if (!runtime) return false;
    runtime->addCleanupCallback([state] { state->shutdown(); });
    runtime->addUpdateCallback([state](float) { state->update(); });
    JS::RootedObject prototype(cx, JS_InitClass(cx, global, JS::NullPtr(), &clientClass, constructor, 0,
                                               nullptr, methods, nullptr, statics));
    if (!prototype) return false;
    JS_SetReservedSlot(prototype, ManagerSlot, JS::PrivateValue(new std::shared_ptr<Manager>(state)));
    JS::RootedValue constructorValue(cx);
    if (!JS_GetProperty(cx, global, "SocketIO", &constructorValue)) return false;
    return property(cx, global, "io", constructorValue);
#else
    return true;
#endif
}
}  // namespace axjs
