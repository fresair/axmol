/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "SocketIOFixture.h"
#include "manual/SocketIOBindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/platform/FileUtils.h"
#include <sys/stat.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;
const char* currentCase = "startup";
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils {
public:
    NativeFiles() { _defaultResRootPath = "/data/local/tmp/axmol-js-sm33/"; check(init(), "Socket.IO native file init"); }
    std::string getWritablePath() const override { return _defaultResRootPath; }
    std::string getNativeWritableAbsolutePath() const override { return _defaultResRootPath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat result{};
        return stat(std::string(path).c_str(), &result) == 0 && S_ISREG(result.st_mode);
    }
};
#endif
void pump()
{
    // Mirror Director::renderFrame(): the script engine's frame entry is owned
    // by the Director, so the headless driver must invoke it explicitly.
    ax::Director::getInstance()->getScheduler()->update(0.005F);
    if (auto* script = ax::ScriptEngineManager::getScriptEngineIfExists())
        script->updateFrame(0.005F);
    std::this_thread::sleep_for(3ms);
}
void eval(ax::JsEngine* engine, std::string_view source)
{
    check(engine->evalString(source), std::string("Socket.IO JavaScript assertion failed: ") + currentCase);
}
void set(ax::JsEngine* engine, const char* name, const std::string& text)
{
    auto* cx = engine->getContext();
    JS::RootedObject global(cx, engine->getGlobalObject()); JS::RootedValue value(cx);
    check(axjs::toJsValue(cx, text, &value) && JS_SetProperty(cx, global, name, value), "Socket.IO test global value");
}
void waitFor(ax::JsEngine* engine, const std::vector<std::string>& errors, std::string_view expression)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;)
    {
        pump(); check(errors.empty(), errors.empty() ? "" : errors.back());
        JS::RootedValue result(engine->getContext());
        check(engine->evalString(expression, &result), "Socket.IO result evaluation failed");
        if (JS::ToBoolean(result)) return;
        check(std::chrono::steady_clock::now() < deadline, std::string("Socket.IO timed out: ") + currentCase);
    }
}
template<class Predicate> void waitNative(Predicate ready)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!ready()) { pump(); check(std::chrono::steady_clock::now() < deadline, "Socket.IO server wire assertion timed out"); }
}
}
int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles());
#endif
    auto* engine = ax::JsEngine::getInstance(); std::vector<std::string> errors;
    engine->setErrorHandler([&](std::string_view value) { errors.emplace_back(value); });
    engine->addRegisterCallback(axjs::registerSocketIO);
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    try
    {
        check(engine->start(), "Socket.IO runtime start");
        eval(engine, "function expect(ok,message){if(!ok)throw Error(message);}expect(io===SocketIO,'window.io alias');");
        for (const bool version1 : {false, true})
        {
            currentCase = version1 ? "Socket.IO 1.x / Engine.IO 2 original default + endpoint API"
                                   : "Socket.IO 0.9 original default + endpoint API";
            std::cout << "Socket.IO test: " << currentCase << std::endl;
            axjs::tests::SocketIOFixture fixture(version1);
            set(engine, "fixtureUrl", fixture.url());
            eval(engine, R"JS(
                var received=0,serverEvents=0,disconnects=0,connections=0,manyEvents=0;
                var eventPayload='[{"name":"myname","type":"mytype"}]';
                function message(data){expect(this.tag==='Test Client'||this.tag==='Test Endpoint','shared callback receiver/tag');expect(data==='Hello Socket.IO!','original message data');received++;}
                function testevent(data){expect(data==='server data','server event data');serverEvents++;}
                function disconnection(){expect(this.tag==='Test Client'||this.tag==='Test Endpoint','disconnect receiver');disconnects++;}
                var sioclient=SocketIO.connect(fixtureUrl,{'force new connection':true});sioclient.tag='Test Client';
                var sioendpoint=SocketIO.connect(fixtureUrl+'/testpoint',{'force new connection':true});sioendpoint.tag='Test Endpoint';
                [sioclient,sioendpoint].forEach(function(socket){
                    expect(socket.on('message',message)===socket,'on chaining');socket.on('testevent',testevent);
                    socket.on('echotest',function(data){expect(data===eventPayload,'original echotest JSON string');received++;});
                    socket.on('many',function(data){var values=JSON.parse(data);expect(values[0]==='a'&&values[1]==='b','multiple event arguments');manyEvents++;});
                    socket.on('disconnect',disconnection);socket.on('error',function(data){throw Error('unexpected Socket.IO error '+data);});
                    socket.on('connect',function(){expect(this===socket,'connect this');connections++;this.send('Hello Socket.IO!');this.emit('echotest',eventPayload);this.emit('many','a','b');});
                });
            )JS");
            engine->garbageCollect();
            try
            {
                waitFor(engine, errors, "connections===2&&received===4&&serverEvents===2&&manyEvents===2");
            }
            catch (...)
            {
                JS::RootedValue value(engine->getContext()); std::string counts;
                if (engine->evalString("JSON.stringify([connections,received,serverEvents,manyEvents])", &value) &&
                    axjs::fromJsValue(engine->getContext(), value, counts))
                    std::cerr << "Socket.IO JS [connections,messages,serverEvents,manyEvents]: " << counts << '\n';
                std::cerr << "Socket.IO native HTTP=" << fixture.handshakeRequests
                          << " WS=" << fixture.websocketRequests << " messages=" << fixture.messages << '\n';
                for (const auto& packet : fixture.packets())
                    std::cerr << "Socket.IO client packet: " << packet << '\n';
                throw;
            }
            waitNative([&] { return fixture.heartbeatReplies >= 2; });
            check(fixture.handshakeRequests == 2 && fixture.websocketRequests == 2, "real HTTP and WebSocket handshakes for both namespaces");
            check(fixture.messages == 6, "actual send/emit packets for both namespaces");
            if (version1)
            {
                check(fixture.probes == 2 && fixture.upgrades == 2, "Engine.IO probe before upgrade");
                check(fixture.saw("40/testpoint,"), "namespace connect wire packet");
                check(fixture.saw("42/testpoint,[\"message\",\"Hello Socket.IO!\"]"), "namespace message wire packet");
            }
            else
            {
                check(fixture.coalescedUpgrades == 2, "HTTP upgrade and first Socket.IO frames sent in one server write");
                check(fixture.saw("1::/testpoint"), "0.9 namespace connect wire packet");
                check(fixture.saw("3::/testpoint:Hello Socket.IO!"), "0.9 namespace message wire packet");
            }
            eval(engine, "sioclient.disconnect();sioendpoint.disconnect();sioclient.disconnect();expect(disconnects===2,'disconnect once');var closedSendError=false;sioclient.on('error',function(data){expect(this===sioclient&&data==='Client not yet connected','closed socket error');closedSendError=true;});sioclient.send('after close');expect(closedSendError,'closed object methods still use receiver handler');SocketIO.close();");
            waitNative([&] { return fixture.disconnectPackets == 2 && fixture.closeFrames == 2; });
            engine->garbageCollect();
        }
        for (const auto mode : {axjs::tests::SocketIOFixture::Mode::MalformedHandshake,
                                axjs::tests::SocketIOFixture::Mode::RejectHandshake,
                                axjs::tests::SocketIOFixture::Mode::MalformedEvent})
        {
            currentCase = mode == axjs::tests::SocketIOFixture::Mode::MalformedHandshake ? "malformed handshake" :
                          mode == axjs::tests::SocketIOFixture::Mode::RejectHandshake ? "HTTP handshake rejection" : "malformed event";
            std::cout << "Socket.IO test: " << currentCase << std::endl;
            axjs::tests::SocketIOFixture fixture(true, mode); set(engine, "fixtureUrl", fixture.url());
            eval(engine, R"JS(
                var failed=false,closed=false,bad=SocketIO.connect(fixtureUrl);
                bad.on('error',function(data){expect(this===bad&&typeof data==='string'&&data.length>0,'transport error this/data');failed=true;});
                bad.on('disconnect',function(){expect(failed,'error before disconnect');closed=true;});
            )JS");
            waitFor(engine, errors, "failed&&closed");
            check(fixture.handshakeRequests == 1, "real rejected handshake request");
        }
        currentCase = "validation and connecting disconnect";
        std::cout << "Socket.IO test: " << currentCase << std::endl;
        {
            axjs::tests::SocketIOFixture fixture(true, axjs::tests::SocketIOFixture::Mode::DelayedHandshake);
            set(engine, "fixtureUrl", fixture.url());
            eval(engine, R"JS(
                var canceledConnects=0,canceledClose=0,canceled=SocketIO.connect(fixtureUrl);
                canceled.on('connect',function(){canceledConnects++;});canceled.on('disconnect',function(){canceledClose++;});
                canceled.disconnect();canceled.disconnect();expect(canceledClose===1,'connecting disconnect once');
                var failed=false;try{new SocketIO();}catch(e){failed=true;}expect(failed,'factory only');
                failed=false;try{SocketIO.connect('file:///tmp/a');}catch(e){failed=true;}expect(failed,'invalid scheme');
                failed=false;try{sioclient.send.call({},'bad');}catch(e){failed=true;}expect(failed,'invalid receiver');
                failed=false;try{sioclient.on('x',17);}catch(e){failed=true;}expect(failed,'invalid callback');
            )JS");
            const auto deadline = std::chrono::steady_clock::now() + 300ms; while (std::chrono::steady_clock::now() < deadline) pump();
            eval(engine, "expect(canceledConnects===0&&canceledClose===1,'canceled HTTP completion suppressed');");
            check(fixture.websocketRequests == 0, "canceled handshake must not create a WebSocket");
        }
        currentCase = "pending handshake across VM restart";
        std::cout << "Socket.IO test: " << currentCase << std::endl;
        {
            axjs::tests::SocketIOFixture fixture(true, axjs::tests::SocketIOFixture::Mode::DelayedHandshake);
            set(engine, "fixtureUrl", fixture.url());
            eval(engine, "var old=SocketIO.connect(fixtureUrl);old.on('connect',function(){throw Error('old VM callback');});old.on('error',function(){throw Error('old VM error');});");
            check(engine->restart(), "Socket.IO VM restart");
            const auto deadline = std::chrono::steady_clock::now() + 300ms; while (std::chrono::steady_clock::now() < deadline) pump();
            check(errors.empty(), errors.empty() ? "" : errors.back());
            check(fixture.websocketRequests == 0, "expired VM handshake must not create transport");
            eval(engine, "expect=function(ok,message){if(!ok)throw Error(message);};expect(io===SocketIO,'restarted registration');");
        }
        currentCase = "open transport across VM restart";
        std::cout << "Socket.IO test: " << currentCase << std::endl;
        {
            axjs::tests::SocketIOFixture fixture(true);
            set(engine, "fixtureUrl", fixture.url());
            eval(engine, R"JS(
                var opened=false,old=SocketIO.connect(fixtureUrl);
                old.on('connect',function(){opened=true;});
                old.on('message',function(){throw Error('old transport callback entered a stopped VM');});
                old.on('disconnect',function(){throw Error('VM teardown called old JS disconnect');});
                old.on('error',function(){throw Error('old transport error entered a stopped VM');});
            )JS");
            waitFor(engine, errors, "opened");
            eval(engine, "old.send('pending echo before restart');");
            check(engine->restart(), "open Socket.IO transport VM restart");
            const auto deadline = std::chrono::steady_clock::now() + 200ms;
            while (std::chrono::steady_clock::now() < deadline) pump();
            check(errors.empty(), errors.empty() ? "" : errors.back());
            check(fixture.websocketRequests == 1, "real transport existed before restart");
        }
        engine->garbageCollect(); engine->stop();
        std::cout << "JavaScript legacy Socket.IO real HTTP/WebSocket protocol tests passed\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "Socket.IO tests failed: " << failure.what() << '\n'; engine->stop(); return 1;
    }
}
