/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/ManualUtils.h"
#if AX_ENABLE_HTTP
#include "axmol/network/HttpClient.h"
#include "axmol/network/HttpResponse.h"
#include "yasio/errc.hpp"
#endif
#if AX_ENABLE_WEBSOCKET
#include "axmol/network/WebSocket.h"
#endif
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <array>
#include <limits>
#if AX_ENABLE_HTTP
#include <zlib.h>
#endif

namespace axjs {
namespace {
using namespace manual;
using Clock = std::chrono::steady_clock;
struct NetworkObject;
struct NetworkState {
    std::weak_ptr<RuntimeState> vm;
    std::atomic<bool> alive{true};
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    std::vector<std::weak_ptr<NetworkObject>> objects;
    void post(std::function<void()> fn) {
        if(!alive.load())return;
        std::lock_guard lock(mutex);
        if(alive.load())pending.emplace_back(std::move(fn));
    }
    void update();
    void shutdown();
};
struct NetworkObject : std::enable_shared_from_this<NetworkObject> {
    std::weak_ptr<NetworkState> state;
    std::unique_ptr<JS::PersistentRootedObject> activeRoot;
    std::unique_ptr<JS::PersistentRootedObject> retainedRoot;
    unsigned retainCount{};
    virtual ~NetworkObject() = default;
    virtual void shutdown() { activeRoot.reset();retainedRoot.reset();retainCount=0; }
    virtual void update() {}
    JSContext* context() const {
        auto network=state.lock();auto runtime=network?network->vm.lock():nullptr;
        return network && network->alive && runtime && runtime->alive && runtime->onOwnerThread()?runtime->context:nullptr;
    }
    void emit(const char* name, const std::function<bool(JSContext*,JS::HandleObject)>& fill = {}) {
        auto* cx=context();if(!cx||!activeRoot)return;
        JSAutoRequest request(cx);
        JS::RootedObject receiver(cx,activeRoot->get());JSAutoCompartment compartment(cx,receiver);
        JS::RootedValue fn(cx);
        const std::string handler="on"+std::string(name);
        if(!JS_GetProperty(cx,receiver,handler.c_str(),&fn)) { engine(cx)->reportException();return; }
        if(!fn.isObject()||!JS_ObjectIsCallable(cx,&fn.toObject()))return;
        JS::RootedObject event(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
        JS::RootedValue target(cx,JS::ObjectValue(*receiver));
        if(!event||!property(cx,event,"type",name)||!property(cx,event,"target",target)||
           !property(cx,event,"currentTarget",target)||(fill&&!fill(cx,event))) { engine(cx)->reportException();return; }
        JS::RootedValue argument(cx,JS::ObjectValue(*event)), result(cx);
        if(!JS_CallFunctionValue(cx,receiver,fn,JS::HandleValueArray(argument),&result))engine(cx)->reportException();
    }
};
void NetworkState::update() {
    auto runtime=vm.lock();if(!alive||!runtime||!runtime->alive||!runtime->onOwnerThread())return;
    std::deque<std::function<void()>> jobs;
    {std::lock_guard lock(mutex);jobs.swap(pending);}
    for(auto& job:jobs){if(!alive||!runtime->alive)break;job();}
    if(!alive||!runtime->alive)return;
    std::vector<std::shared_ptr<NetworkObject>> current;
    std::erase_if(objects,[&](const auto& entry){if(auto object=entry.lock()){current.push_back(std::move(object));return false;}return true;});
    for(auto& object:current){if(!alive||!runtime->alive)break;object->update();}
}
void NetworkState::shutdown() {
    alive=false;
    std::deque<std::function<void()>> jobs;
    {std::lock_guard lock(mutex);jobs.swap(pending);}
    std::vector<std::shared_ptr<NetworkObject>> current;
    for(auto& entry:objects)if(auto object=entry.lock())current.push_back(std::move(object));
    for(auto& object:current)object->shutdown();
    objects.clear();
}
// Each constructor receives its own VM's manager through a reserved prototype
// slot. No process-global context or prototype survives a VM restart.
constexpr unsigned StateSlot=0;
std::shared_ptr<NetworkState> stateFor(JSContext* cx,JS::HandleObject object) {
    JS::RootedObject prototype(cx);
    if(!JS_GetPrototype(cx,object,&prototype)||!prototype||JSCLASS_RESERVED_SLOTS(JS_GetClass(prototype))<=StateSlot)return {};
    auto value=JS_GetReservedSlot(prototype,StateSlot);
    if(!value.isDouble())return {};
    auto* holder=static_cast<std::shared_ptr<NetworkState>*>(value.toPrivate());
    return holder?*holder:nullptr;
}
template<class T> std::shared_ptr<T> selfObject(JSContext* cx,const JS::CallArgs& args,const JSClass* klass) {
    if(!args.thisv().isObject()) {error(cx,"Invalid network receiver");return {};}
    JS::RootedObject object(cx,&args.thisv().toObject());
    if(JS_GetClass(object)!=klass){error(cx,"Invalid network receiver");return {};}
    auto* holder=static_cast<std::shared_ptr<T>*>(JS_GetPrivate(object));
    if(!holder||!(*holder)->context()){error(cx,"Network object belongs to a stopped VM");return {};}
    return *holder;
}
template<class T> void finalize(JSFreeOp*,JSObject* object) {
    delete static_cast<std::shared_ptr<T>*>(JS_GetPrivate(object));
    auto value=JS_GetReservedSlot(object,StateSlot);
    if(value.isDouble())delete static_cast<std::shared_ptr<NetworkState>*>(value.toPrivate());
}
bool text(JSContext* cx,JS::HandleValue value,std::string& out) {
    JS::RootedString string(cx,JS::ToString(cx,value));if(!string)return false;
    JS::RootedValue converted(cx,JS::StringValue(string));return fromJsValue(cx,converted,out);
}
std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;
}
bool bytes(JSContext* cx,JS::HandleValue value,std::string& out) {
    if(value.isNullOrUndefined()){out.clear();return true;}
    if(value.isString())return fromJsValue(cx,value,out);
    if(!value.isObject())return error(cx,"send expects a string, ArrayBuffer or typed array");
    JS::RootedObject object(cx,&value.toObject());
    if(JS_IsArrayBufferObject(object)) {
        const auto size=JS_GetArrayBufferByteLength(object);const auto* data=JS_GetArrayBufferData(object);
        if(size&&!data)return error(cx,"ArrayBuffer is detached");
        out.assign(size?reinterpret_cast<const char*>(data):"",size);return true;
    }
    if(JS_IsArrayBufferViewObject(object)) {
        const auto size=JS_GetArrayBufferViewByteLength(object);const auto* data=JS_GetArrayBufferViewData(object);
        if(size&&!data)return error(cx,"Typed array is detached");
        out.assign(size?reinterpret_cast<const char*>(data):"",size);return true;
    }
    return error(cx,"send expects a string, ArrayBuffer or typed array");
}
bool arrayBuffer(JSContext* cx,std::string_view data,JS::MutableHandleValue out) {
    if(data.size()>UINT32_MAX)return error(cx,"Response is too large for ArrayBuffer");
    JS::RootedObject buffer(cx,JS_NewArrayBuffer(cx,static_cast<uint32_t>(data.size())));
    if(!buffer)return false;
    if(!data.empty())std::memcpy(JS_GetArrayBufferData(buffer),data.data(),data.size());
    out.setObject(*buffer);return true;
}
bool initHandlers(JSContext* cx,JS::HandleObject wrapper,std::initializer_list<const char*> handlers) {
    JS::RootedValue null(cx,JS::NullValue());for(auto name:handlers)if(!property(cx,wrapper,name,null))return false;return true;
}
template<class T> bool retainObject(JSContext* cx,JS::CallArgs& args,const JSClass* klass,bool retain) {
    auto object=selfObject<T>(cx,args,klass);if(!object)return false;
    if(retain){if(!object->retainCount++){JS::RootedObject wrapper(cx,&args.thisv().toObject());object->retainedRoot=std::make_unique<JS::PersistentRootedObject>(cx,wrapper);}}
    else {if(!object->retainCount)return error(cx,"Unbalanced network object release");if(!--object->retainCount)object->retainedRoot.reset();}
    args.rval().setUndefined();return true;
}

#if AX_ENABLE_HTTP
class HttpInflater {
public:
    explicit HttpInflater(bool gzip):gzip_(gzip){}
    ~HttpInflater(){if(initialized_)inflateEnd(&stream_);}
    HttpInflater(const HttpInflater&)=delete;
    HttpInflater& operator=(const HttpInflater&)=delete;
    bool append(std::string_view input,std::string& output) {
        if(!initialized_) {
            int bits=MAX_WBITS+16;
            if(!gzip_) {
                prefix_.append(input);
                if(prefix_.size()<2)return true;
                const auto cmf=static_cast<uint8_t>(prefix_[0]),flags=static_cast<uint8_t>(prefix_[1]);
                // RFC 1950 wrapping is standard. Accept raw RFC 1951 streams
                // too, as the old curl-backed XHR did for legacy HTTP servers.
                bits=(cmf&15)==Z_DEFLATED&&(cmf>>4)<=7&&((unsigned(cmf)<<8)+flags)%31==0?MAX_WBITS:-MAX_WBITS;
                input=prefix_;
            }
            if(inflateInit2(&stream_,bits)!=Z_OK)return false;
            initialized_=true;
        }
        constexpr size_t maximumOutput=256u*1024u*1024u;
        std::array<char,16384> buffer;
        while(!input.empty()) {
            if(ended_) {
                if(!gzip_||inflateReset2(&stream_,MAX_WBITS+16)!=Z_OK)return false;
                ended_=false;
            }
            const auto count=static_cast<uInt>(std::min(input.size(),size_t(std::numeric_limits<uInt>::max())));
            stream_.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));stream_.avail_in=count;
            for(;;) {
                stream_.next_out=reinterpret_cast<Bytef*>(buffer.data());stream_.avail_out=static_cast<uInt>(buffer.size());
                const auto previous=stream_.avail_in;const auto result=inflate(&stream_,Z_NO_FLUSH);
                const size_t produced=buffer.size()-stream_.avail_out;
                if(produced>maximumOutput-decoded_)return false;
                output.append(buffer.data(),produced);decoded_+=produced;
                if(result==Z_STREAM_END) {
                    ended_=true;
                    if(stream_.avail_in) {
                        if(!gzip_)return false;
                        // Preserve the remaining input across reset for an
                        // RFC 1952 concatenated member in the same body chunk.
                        auto* remaining=stream_.next_in;const auto available=stream_.avail_in;
                        if(inflateReset2(&stream_,MAX_WBITS+16)!=Z_OK)return false;
                        stream_.next_in=remaining;stream_.avail_in=available;ended_=false;
                        continue;
                    }
                    break;
                }
                if(result!=Z_OK&&result!=Z_BUF_ERROR)return false;
                if(stream_.avail_in==0&&stream_.avail_out!=0)break;
                if(previous==stream_.avail_in&&produced==0)return false;
            }
            input.remove_prefix(count);
        }
        prefix_.clear();return true;
    }
    bool complete() const {return initialized_&&ended_;}
private:
    z_stream stream_{};
    bool gzip_{},initialized_{},ended_{};
    size_t decoded_{};
    std::string prefix_;
};
struct Xhr : NetworkObject {
    using Headers=std::multimap<std::string,std::string>;
    int readyState{},status{};
    uint64_t requestGeneration{},timeout{};
    bool sending{},failed{},withCredentials{true};
    std::string url,method,responseType,responseText,statusText,mimeType;
    std::map<std::string,std::pair<std::string,std::string>> requestHeaders;
    Headers responseHeaders;
    uint64_t total{};
    uint64_t receivedBytes{};
    bool codingReady{};
    std::vector<std::unique_ptr<HttpInflater>> inflaters;
    Clock::time_point started;
    std::unique_ptr<JS::PersistentRootedValue> response;
    void clearCoding(){inflaters.clear();codingReady=false;receivedBytes=0;}
    bool prepareCoding() {
        if(codingReady)return true;
        codingReady=true;
        std::vector<std::string> codings;
        const auto fields=responseHeaders.equal_range("content-encoding");
        for(auto it=fields.first;it!=fields.second;++it) {
            std::string_view value=it->second;
            while(!value.empty()) {
                const auto comma=value.find(',');auto token=value.substr(0,comma);
                const auto first=token.find_first_not_of(" \t"),last=token.find_last_not_of(" \t");
                if(first==std::string_view::npos)return false;
                auto coding=lower(std::string(token.substr(first,last-first+1)));
                if(coding!="identity")codings.push_back(std::move(coding));
                if(comma==std::string_view::npos)break;
                value.remove_prefix(comma+1);if(value.empty())return false;
            }
        }
        // Content codings are undone in the reverse of the server's order.
        for(auto it=codings.rbegin();it!=codings.rend();++it) {
            if(*it!="gzip"&&*it!="x-gzip"&&*it!="deflate")return false;
            inflaters.push_back(std::make_unique<HttpInflater>(*it!="deflate"));
        }
        return true;
    }
    bool appendBody(std::string_view data) {
        if(!prepareCoding())return false;
        receivedBytes+=data.size();
        std::string decoded;
        for(auto& inflater:inflaters) {
            std::string next;if(!inflater->append(data,next))return false;
            decoded=std::move(next);data=decoded;
        }
        responseText.append(data);return true;
    }
    void shutdown() override {++requestGeneration;sending=false;response.reset();clearCoding();NetworkObject::shutdown();}
    bool current(uint64_t generation) const {return sending&&requestGeneration==generation&&context();}
    void setReady(int value) {readyState=value;emit("readystatechange");}
    void progress() {emit("progress",[this](auto* cx,auto event){return property(cx,event,"lengthComputable",total>0)&&property(cx,event,"loaded",receivedBytes)&&property(cx,event,"total",total);});}
    void finish(const char* event,uint64_t generation,bool reset=false) {
        if(!current(generation))return;
        sending=false;failed=std::strcmp(event,"load")!=0;
        if(failed){status=0;statusText.clear();responseText.clear();responseHeaders.clear();response.reset();}
        clearCoding();
        setReady(4);
        if(requestGeneration!=generation)return;
        emit(event);if(requestGeneration!=generation)return;
        emit("loadend");if(requestGeneration!=generation)return;
        if(reset)readyState=0;
        activeRoot.reset();
    }
    void update() override {
        if(sending&&timeout&&std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count()>=static_cast<int64_t>(timeout))
            finish("timeout",requestGeneration);
    }
    void chunk(uint64_t generation,std::string data,Headers headers,int code,std::string reason,uint64_t expected) {
        if(!current(generation))return;
        if(code>=300&&code<400&&headers.contains("location"))return;
        if(readyState<2){status=code;statusText=std::move(reason);responseHeaders=std::move(headers);total=expected;setReady(2);}
        if(!current(generation))return;
        if(!appendBody(data)){finish("error",generation);return;}
        setReady(3);if(current(generation))progress();
    }
    void complete(uint64_t generation,Headers headers,int code,int nativeError,std::string reason,uint64_t expected) {
        if(!current(generation))return;
        status=code;statusText=std::move(reason);responseHeaders=std::move(headers);total=expected;
        // HttpClient reports a fully parsed response with yasio::errc::eof;
        // it substitutes -1 when the connection closes before HTTP completion.
        if((nativeError&&nativeError!=yasio::errc::eof)||code<=0){finish("error",generation);return;}
        if(readyState<2)setReady(2);if(!current(generation))return;
        if(readyState<3)setReady(3);if(!current(generation))return;
        if(!prepareCoding()||std::any_of(inflaters.begin(),inflaters.end(),[](const auto& inflater){return !inflater->complete();}))
            {finish("error",generation);return;}
        if(responseType=="json"||responseType=="arraybuffer") {
            auto* cx=context();JSAutoRequest request(cx);JS::RootedValue value(cx,JS::NullValue());
            if(responseType=="arraybuffer") {if(!arrayBuffer(cx,responseText,&value)){engine(cx)->reportException();finish("error",generation);return;}}
            else {JS::RootedValue source(cx);if(toJsValue(cx,responseText,&source)){JS::RootedString string(cx,source.toString());
                // The JSON response contract returns null for malformed input.
                // SM33 can call its uncaught-error reporter before returning
                // false, so suppress that report for this internal parse only.
                auto reporter=JS_SetErrorReporter(cx,nullptr);const bool parsed=JS_ParseJSON(cx,string,&value);JS_SetErrorReporter(cx,reporter);
                if(!parsed){JS_ClearPendingException(cx);value.setNull();}}else{JS_ClearPendingException(cx);value.setNull();}}
            response=std::make_unique<JS::PersistentRootedValue>(cx,value);
        }
        finish("load",generation);
    }
};
JSClass xhrClass={"XMLHttpRequest",JSCLASS_HAS_PRIVATE|JSCLASS_HAS_RESERVED_SLOTS(1),JS_PropertyStub,JS_DeletePropertyStub,
    JS_PropertyStub,JS_StrictPropertyStub,JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,finalize<Xhr>,nullptr,nullptr,nullptr,nullptr};
