/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsEngine.h"
#include "runtime/JsRuntime.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/base/Logging.h"
#include "axmol/scene/Node.h"
#include "axmol/2d/Action.h"
#include <jsfriendapi.h>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#if defined(AX_ENABLE_JS_DEBUGGER) && AX_ENABLE_JS_DEBUGGER
# include "debugger/Debugger.h"
#endif

namespace ax
{
namespace
{
JsEngine* singleton{};
struct SpiderMonkeyLibrary
{
    bool initialized{JS_Init()};
    ~SpiderMonkeyLibrary() { if(initialized) JS_ShutDown(); }
};
SpiderMonkeyLibrary& library() { static SpiderMonkeyLibrary instance; return instance; }
const JSClass globalClass={
    "AxmolGlobal", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, nullptr,
    nullptr, nullptr, nullptr, JS_GlobalObjectTraceHook
};
std::string bytecodePath(std::string_view filename)
{
    std::string path(filename);
    auto slash=path.find_last_of("/\\");
    auto dot=path.find_last_of('.');
    if(dot!=std::string::npos && (slash==std::string::npos || dot>slash)) path.resize(dot);
    return path+".jsc";
}
std::string sourcePath(std::string_view filename)
{
    std::string path(filename);
    if(path.ends_with(".jsc")) path.replace(path.size()-4,4,".js");
    return path;
}
}

struct JsEngine::Impl
{
    JSRuntime* runtime{};
    JSContext* context{};
    JSCompartment* oldCompartment{};
    std::unique_ptr<JS::PersistentRootedObject> global;
    std::shared_ptr<axjs::RuntimeState> state;
    uint64_t generation{};
    bool fromScript{};
    bool stopping{};
    std::vector<RegisterCallback> registration;
    std::vector<std::function<void()>> cleanup;
    std::vector<std::shared_ptr<std::function<void(float)>>> updates;
    std::unordered_map<std::string,std::unique_ptr<JS::PersistentRootedScript>> scripts;
    std::vector<std::pair<Object*,std::string>> dispatchStack;
    std::unordered_map<std::string,JSNative> lifecycleNatives;
    std::unordered_set<ScriptEventType> nativeHandledEvents;
    ErrorHandler errors;
};
JsEngine::JsEngine():_impl(std::make_unique<Impl>()) {}
JsEngine::~JsEngine()
{
    stop();
    if(singleton==this) singleton=nullptr;
}
JsEngine* JsEngine::getInstance()
{
    if(!singleton) singleton=new JsEngine;
    return singleton;
}
void JsEngine::addRegisterCallback(RegisterCallback callback)
{
    if(callback && std::find(_impl->registration.begin(),_impl->registration.end(),callback)==_impl->registration.end())
        _impl->registration.push_back(callback);
}
bool JsEngine::isRunning() const { return _impl->context && _impl->state && _impl->state->alive; }
bool JsEngine::isOwnerThread() const { return _impl->state && _impl->state->onOwnerThread(); }
uint64_t JsEngine::getGeneration() const { return _impl->generation; }
JSContext* JsEngine::getContext() const { return _impl->context; }
JSRuntime* JsEngine::getRuntime() const { return _impl->runtime; }
JSObject* JsEngine::getGlobalObject() const { return _impl->global ? _impl->global->get() : nullptr; }
std::shared_ptr<axjs::RuntimeState> JsEngine::getRuntimeState() const { return _impl->state; }

bool JsEngine::start()
{
    if(isRunning()) return isOwnerThread();
    if(_impl->context || _impl->stopping || !library().initialized) return false;
    _impl->state=std::make_shared<axjs::RuntimeState>();
    _impl->state->engine=this;
    _impl->state->generation=++_impl->generation;
    _impl->state->owner=std::this_thread::get_id();
    _impl->runtime=JS_NewRuntime(32U*1024U*1024U);
    if(!_impl->runtime) { _impl->state.reset(); return false; }
    JS_SetNativeStackQuota(_impl->runtime,1024U*1024U);
    JS_SetGCParameter(_impl->runtime,JSGC_MAX_BYTES,256U*1024U*1024U);
#if defined(AX_JS_SPIDERMONKEY_JIT) && AX_JS_SPIDERMONKEY_JIT
    JS::RuntimeOptionsRef(_impl->runtime).setIon(true).setBaseline(true);
#else
    JS::RuntimeOptionsRef(_impl->runtime).setIon(false).setBaseline(false);
#endif
    _impl->context=JS_NewContext(_impl->runtime,32U*1024U);
    if(!_impl->context) { JS_DestroyRuntime(_impl->runtime); _impl->runtime=nullptr; _impl->state.reset(); return false; }
    _impl->state->context=_impl->context;
    _impl->state->alive=true;
    JS_SetContextPrivate(_impl->context,_impl->state.get());
    JS_SetErrorReporter(_impl->context,[](JSContext* cx,const char* message,JSErrorReport* report) {
        auto* self=axjs::engine(cx);
        std::string text;
        if(report && report->filename) text=std::string(report->filename)+":"+std::to_string(report->lineno)+": ";
        text+=message ? message : "JavaScript error";
        if(report && (report->flags & JSREPORT_WARNING)) { AXLOGW("{}",text); return; }
        if(self && self->_impl->errors) self->_impl->errors(text);
        else AXLOGE("{}",text);
    });
    JS::CompartmentOptions options;
    options.setVersion(JSVERSION_LATEST);
    bool initialized=false;
    {
        JS::RootedObject global(_impl->context,JS_NewGlobalObject(_impl->context,&globalClass,nullptr,JS::DontFireOnNewGlobalHook,options));
        if(global)
        {
            _impl->global=std::make_unique<JS::PersistentRootedObject>(_impl->context,global);
            _impl->oldCompartment=JS_EnterCompartment(_impl->context,global);
            js::SetDefaultObjectForContext(_impl->context,global);
            initialized=axjs::detail::createRegistry(_impl->context) &&
                JS_InitStandardClasses(_impl->context,global) && JS_InitReflect(_impl->context,global);
            if(initialized)
            {
                JS_FireOnNewGlobalObject(_impl->context,global);
                for(auto callback:_impl->registration)
                    if(!callback(_impl->context,global)) { initialized=false; break; }
            }
        }
    }
    // Rooted locals must leave their scope before destroying the context.
    if(!initialized) { reportException(); stop(); return false; }
    return true;
}
void JsEngine::stop()
{
    if(!_impl->context || _impl->stopping) return;
    if(!isOwnerThread())
    {
        // JS runtime destruction is thread-affine; the director frame hook performs
        // requests from native worker threads before their next callback.
        auto state=_impl->state;
        state->enqueue([this,state] { if(_impl->state==state) stop(); });
        return;
    }
    _impl->stopping=true;
    _impl->state->alive=false;
    auto cleanup=std::move(_impl->cleanup);
    for(auto it=cleanup.rbegin();it!=cleanup.rend();++it) (*it)();
    _impl->updates.clear();
    _impl->state->drain();
    axjs::detail::releaseCallbackRoots(_impl->context);
    axjs::detail::destroyRegistry(_impl->context);
    _impl->scripts.clear();
    _impl->dispatchStack.clear();
    _impl->nativeHandledEvents.clear();
    _impl->global.reset();
    if(_impl->oldCompartment) JS_LeaveCompartment(_impl->context,_impl->oldCompartment);
    _impl->oldCompartment=nullptr;
    JS_GC(_impl->runtime);
    // A foreign thread may release its last callback while teardown resets
    // the remaining roots. Drain that ownership transfer before destruction.
    _impl->state->drain();
    JS_SetContextPrivate(_impl->context,nullptr);
    JS_DestroyContext(_impl->context);
    _impl->context=nullptr;
    JS_DestroyRuntime(_impl->runtime);
    _impl->runtime=nullptr;
    _impl->state->context=nullptr;
    _impl->state->engine=nullptr;
    _impl->fromScript=false;
    _impl->stopping=false;
}
bool JsEngine::restart() { if(_impl->context && !isOwnerThread()) return false; stop(); return start(); }
void JsEngine::drainDeferredReleases() { if(_impl->state) _impl->state->drain(); }
void JsEngine::updateFrame(float delta) { update(delta); }
void JsEngine::update(float delta)
{
    if(!isRunning() || !isOwnerThread()) return;
    auto state=_impl->state;
    state->drain();
    if(!state->alive || state!=_impl->state) return;
    axjs::detail::collectExpiredNativeHandles(_impl->context);
    auto callbacks=_impl->updates;
    for(auto& callback:callbacks)
    { if(!state->alive || state!=_impl->state) break; (*callback)(delta); }
}
void JsEngine::addCleanupCallback(std::function<void()> callback) { if(callback) _impl->cleanup.push_back(std::move(callback)); }
void JsEngine::addUpdateCallback(std::function<void(float)> callback)
{ if(callback) _impl->updates.push_back(std::make_shared<std::function<void(float)>>(std::move(callback))); }
bool JsEngine::enableDebugger(uint16_t port,bool waitForConnection,std::string_view bindAddress)
{
#if defined(AX_ENABLE_JS_DEBUGGER) && AX_ENABLE_JS_DEBUGGER
    if(!isRunning() || !isOwnerThread()) return false;
    JS::RootedObject global(_impl->context,getGlobalObject());
    axjs::debugger::Options options;
    options.port=port; options.waitForConnection=waitForConnection; options.bindAddress=std::string(bindAddress);
    if(!axjs::debugger::initialize(_impl->context,global,options)) return false;
    addUpdateCallback([](float){ axjs::debugger::pump(); });
    addCleanupCallback([]{ axjs::debugger::shutdown(); });
    return true;
#else
    AXLOGE("JavaScript debugger was disabled in this build");
    return false;
#endif
}
void JsEngine::setErrorHandler(ErrorHandler handler) { _impl->errors=std::move(handler); }
void JsEngine::reportException()
{
    if(!_impl->context || !isOwnerThread() || !JS_IsExceptionPending(_impl->context)) return;
    JS::RootedValue exception(_impl->context);
    if(!JS_GetPendingException(_impl->context,&exception)) return;
    JS_ClearPendingException(_impl->context);
    std::string text="JavaScript exception";
    JS::RootedString string(_impl->context,JS::ToString(_impl->context,exception));
    if(string)
    {
        JS::RootedValue value(_impl->context,JS::StringValue(string));
        axjs::fromJsValue(_impl->context,value,text);
    }
    if(exception.isObject())
    {
        JS::RootedObject object(_impl->context,&exception.toObject());
        JS::RootedValue stack(_impl->context);
        if(JS_GetProperty(_impl->context,object,"stack",&stack) && stack.isString())
        {
            std::string trace;
            if(axjs::fromJsValue(_impl->context,stack,trace) && !trace.empty()) text+="\n"+trace;
        }
    }
    JS_ClearPendingException(_impl->context);
    if(_impl->errors) _impl->errors(text); else AXLOGE("{}",text);
}
bool JsEngine::evalString(std::string_view code,std::string_view filename)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JS::RootedValue result(_impl->context);
    return evalString(code,&result,filename);
}
bool JsEngine::evalString(std::string_view code,JS::MutableHandleValue result,std::string_view filename)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JSAutoCompartment compartment(_impl->context,getGlobalObject());
    JS::RootedObject global(_impl->context,getGlobalObject());
    const std::string name(filename);
    JS::CompileOptions options(_impl->context);
    options.setUTF8(true).setFileAndLine(name.c_str(),1);
    const bool ok=JS::Evaluate(_impl->context,global,options,code.data(),code.size(),result);
    if(!ok) reportException();
    return ok;
}
bool JsEngine::runScript(std::string_view filename)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JS::RootedValue result(_impl->context);
    return runScript(filename,&result);
}
bool JsEngine::runScript(std::string_view filename,JS::MutableHandleValue result)
{
    if(!isRunning() || !isOwnerThread() || filename.empty()) return false;
    JSAutoCompartment compartment(_impl->context,getGlobalObject());
    JS::RootedObject global(_impl->context,getGlobalObject());
    auto* files=FileUtils::getInstance();
    const std::string bytePath=bytecodePath(filename);
    const std::string source=sourcePath(filename);
    // A logical source path is stable across resource packaging and is the
    // filename visible to the debugger. Full paths only identify cache entries.
    std::string key=files->fullPathForFilename(bytePath);
    if(key.empty()) key=files->fullPathForFilename(source);
    if(key.empty()) key=source;
    JS::RootedScript script(_impl->context);
    if(auto it=_impl->scripts.find(key);it!=_impl->scripts.end()) script=it->second->get();
    else
    {
        if(files->isFileExist(bytePath))
        {
            auto bytes=files->getDataFromFile(bytePath);
            if(!bytes.isNull() && bytes.getSize()<=UINT32_MAX)
                script=JS_DecodeScript(_impl->context,bytes.getBytes(),static_cast<uint32_t>(bytes.getSize()),nullptr);
            if(!script) JS_ClearPendingException(_impl->context);
        }
        if(!script)
        {
            if(!files->isFileExist(source))
            { JS_ReportError(_impl->context,"Cannot load script %s: no compatible bytecode or source",source.c_str()); reportException(); return false; }
            const auto bytes=files->getDataFromFile(source);
            JS::CompileOptions options(_impl->context);
            options.setUTF8(true).setFileAndLine(source.c_str(),1);
            if(bytes.isNull() || !JS::Compile(_impl->context,global,options,
                reinterpret_cast<const char*>(bytes.getBytes()),bytes.getSize(),&script))
            { reportException(); return false; }
        }
        _impl->scripts.emplace(key,std::make_unique<JS::PersistentRootedScript>(_impl->context,script));
    }
    const bool ok=JS_ExecuteScript(_impl->context,global,script,result);
    if(!ok) reportException();
    return ok;
}
bool JsEngine::compileBytecode(std::string_view filename,std::string_view outputFilename)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JSAutoCompartment compartment(_impl->context,getGlobalObject());
    JS::RootedObject global(_impl->context,getGlobalObject());
    JS::RootedScript script(_impl->context);
    const std::string source(filename);
    const auto bytes=FileUtils::getInstance()->getDataFromFile(filename);
    JS::CompileOptions options(_impl->context);
    options.setUTF8(true).setFileAndLine(source.c_str(),1);
    if(bytes.isNull() || !JS::Compile(_impl->context,global,options,
        reinterpret_cast<const char*>(bytes.getBytes()),bytes.getSize(),&script))
    { reportException(); return false; }
    uint32_t length{};
    void* encoded=JS_EncodeScript(_impl->context,script,&length);
    if(!encoded) { reportException(); return false; }
    Data data; data.copy(static_cast<unsigned char*>(encoded),length);
    JS_free(_impl->context,encoded);
    return FileUtils::getInstance()->writeDataToFile(data,outputFilename);
}
void JsEngine::cleanScript(std::string_view filename)
{
    if(!isOwnerThread()) return;
    auto* files=FileUtils::getInstance();
    _impl->scripts.erase(files->fullPathForFilename(bytecodePath(filename)));
    _impl->scripts.erase(files->fullPathForFilename(sourcePath(filename)));
    _impl->scripts.erase(std::string(filename));
}
void JsEngine::cleanAllScripts() { if(isOwnerThread()) _impl->scripts.clear(); }
int JsEngine::executeString(const char* codes) { return codes && evalString(codes) ? 0 : 1; }
int JsEngine::executeScriptFile(const char* filename) { return filename && runScript(filename) ? 0 : 1; }
int JsEngine::executeGlobalFunction(const char* name)
{
    if(!isRunning() || !isOwnerThread() || !name) return -1;
    JSAutoCompartment compartment(_impl->context,getGlobalObject());
    JS::RootedObject global(_impl->context,getGlobalObject());
    JS::RootedValue result(_impl->context);
    if(!JS_CallFunctionName(_impl->context,global,name,JS::HandleValueArray::empty(),&result))
    { reportException(); return -1; }
    int value{};
    return axjs::fromJsValue(_impl->context,result,value) ? value : 0;
}
void JsEngine::setCalledFromScript(bool value) { _impl->fromScript=value; }
bool JsEngine::isCalledFromScript() { return _impl->fromScript; }
bool JsEngine::handleAssert(const char* message)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JS_ReportError(_impl->context,"Native assertion: %s",message ? message : "");
    reportException(); return true;
}
bool JsEngine::parseConfig(ConfigType type,std::string_view str)
{
    if(!isRunning() || !isOwnerThread()) return false;
    JSAutoCompartment compartment(_impl->context,getGlobalObject());
    JS::RootedObject global(_impl->context,getGlobalObject());
    JS::AutoValueVector values(_impl->context);
    JS::RootedValue number(_impl->context,JS::Int32Value(static_cast<int>(type))),text(_impl->context),result(_impl->context);
    if(!axjs::toJsValue(_impl->context,str,&text) || !values.append(number) || !values.append(text)) return false;
    const bool ok=JS_CallFunctionName(_impl->context,global,"__onParseConfig",JS::HandleValueArray(values),&result);
    if(!ok) reportException(); return ok;
}
void JsEngine::invalidateScriptObject(Object* object)
{
    if(!_impl->context || !object) return;
    if(isOwnerThread()) axjs::detail::invalidateNative(_impl->context,object);
    else
    {
        // Axmol Objects are thread-affine. Nevertheless, a foreign destructor
        // may invalidate a JS handle without touching JSAPI or retaining it.
        auto state=_impl->state;
        state->enqueue([this,state,object] {
            if(state==_impl->state && state->alive) axjs::detail::invalidateExpiredNative(_impl->context,object);
        });
    }
}
void JsEngine::rootObject(Object* object) { (void)object; }
void JsEngine::unrootObject(Object* object) { (void)object; }
void JsEngine::garbageCollect()
{
    if(!isRunning() || !isOwnerThread()) return;
    drainDeferredReleases(); axjs::detail::collectExpiredNativeHandles(_impl->context); JS_GC(_impl->runtime);
}
bool JsEngine::callNativeMethod(Object* object,const char* method,const JS::HandleValueArray& args,
                               JS::MutableHandleValue result,bool* found)
{
    if(found) *found=false;
    if(!isRunning() || !isOwnerThread()) return false;
    JS::RootedObject wrapper(_impl->context,axjs::detail::findNativeObject(_impl->context,object));
    if(!wrapper) return true;
    JSAutoCompartment compartment(_impl->context,wrapper);
    JS::RootedValue function(_impl->context);
    if(!JS_GetProperty(_impl->context,wrapper,method,&function)) { reportException(); return false; }
    if(!function.isObject() || !JS_ObjectIsCallable(_impl->context,&function.toObject())) return true;
    if(found) *found=true;
    const bool ok=JS_CallFunctionValue(_impl->context,wrapper,function,args,result);
    if(!ok) reportException(); return ok;
}
bool JsEngine::dispatchNodeLifecycle(Node* node,const char* method)
{
    if(!isRunning() || !isOwnerThread() || !node || !method) return false;
    const auto key=std::pair<Object*,std::string>(node,method);
    if(std::find(_impl->dispatchStack.begin(),_impl->dispatchStack.end(),key)!=_impl->dispatchStack.end()) return false;
    JSContext* cx=_impl->context;
    JS::RootedObject wrapper(cx,axjs::detail::findNativeObject(cx,node));
    if(!wrapper) return false;
    JSAutoCompartment compartment(cx,wrapper);
    JS::RootedValue function(cx);
    if(!JS_GetProperty(cx,wrapper,method,&function)) { reportException(); return true; }
    if(!function.isObject() || !JS_ObjectIsFunction(cx,&function.toObject())) return false;
    auto sentinel=_impl->lifecycleNatives.find(method);
    if(sentinel!=_impl->lifecycleNatives.end() && JS_IsNativeFunction(&function.toObject(),sentinel->second)) return false;
    _impl->dispatchStack.push_back(key);
    JS::RootedValue result(cx);
    const bool ok=JS_CallFunctionValue(cx,wrapper,function,JS::HandleValueArray::empty(),&result);
    _impl->dispatchStack.pop_back();
    if(!ok) reportException();
    return true;
}
bool JsEngine::dispatchNodeUpdate(Node* node,float delta)
{
    if(!isRunning() || !isOwnerThread() || !node) return false;
    const auto key=std::pair<Object*,std::string>(node,"update");
    if(std::find(_impl->dispatchStack.begin(),_impl->dispatchStack.end(),key)!=_impl->dispatchStack.end()) return false;
    JSContext* cx=_impl->context;
    JS::RootedObject wrapper(cx,axjs::detail::findNativeObject(cx,node));
    if(!wrapper) return false;
    JSAutoCompartment compartment(cx,wrapper);
    JS::RootedValue function(cx);
    if(!JS_GetProperty(cx,wrapper,"update",&function)) { reportException(); return true; }
    if(!function.isObject() || !JS_ObjectIsFunction(cx,&function.toObject())) return false;
    auto sentinel=_impl->lifecycleNatives.find("update");
    if(sentinel!=_impl->lifecycleNatives.end() && JS_IsNativeFunction(&function.toObject(),sentinel->second)) return false;
    _impl->dispatchStack.push_back(key);
    JS::RootedValue arg(cx,JS::DoubleValue(JS::CanonicalizeNaN(delta))),result(cx);
    const bool ok=JS_CallFunctionValue(cx,wrapper,function,JS::HandleValueArray(arg),&result);
    _impl->dispatchStack.pop_back();
    if(!ok) reportException();
    return true;
}
void JsEngine::setNodeLifecycleNative(const char* method,JSNative function)
{ if(method && function) _impl->lifecycleNatives[method]=function; }
void JsEngine::setEventHandledByNative(ScriptEventType type)
{ if(isRunning() && isOwnerThread()) _impl->nativeHandledEvents.insert(type); }
bool JsEngine::dispatchActionUpdate(Action* action,float normalizedTime)
{
    if(!isRunning() || !isOwnerThread() || !action) return false;
    const auto key=std::pair<Object*,std::string>(action,"update");
    if(std::find(_impl->dispatchStack.begin(),_impl->dispatchStack.end(),key)!=_impl->dispatchStack.end()) return false;
    JSContext* cx=_impl->context;
    JS::RootedObject wrapper(cx,axjs::detail::findNativeObject(cx,action));
    if(!wrapper) return false;
    JSAutoCompartment compartment(cx,wrapper);
    JS::RootedValue function(cx),nativeMethod(cx),wrapped(cx,JS::ObjectValue(*wrapper));
    if(!JS_GetProperty(cx,wrapper,"update",&function)) { reportException(); return true; }
    if(!function.isObject() || !JS_ObjectIsCallable(cx,&function.toObject())) return false;
    JS::RootedObject prototype(cx,axjs::detail::getNativePrototype(cx,wrapped));
    if(prototype)
    {
        if(!JS_GetProperty(cx,prototype,"update",&nativeMethod)) { reportException(); return true; }
        if(nativeMethod.isObject() && &nativeMethod.toObject()==&function.toObject()) return false;
    }
    _impl->dispatchStack.push_back(key);
    JS::RootedValue argument(cx,JS::DoubleValue(JS::CanonicalizeNaN(normalizedTime))),result(cx);
    const bool ok=JS_CallFunctionValue(cx,wrapper,function,JS::HandleValueArray(argument),&result);
    _impl->dispatchStack.pop_back();
    if(!ok) reportException();
    return true;
}
bool JsEngine::invokeNodeBase(Node* node,std::string_view method,float delta)
{
    if(!isRunning() || !isOwnerThread() || !node) return false;
    _impl->dispatchStack.emplace_back(node,std::string(method));
    bool recognized=true;
    if(method=="onEnter") node->Node::onEnter();
    else if(method=="onExit") node->Node::onExit();
    else if(method=="onEnterTransitionDidFinish") node->Node::onEnterTransitionDidFinish();
    else if(method=="onExitTransitionDidStart") node->Node::onExitTransitionDidStart();
    else if(method=="cleanup") node->Node::cleanup();
    else if(method=="update") node->Node::update(delta);
    else recognized=false;
    _impl->dispatchStack.pop_back();
    return recognized;
}
int JsEngine::sendEvent(const ScriptEvent& event)
{
    if(event.type==kRestartGame) return restart() ? 0 : 1;
    if(!isRunning() || !isOwnerThread() || !event.data) return 0;
    if(_impl->nativeHandledEvents.contains(event.type)) return 1;
    JS::RootedValue result(_impl->context);
    if(event.type==kScheduleEvent)
    {
        const auto* data=static_cast<SchedulerScriptData*>(event.data);
        return dispatchNodeUpdate(static_cast<Node*>(data->node),data->elapse) ? 1 : 0;
    }
    if(event.type==kMenuClickedEvent || event.type==kCallFuncEvent)
    {
        const auto* data=static_cast<BasicScriptData*>(event.data);
        JS::RootedValue arg(_impl->context);
        if(data->value && !axjs::wrapNative(_impl->context,static_cast<Object*>(data->value),&arg)) return 0;
        bool found=false;
        return callNativeMethod(static_cast<Object*>(data->nativeObject),"__callback",JS::HandleValueArray(arg),&result,&found) && found;
    }
    // Native event-listener bindings call their rooted Callback directly;
    // unsupported legacy protocol events are explicitly reported.
    AXLOGW("Unsupported legacy JS script event: {}",static_cast<int>(event.type));
    return 0;
}
} // namespace ax