bool xhrConstructor(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);if(!args.isConstructing())return error(cx,"XMLHttpRequest requires new");
    JS::RootedObject wrapper(cx,JS_NewObjectForConstructor(cx,&xhrClass,args));if(!wrapper)return false;
    auto state=stateFor(cx,wrapper);if(!state)return error(cx,"XMLHttpRequest runtime is unavailable");
    auto native=std::make_shared<Xhr>();native->state=state;
    JS_SetPrivate(wrapper,new std::shared_ptr<Xhr>(native));state->objects.push_back(native);
    if(!initHandlers(cx,wrapper,{"onreadystatechange","onloadstart","onprogress","onload","onerror","onabort","ontimeout","onloadend"}))return false;
    args.rval().setObject(*wrapper);return true;
}
bool xhrOpen(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(argc<2)return error(cx,"open requires method and URL");
    std::string method,url;if(!text(cx,args[0],method)||!text(cx,args[1],url))return false;
    method=lower(std::move(method));
    if(method!="get"&&method!="post"&&method!="put"&&method!="patch"&&method!="delete")return error(cx,"Unsupported XMLHttpRequest method");
    if(url.find('\r')!=std::string::npos||url.find('\n')!=std::string::npos||url.find('\0')!=std::string::npos)return error(cx,"Invalid XMLHttpRequest URL");
    if(argc>3&&!args[3].isNullOrUndefined())return error(cx,"Native XMLHttpRequest does not support open() username/password parameters");
    // The v3 reference accepts the async argument but always sends asynchronously.
    ++native->requestGeneration;native->sending=false;native->failed=false;native->response.reset();native->clearCoding();
    native->status=0;native->statusText.clear();native->responseText.clear();native->responseHeaders.clear();native->requestHeaders.clear();native->total=0;
    native->url=std::move(url);native->method=std::move(method);
    if(native->responseType.empty()&&native->url.ends_with(".json"))native->responseType="json";
    JS::RootedObject wrapper(cx,&args.thisv().toObject());native->activeRoot=std::make_unique<JS::PersistentRootedObject>(cx,wrapper);
    const auto generation=native->requestGeneration;native->setReady(1);
    if(!native->sending&&native->requestGeneration==generation)native->activeRoot.reset();
    args.rval().setUndefined();return true;
}
bool xhrSend(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(native->readyState!=1||native->sending)return error(cx,"send requires an opened request that has not been sent");
    std::string body;if(argc&&!bytes(cx,args[0],body))return false;
    auto* request=new ax::network::HttpRequest();
    using Type=ax::network::HttpRequest::Type;
    const auto& method=native->method;
    request->setRequestType(method=="post"?Type::POST:method=="put"?Type::PUT:method=="patch"?Type::PATCH:method=="delete"?Type::DELETE:Type::GET);
    request->setUrl(native->url);if(method!="get"&&!body.empty())request->setRequestData(body.data(),body.size());
    std::vector<std::string> headers;for(const auto& [name,value]:native->requestHeaders)headers.push_back(value.first+": "+value.second);request->setHeaders(std::move(headers));
    JS::RootedObject wrapper(cx,&args.thisv().toObject());native->activeRoot=std::make_unique<JS::PersistentRootedObject>(cx,wrapper);
    native->sending=true;native->started=Clock::now();const auto generation=native->requestGeneration;
    std::weak_ptr<NetworkState> weak=native->state;
    request->setDataCallback([native,generation,weak](ax::network::HttpResponse* response,const char* data,size_t size){
        if(auto network=weak.lock()){
            std::string chunk(data,size);auto headers=response->getResponseHeaders();const auto code=response->getResponseCode();auto reason=response->getStatusText();const auto total=response->getContentLength();
            network->post([native,generation,chunk=std::move(chunk),headers=std::move(headers),code,reason=std::move(reason),total]()mutable{native->chunk(generation,std::move(chunk),std::move(headers),code,std::move(reason),total);});
        }
    });
    request->setCompleteCallback([native,generation,weak](ax::network::HttpClient*,ax::network::HttpResponse* response){
        if(auto network=weak.lock()){
            auto headers=response->getResponseHeaders();const auto code=response->getResponseCode();const auto error=response->getInternalCode();auto reason=response->getStatusText();const auto total=response->getContentLength();
            network->post([native,generation,headers=std::move(headers),code,error,reason=std::move(reason),total]()mutable{native->complete(generation,std::move(headers),code,error,std::move(reason),total);});
        }
    });
    native->emit("loadstart");
    if(native->current(generation))ax::network::HttpClient::getInstance()->send(request);
    request->release();args.rval().setUndefined();return true;
}
bool xhrAbort(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(native->sending){const auto generation=native->requestGeneration;native->finish("abort",generation,true);if(native->requestGeneration==generation)++native->requestGeneration;}
    else {++native->requestGeneration;native->readyState=0;native->status=0;native->responseText.clear();native->responseHeaders.clear();native->response.reset();native->activeRoot.reset();}
    args.rval().setUndefined();return true;
}
bool xhrRequestHeader(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(native->readyState!=1||native->sending)return error(cx,"setRequestHeader requires an opened request before send");
    if(argc<2)return error(cx,"setRequestHeader requires a name and value");
    std::string name,value;if(!text(cx,args[0],name)||!text(cx,args[1],value))return false;
    if(name.empty()||!std::all_of(name.begin(),name.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos);})||value.find_first_of("\r\n\0",0,3)!=std::string::npos)return error(cx,"Invalid HTTP header");
    auto& entry=native->requestHeaders[lower(name)];if(entry.first.empty())entry={std::move(name),std::move(value)};else entry.second+=", "+value;
    args.rval().setUndefined();return true;
}
bool xhrResponseHeader(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(!argc)return error(cx,"getResponseHeader requires a name");std::string name;if(!text(cx,args[0],name))return false;
    const auto range=native->responseHeaders.equal_range(lower(name));if(range.first==range.second){args.rval().setNull();return true;}
    std::string out;for(auto it=range.first;it!=range.second;++it){if(!out.empty())out+=", ";out+=it->second;}return toJsValue(cx,out,args.rval());
}
bool xhrResponseHeaders(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    std::string out;for(const auto& [name,value]:native->responseHeaders)out+=name+": "+value+"\r\n";return toJsValue(cx,out,args.rval());
}
bool xhrMime(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(!argc||native->readyState>=3)return error(cx,"overrideMimeType requires a MIME type before loading");
    if(!text(cx,args[0],native->mimeType))return false;
    const auto lowerMime=lower(native->mimeType);
    if(lowerMime.find("charset=")!=std::string::npos&&lowerMime.find("charset=utf-8")==std::string::npos&&lowerMime.find("charset=utf8")==std::string::npos)return error(cx,"Native text responses require UTF-8; use arraybuffer for other encodings");
    args.rval().setUndefined();return true;
}
#define XHR_GET(name,expression) bool xhrGet_##name(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);return native&&toJsValue(cx,expression,args.rval());}
XHR_GET(readyState,native->readyState) XHR_GET(status,native->status) XHR_GET(statusText,native->statusText)
XHR_GET(responseText,native->responseText) XHR_GET(responseType,native->responseType) XHR_GET(timeout,native->timeout) XHR_GET(withCredentials,native->withCredentials)
#undef XHR_GET
bool xhrGet_response(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native)return false;
    if(native->responseType.empty()||native->responseType=="text")return toJsValue(cx,native->responseText,args.rval());
    if(native->response)args.rval().set(native->response->get());else args.rval().setNull();return true;
}
bool xhrGet_responseXML(JSContext* cx,unsigned argc,JS::Value* vp) {auto args=JS::CallArgsFromVp(argc,vp);args.rval().setNull();return true;}
bool xhrGet_upload(JSContext* cx,unsigned argc,JS::Value* vp) {auto args=JS::CallArgsFromVp(argc,vp);args.rval().setNull();return true;}
bool xhrSet_responseType(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native||!argc)return false;
    std::string type;if(!text(cx,args[0],type))return false;
    if(type!=""&&type!="text"&&type!="json"&&type!="arraybuffer")return error(cx,"responseType supports text, json and arraybuffer");
    if(native->sending||native->readyState>=3)return error(cx,"Cannot change responseType after loading starts");native->responseType=std::move(type);args.rval().setUndefined();return true;
}
bool xhrSet_timeout(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native||!argc)return false;
    uint64_t value{};if(!fromJsValue(cx,args[0],value)||value>UINT32_MAX)return error(cx,"timeout must be a nonnegative millisecond value");native->timeout=value;args.rval().setUndefined();return true;
}
bool xhrSet_withCredentials(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Xhr>(cx,args,&xhrClass);if(!native||!argc)return false;
    native->withCredentials=JS::ToBoolean(args[0]);args.rval().setUndefined();return true;
}
bool xhrRetain(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);return retainObject<Xhr>(cx,args,&xhrClass,true);}
bool xhrRelease(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);return retainObject<Xhr>(cx,args,&xhrClass,false);}
const JSPropertySpec xhrProperties[]={
    JS_PSG("readyState",xhrGet_readyState,JSPROP_ENUMERATE),JS_PSG("status",xhrGet_status,JSPROP_ENUMERATE),
    JS_PSG("statusText",xhrGet_statusText,JSPROP_ENUMERATE),JS_PSG("responseText",xhrGet_responseText,JSPROP_ENUMERATE),
    JS_PSG("response",xhrGet_response,JSPROP_ENUMERATE),JS_PSG("responseXML",xhrGet_responseXML,JSPROP_ENUMERATE),JS_PSG("upload",xhrGet_upload,JSPROP_ENUMERATE),
    JS_PSGS("responseType",xhrGet_responseType,xhrSet_responseType,JSPROP_ENUMERATE),
    JS_PSGS("timeout",xhrGet_timeout,xhrSet_timeout,JSPROP_ENUMERATE),
    JS_PSGS("withCredentials",xhrGet_withCredentials,xhrSet_withCredentials,JSPROP_ENUMERATE),JS_PS_END};
const JSFunctionSpec xhrMethods[]={JS_FN("open",xhrOpen,2,0),JS_FN("send",xhrSend,1,0),JS_FN("abort",xhrAbort,0,0),
    JS_FN("setRequestHeader",xhrRequestHeader,2,0),JS_FN("getResponseHeader",xhrResponseHeader,1,0),
    JS_FN("getAllResponseHeaders",xhrResponseHeaders,0,0),JS_FN("overrideMimeType",xhrMime,1,0),
    JS_FN("retain",xhrRetain,0,0),JS_FN("release",xhrRelease,0,0),JS_FS_END};
#endif

#if AX_ENABLE_WEBSOCKET
struct Socket : NetworkObject, ax::network::WebSocket::Delegate {
    std::unique_ptr<ax::network::WebSocket> socket;
    int readyState{};
    std::string url;
    std::string binaryType{"arraybuffer"};
    ~Socket() override {if(socket)socket->close();}
    void shutdown() override {if(socket)socket->close();readyState=3;NetworkObject::shutdown();}
    std::shared_ptr<Socket> shared() {return std::static_pointer_cast<Socket>(shared_from_this());}
    void onOpen(ax::network::WebSocket*) override {
        auto me=shared();if(auto network=state.lock())network->post([me]{if(!me->context()||me->readyState!=0)return;me->readyState=1;me->emit("open");});
    }
    void onMessage(ax::network::WebSocket*,const ax::network::WebSocket::Data& value) override {
        auto me=shared();std::string data(value.len?value.bytes:"",value.len);const bool binary=value.isBinary;
        if(auto network=state.lock())network->post([me,data=std::move(data),binary]{if(!me->context()||me->readyState!=1)return;
            me->emit("message",[&](auto* cx,auto event){JS::RootedValue value(cx);return (binary?arrayBuffer(cx,data,&value):toJsValue(cx,data,&value))&&property(cx,event,"data",value);});});
    }
    void onClose(ax::network::WebSocket*,uint16_t code,std::string_view reason) override {
        auto me=shared();if(auto network=state.lock())network->post([me,code,reason=std::string(reason)]{if(!me->context())return;me->readyState=3;
            me->emit("close",[&](auto* cx,auto event){return property(cx,event,"code",code)&&property(cx,event,"reason",reason)&&property(cx,event,"wasClean",code!=1006);});me->activeRoot.reset();});
    }
    void onError(ax::network::WebSocket*,const ax::network::WebSocket::ErrorCode& error) override {
        auto me=shared();if(auto network=state.lock())network->post([me,code=static_cast<int>(error)]{if(!me->context())return;
            me->emit("error",[&](auto* cx,auto event){return property(cx,event,"code",code);});
            // Axmol reports connection failure without a subsequent close event.
            if(me->readyState==0){me->readyState=3;me->emit("close",[](auto* cx,auto event){return property(cx,event,"code",1006)&&property(cx,event,"reason","")&&property(cx,event,"wasClean",false);});me->activeRoot.reset();}
        });
    }
};
JSClass socketClass={"WebSocket",JSCLASS_HAS_PRIVATE|JSCLASS_HAS_RESERVED_SLOTS(1),JS_PropertyStub,JS_DeletePropertyStub,
    JS_PropertyStub,JS_StrictPropertyStub,JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,finalize<Socket>,nullptr,nullptr,nullptr,nullptr};
bool token(std::string_view value){return !value.empty()&&std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos;});}
bool socketConstructor(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);if(!args.isConstructing()||!argc)return error(cx,"WebSocket requires new and a URL");
    std::string url,protocols,ca;if(!text(cx,args[0],url))return false;
    if(!url.starts_with("ws://")&&!url.starts_with("wss://"))return error(cx,"WebSocket URL must use ws:// or wss://");
    if(url.find_first_of("\r\n\0#",0,4)!=std::string::npos)return error(cx,"Invalid WebSocket URL");
    std::vector<std::string> offered;
    if(argc>1&&!args[1].isNullOrUndefined()) {
        if(args[1].isString()){std::string p;if(!fromJsValue(cx,args[1],p))return false;
            // Native v3 WebSocketTest passes "" before its CA certificate path.
            // The empty string means no subprotocol in that legacy signature.
            if(!p.empty())offered.push_back(std::move(p));}
        else if(args[1].isObject()){JS::RootedObject array(cx,&args[1].toObject());if(!JS_IsArrayObject(cx,array))return error(cx,"WebSocket protocols must be a string or array");uint32_t length{};if(!JS_GetArrayLength(cx,array,&length))return false;
            for(uint32_t i=0;i<length;++i){JS::RootedValue value(cx);std::string p;if(!JS_GetElement(cx,array,i,&value)||!fromJsValue(cx,value,p))return error(cx,"Invalid WebSocket protocol");offered.push_back(std::move(p));}}
        else return error(cx,"WebSocket protocols must be a string or array");
    }
    std::set<std::string> unique;for(const auto& p:offered){if(!token(p)||!unique.insert(p).second)return error(cx,"Invalid or duplicate WebSocket protocol");if(!protocols.empty())protocols+=", ";protocols+=p;}
    if(argc>2&&!args[2].isNullOrUndefined()&&!fromJsValue(cx,args[2],ca))return error(cx,"WebSocket CA path must be a string");
    JS::RootedObject wrapper(cx,JS_NewObjectForConstructor(cx,&socketClass,args));if(!wrapper)return false;
    auto state=stateFor(cx,wrapper);if(!state)return error(cx,"WebSocket runtime is unavailable");
    auto native=std::make_shared<Socket>();native->state=state;native->url=std::move(url);native->socket=std::make_unique<ax::network::WebSocket>();
    native->activeRoot=std::make_unique<JS::PersistentRootedObject>(cx,wrapper);JS_SetPrivate(wrapper,new std::shared_ptr<Socket>(native));state->objects.push_back(native);
    if(!initHandlers(cx,wrapper,{"onopen","onmessage","onerror","onclose"}))return false;
    if(!native->socket->open(native.get(),native->url,ca,protocols)){native->shutdown();return error(cx,"WebSocket could not open the URL");}
    args.rval().setObject(*wrapper);return true;
}
bool socketSend(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Socket>(cx,args,&socketClass);if(!native)return false;
    if(native->readyState!=1||native->socket->getReadyState()!=ax::network::WebSocket::State::OPEN)return error(cx,"WebSocket is not open");
    if(!argc)return error(cx,"WebSocket send requires data");std::string data;if(!bytes(cx,args[0],data))return false;
    if(data.size()>UINT32_MAX)return error(cx,"WebSocket message is too large");
    if(args[0].isString())native->socket->send(std::string_view(data));else native->socket->send(data.data(),static_cast<unsigned>(data.size()));
    args.rval().setUndefined();return true;
}
bool socketClose(JSContext* cx,unsigned argc,JS::Value* vp) {
    auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Socket>(cx,args,&socketClass);if(!native)return false;
    int code=1000;std::string reason;
    if(argc&&!fromJsValue(cx,args[0],code))return error(cx,"Invalid WebSocket close code");
    if(code!=1000&&(code<3000||code>4999))return error(cx,"WebSocket close code must be 1000 or 3000 through 4999");
    if(argc>1&&!text(cx,args[1],reason))return false;if(reason.size()>123)return error(cx,"WebSocket close reason exceeds 123 UTF-8 bytes");
    if(native->readyState==3){args.rval().setUndefined();return true;}
    const bool connecting=native->readyState==0;native->readyState=2;
    auto network=native->state.lock();
    native->socket->closeAsync(static_cast<uint16_t>(code),reason);
    if(connecting){network->post([native]{if(!native->context()||native->readyState==3)return;native->readyState=3;
        native->emit("close",[](auto* cx,auto event){return property(cx,event,"code",1006)&&property(cx,event,"reason","")&&property(cx,event,"wasClean",false);});native->activeRoot.reset();});}
    args.rval().setUndefined();return true;
}
#define WS_GET(name,expression) bool socketGet_##name(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Socket>(cx,args,&socketClass);return native&&toJsValue(cx,expression,args.rval());}
WS_GET(readyState,native->readyState) WS_GET(url,native->url) WS_GET(protocol,native->socket->getProtocol()) WS_GET(binaryType,native->binaryType)
#undef WS_GET
bool socketSet_binaryType(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);auto native=selfObject<Socket>(cx,args,&socketClass);if(!native||!argc)return false;std::string type;
    if(!text(cx,args[0],type))return false;if(type!="arraybuffer")return error(cx,"Native WebSocket binaryType supports arraybuffer");native->binaryType=std::move(type);args.rval().setUndefined();return true;}
const JSPropertySpec socketProperties[]={JS_PSG("readyState",socketGet_readyState,JSPROP_ENUMERATE),JS_PSG("url",socketGet_url,JSPROP_ENUMERATE),
    JS_PSG("URL",socketGet_url,JSPROP_ENUMERATE),JS_PSG("protocol",socketGet_protocol,JSPROP_ENUMERATE),JS_PSGS("binaryType",socketGet_binaryType,socketSet_binaryType,JSPROP_ENUMERATE),JS_PS_END};
const JSFunctionSpec socketMethods[]={JS_FN("send",socketSend,1,0),JS_FN("close",socketClose,0,0),JS_FS_END};
#endif
}

bool registerNetwork(JSContext* cx,JS::HandleObject global) {
    auto* runtime=engine(cx);if(!runtime)return false;
    auto state=std::make_shared<NetworkState>();state->vm=runtimeState(cx);
    runtime->addCleanupCallback([state]{state->shutdown();});runtime->addUpdateCallback([state](float){state->update();});
#if AX_ENABLE_HTTP
    JS::RootedObject xhrPrototype(cx,JS_InitClass(cx,global,JS::NullPtr(),&xhrClass,xhrConstructor,0,xhrProperties,xhrMethods,nullptr,nullptr));
    if(!xhrPrototype)return false;JS_SetReservedSlot(xhrPrototype,StateSlot,JS::PrivateValue(new std::shared_ptr<NetworkState>(state)));
    JS::RootedValue xhrConstructorValue(cx);if(!JS_GetProperty(cx,global,"XMLHttpRequest",&xhrConstructorValue)||!xhrConstructorValue.isObject())return false;
    JS::RootedObject xhrCtor(cx,&xhrConstructorValue.toObject());
    const char* names[]={"UNSENT","OPENED","HEADERS_RECEIVED","LOADING","DONE"};for(int i=0;i<5;++i)if(!property(cx,xhrCtor,names[i],i)||!property(cx,xhrPrototype,names[i],i))return false;
#endif
#if AX_ENABLE_WEBSOCKET
    JS::RootedObject socketPrototype(cx,JS_InitClass(cx,global,JS::NullPtr(),&socketClass,socketConstructor,1,socketProperties,socketMethods,nullptr,nullptr));
    if(!socketPrototype)return false;JS_SetReservedSlot(socketPrototype,StateSlot,JS::PrivateValue(new std::shared_ptr<NetworkState>(state)));
    JS::RootedValue socketConstructorValue(cx);if(!JS_GetProperty(cx,global,"WebSocket",&socketConstructorValue)||!socketConstructorValue.isObject())return false;
    JS::RootedObject socketCtor(cx,&socketConstructorValue.toObject());
    const char* socketNames[]={"CONNECTING","OPEN","CLOSING","CLOSED"};for(int i=0;i<4;++i)if(!property(cx,socketCtor,socketNames[i],i)||!property(cx,socketPrototype,socketNames[i],i))return false;
#endif
    return true;
}
}
