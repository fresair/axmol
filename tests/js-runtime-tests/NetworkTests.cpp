/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/network/HttpClient.h"
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
#include "axmol/platform/FileUtils.h"
#include <sys/stat.h>
#endif
#include <mozilla/SHA1.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cctype>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <zlib.h>

namespace {
using namespace std::chrono_literals;
const char* currentCase="startup";
void beginCase(const char* name){currentCase=name;std::cout<<"Network test: "<<name<<std::endl;}
void check(bool success,const std::string& message){if(!success)throw std::runtime_error(message);}
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// Native command-line tests have no Java Activity or APK assets. The network
// adapters still use the engine's real sockets; provide the filesystem delegate
// required by WebSocket's optional CA-file lookup without a Java VM.
class NativeTestFileUtils final : public ax::FileUtils {
public:
    NativeTestFileUtils(){_defaultResRootPath="/data/local/tmp/axmol-js-sm33/";check(init(),"native test filesystem init failed");}
    std::string getWritablePath() const override{return _defaultResRootPath;}
    std::string getNativeWritableAbsolutePath() const override{return _defaultResRootPath;}
    bool isFileExistInternal(std::string_view path) const override{struct stat result{};const std::string name(path);return stat(name.c_str(),&result)==0&&S_ISREG(result.st_mode);}
};
#endif
std::string lowercase(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
std::string encoded(std::string_view data,int bits) {
    z_stream stream{};check(deflateInit2(&stream,Z_BEST_COMPRESSION,Z_DEFLATED,bits,8,Z_DEFAULT_STRATEGY)==Z_OK,"HTTP fixture compressor init failed");
    std::string output(256,'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));stream.avail_in=static_cast<uInt>(data.size());
    stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=static_cast<uInt>(output.size());
    const auto result=deflate(&stream,Z_FINISH);const auto size=stream.total_out;deflateEnd(&stream);check(result==Z_STREAM_END,"HTTP fixture compression failed");output.resize(size);return output;
}
bool writeAll(int fd,std::string_view value){while(!value.empty()){
#if defined(MSG_NOSIGNAL)
    const auto count=send(fd,value.data(),value.size(),MSG_NOSIGNAL);
#else
    const auto count=send(fd,value.data(),value.size(),0);
#endif
    if(count<=0)return false;value.remove_prefix(static_cast<size_t>(count));}return true;}
bool readAll(int fd,char* out,size_t size){while(size){const auto count=recv(fd,out,size,0);if(count<=0)return false;out+=count;size-=static_cast<size_t>(count);}return true;}
std::string base64(const uint8_t* input,size_t length){const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string output;
    for(size_t i=0;i<length;i+=3){const uint32_t word=(input[i]<<16)|(i+1<length?input[i+1]<<8:0)|(i+2<length?input[i+2]:0);
        output+=table[(word>>18)&63];output+=table[(word>>12)&63];output+=i+1<length?table[(word>>6)&63]:'=';output+=i+2<length?table[word&63]:'=';}return output;}
class Fixture {
    int listener{-1};std::atomic<bool> stopped{false};std::thread acceptThread;
    std::mutex mutex;std::vector<std::thread> workers;std::vector<int> connections;
    unsigned receivedCloseCode{};std::string receivedCloseReason;
    static std::string encodedFrame(unsigned opcode,std::string_view body){std::string data;data.push_back(static_cast<char>(0x80|opcode));
        if(body.size()<126)data.push_back(static_cast<char>(body.size()));else{data.push_back(126);data.push_back(static_cast<char>(body.size()>>8));data.push_back(static_cast<char>(body.size()));}
        data+=body;return data;}
    void frame(int fd,unsigned opcode,std::string_view body){writeAll(fd,encodedFrame(opcode,body));}
    void websocket(int fd,const std::map<std::string,std::string>& headers,std::string_view path){auto it=headers.find("sec-websocket-key");if(it==headers.end())return;
        const auto challenge=it->second+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";mozilla::SHA1Sum hash;hash.update(challenge.data(),static_cast<uint32_t>(challenge.size()));mozilla::SHA1Sum::Hash bytes;hash.finish(bytes);
        std::string response="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+base64(bytes,sizeof(bytes))+"\r\n";
        if(headers.contains("sec-websocket-protocol"))response+=path=="/ws-invalid"?"Sec-WebSocket-Protocol: unoffered\r\n":"Sec-WebSocket-Protocol: alpha\r\n";
        response+="\r\n";
        if(path=="/ws")response+=encodedFrame(1,"initial upgrade frame");
        if(!writeAll(fd,response))return;
        if(path=="/ws-peer-close"){std::this_thread::sleep_for(20ms);frame(fd,8,std::string("\x0b\xba")+"peer");return;}
        while(!stopped){unsigned char prefix[2];if(!readAll(fd,reinterpret_cast<char*>(prefix),2))return;const unsigned opcode=prefix[0]&15;uint64_t size=prefix[1]&127;
            if(size==126){unsigned char extended[2];if(!readAll(fd,reinterpret_cast<char*>(extended),2))return;size=(extended[0]<<8)|extended[1];}
            if(size==127||size>65535)return;
            unsigned char mask[4]{};if((prefix[1]&128)&&!readAll(fd,reinterpret_cast<char*>(mask),4))return;
            std::string body(static_cast<size_t>(size),'\0');if(size&&!readAll(fd,body.data(),body.size()))return;
            if(prefix[1]&128)for(size_t i=0;i<body.size();++i)body[i]^=mask[i%4];
            if(opcode==8){if(!(prefix[1]&128)||body.size()<2)return;
                {std::lock_guard lock(mutex);receivedCloseCode=(static_cast<unsigned char>(body[0])<<8)|static_cast<unsigned char>(body[1]);receivedCloseReason=body.substr(2);}
                frame(fd,8,body);return;}if(opcode==9){frame(fd,10,body);continue;}frame(fd,opcode,body);
        }
    }
    void serve(int fd){std::string request;char chunk[1024];size_t boundary=std::string::npos;
        while(request.size()<65536&&(boundary=request.find("\r\n\r\n"))==std::string::npos){const auto count=recv(fd,chunk,sizeof(chunk),0);if(count<=0)return;request.append(chunk,static_cast<size_t>(count));}
        if(boundary==std::string::npos)return;const auto first=request.find("\r\n");const auto space=request.find(' ');const auto end=request.find(' ',space+1);if(space==std::string::npos||end==std::string::npos)return;
        const auto path=request.substr(space+1,end-space-1);std::map<std::string,std::string> headers;
        size_t begin=first+2;while(begin<boundary){auto finish=request.find("\r\n",begin);const auto colon=request.find(':',begin);if(colon<finish){auto value=request.substr(colon+1,finish-colon-1);while(!value.empty()&&value.front()==' ')value.erase(value.begin());headers[lowercase(request.substr(begin,colon-begin))]=std::move(value);}begin=finish+2;}
        if(headers.contains("upgrade")){websocket(fd,headers,path);return;}
        const auto contentLength=headers.contains("content-length")?static_cast<size_t>(std::stoul(headers["content-length"])):0;
        std::string body=request.substr(boundary+4);while(body.size()<contentLength){const auto count=recv(fd,chunk,sizeof(chunk),0);if(count<=0)return;body.append(chunk,static_cast<size_t>(count));}body.resize(contentLength);
        if(path=="/drop")return;
        if(path=="/truncated"){writeAll(fd,"HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\ncut");return;}
        if(path=="/slow")std::this_thread::sleep_for(180ms);
        if(path=="/redirect"){
            writeAll(fd,"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:"+std::to_string(port)+"/text\r\nContent-Length: 6\r\nConnection: close\r\n\r\nignore");return;
        }
        int status=200;std::string contentType="text/plain; charset=utf-8",output;
        std::string coding;
        if(path=="/text")output=std::string("你好\0JS",9);
        else if(path=="/json.json"){contentType="application/json";output="{\"answer\":42,\"message\":\"中文\"}";}
        else if(path=="/bad.json"){contentType="application/json";output="{invalid";}
        else if(path=="/echo")output=body;
        else if(path=="/gzip"||path=="/gzip-raw"||path=="/deflate"||path=="/raw-deflate"||path=="/identity"||path=="/broken-gzip"||path=="/short-gzip"||path=="/stacked"||path=="/gzip-concat"||path=="/gzip-trailing") {
            output="{\"gzipped\":true,\"message\":\"中文\"}";contentType="application/json";
            if(path=="/identity")coding="identity";
            else if(path=="/deflate"||path=="/raw-deflate"){output=encoded(output,path=="/deflate"?MAX_WBITS:-MAX_WBITS);coding="DeFlAtE";}
            else if(path=="/gzip-concat") {output=encoded(std::string_view(output).substr(0,14),MAX_WBITS+16)+encoded(std::string_view(output).substr(14),MAX_WBITS+16);coding="gzip";}
            else {output=encoded(output,MAX_WBITS+16);coding="GZiP";}
            if(path=="/stacked"){output=encoded(output,MAX_WBITS);coding="gzip, deflate";}
            if(path=="/broken-gzip")output.back()^=0xff;
            if(path=="/short-gzip")output.resize(output.size()-5);
            if(path=="/gzip-trailing")output+="invalid gzip trailer";
            if(path=="/gzip-raw")coding.clear();
        }
        else if(path=="/unsupported-encoding"){output="raw";coding="br";}
        else if(path=="/slow")output="late";
        else{status=404;output="missing";}
        std::string response="HTTP/1.1 "+std::to_string(status)+(status==200?" OK":" Not Found")+"\r\nContent-Type: "+contentType+"\r\nContent-Length: "+std::to_string(output.size())+"\r\nX-Custom: first\r\nX-Custom: second\r\nConnection: close\r\n";
        if(headers.contains("x-test"))response+="X-Echo: "+headers["x-test"]+"\r\n";
        if(!coding.empty())response+="cOnTeNt-EnCoDiNg: "+coding+"\r\n";
        response+="\r\n";if(!writeAll(fd,response))return;
        // Separate body chunks ensure HEADERS_RECEIVED and LOADING are observed
        // while the core parser has already populated the HTTP response code.
        if(path=="/text"){writeAll(fd,std::string_view(output).substr(0,6));std::this_thread::sleep_for(25ms);writeAll(fd,std::string_view(output).substr(6));}
        else if(path=="/gzip"||path=="/deflate"||path=="/raw-deflate") {
            writeAll(fd,std::string_view(output).substr(0,1));std::this_thread::sleep_for(15ms);
            writeAll(fd,std::string_view(output).substr(1,6));std::this_thread::sleep_for(15ms);
            writeAll(fd,std::string_view(output).substr(7));
        }
        else writeAll(fd,output);
    }
public:
    uint16_t port{};
    bool sawCloseFrame(unsigned code,std::string_view reason){std::lock_guard lock(mutex);return receivedCloseCode==code&&receivedCloseReason==reason;}
    Fixture(){listener=socket(AF_INET,SOCK_STREAM,0);check(listener>=0,"fixture socket failed");int enable=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))){const auto reason=std::string(std::strerror(errno));close(listener);listener=-1;throw std::runtime_error("fixture loopback bind failed: "+reason);}
        check(listen(listener,16)==0,"fixture listen failed");socklen_t length=sizeof(address);check(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0,"fixture port failed");port=ntohs(address.sin_port);
        acceptThread=std::thread([this]{while(!stopped){const int fd=accept(listener,nullptr,nullptr);if(fd<0)break;
#if defined(SO_NOSIGPIPE)
            int enable=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&enable,sizeof(enable));
#endif
            int noDelay=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));
            timeval timeout{5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            std::lock_guard lock(mutex);connections.push_back(fd);workers.emplace_back([this,fd]{try{serve(fd);}catch(...){}shutdown(fd,SHUT_RDWR);
                {std::lock_guard lock(mutex);auto it=std::find(connections.begin(),connections.end(),fd);if(it!=connections.end())connections.erase(it);}close(fd);});}});
    }
    ~Fixture(){stopped=true;if(listener>=0){shutdown(listener,SHUT_RDWR);close(listener);}if(acceptThread.joinable())acceptThread.join();
        {std::lock_guard lock(mutex);for(int fd:connections)shutdown(fd,SHUT_RDWR);}for(auto& worker:workers)if(worker.joinable())worker.join();}
};
void eval(ax::JsEngine* engine,std::string_view source){check(engine->evalString(source),"network JavaScript assertion failed");}
void waitFor(ax::JsEngine* engine,const std::vector<std::string>& errors,std::string_view expression="done",std::chrono::milliseconds limit=4000ms){const auto deadline=std::chrono::steady_clock::now()+limit;
    // Mirror Director::renderFrame(): tick the Scheduler, then run the script
    // engine's Director-owned frame entry. The runtime no longer self-registers
    // a Scheduler timer, so a bare Scheduler tick must not be expected to pump it.
    for(;;){ax::Director::getInstance()->getScheduler()->update(0.005F);engine->updateFrame(0.005F);check(errors.empty(),errors.empty()?"":errors.back());JS::RootedValue result(engine->getContext());
        check(engine->evalString(expression,&result),"network result evaluation failed");if(JS::ToBoolean(result))return;check(std::chrono::steady_clock::now()<deadline,std::string("network test timed out: ")+currentCase);std::this_thread::sleep_for(3ms);}
}
}
int main(){
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeTestFileUtils());
#endif
    auto* engine=ax::JsEngine::getInstance();std::vector<std::string> errors;engine->setErrorHandler([&](std::string_view value){errors.emplace_back(value);std::cerr<<"VM ERROR: "<<value<<std::endl;});
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);engine->addRegisterCallback(axjs::registerNetwork);
    try{Fixture fixture;check(engine->start(),"network runtime start failed");
        eval(engine,"var http='http://127.0.0.1:"+std::to_string(fixture.port)+"', wsUrl='ws://127.0.0.1:"+std::to_string(fixture.port)+"/ws';function expect(value,message){if(!value)throw Error(message);}var done=false;");
#if AX_ENABLE_HTTP
        beginCase("XHR original GET gzip and compressed progress/headers");
        eval(engine,R"JS(
            done=false;var compressed=new XMLHttpRequest(),compressedStates=[],compressedProgress=false;
            compressed.onreadystatechange=function(){compressedStates.push(this.readyState);};
            compressed.open('GET',http+'/gzip',true);compressed.timeout=5000;compressed.setRequestHeader('Accept-Encoding','gzip,deflate');
            compressed.onprogress=function(e){expect(e.lengthComputable&&e.loaded<=e.total,'wire-byte compressed progress');compressedProgress=true;};
            compressed.onload=function(){expect(this.responseText==='{"gzipped":true,"message":"中文"}','decompressed original gzip responseText');expect(this.getResponseHeader('CONTENT-ENCODING')==='GZiP','preserved encoding header');expect(this.status===200&&compressedStates.indexOf(2)>=0&&compressedStates.indexOf(3)>=0,'compressed response state/status');expect(compressedProgress,'compressed progress dispatched');done=true;};
            compressed.onerror=function(){throw Error('unexpected gzip transport/decode error');};compressed.send();
        )JS");waitFor(engine,errors);
        beginCase("XHR zlib and raw deflate, identity and stacked encodings");
        for(const char* resource:{"/deflate","/raw-deflate","/identity","/stacked","/gzip-concat"}) {
            eval(engine,std::string("done=false;var c=new XMLHttpRequest();c.open('GET',http+'")+resource+"');c.responseType='json';c.onload=function(){expect(this.response.gzipped===true&&this.response.message==='中文','decoded JSON coding');done=true;};c.onerror=function(){throw Error('unexpected encoding failure');};c.send();");waitFor(engine,errors);
        }
        beginCase("XHR decoded ArrayBuffer and raw gzip transport bytes");
        eval(engine,R"JS(
            done=false;var c=new XMLHttpRequest();c.open('GET',http+'/gzip');c.responseType='arraybuffer';c.onload=function(){var data=new Uint8Array(this.response);expect(data[0]===123&&data[data.length-1]===125,'ArrayBuffer receives decoded content');done=true;};c.send();
        )JS");waitFor(engine,errors);
        eval(engine,R"JS(
            done=false;var c=new XMLHttpRequest();c.open('GET',http+'/gzip-raw');c.responseType='arraybuffer';c.onload=function(){var data=new Uint8Array(this.response);expect(data[0]===31&&data[1]===139,'no Content-Encoding preserves raw gzip binary');done=true;};c.send();
        )JS");waitFor(engine,errors);
        beginCase("XHR corrupt, truncated and unsupported Content-Encoding failures");
        for(const char* resource:{"/broken-gzip","/short-gzip","/gzip-trailing","/unsupported-encoding"}) {
            eval(engine,std::string("done=false;var c=new XMLHttpRequest(),failed=false;c.open('GET',http+'")+resource+"');c.onload=function(){throw Error('invalid coded response loaded');};c.onerror=function(){failed=true;expect(this.status===0&&this.readyState===4&&this.responseText==='','decoding error clears partial response');};c.onloadend=function(){expect(failed,'encoding failure precedes loadend');done=true;};c.send();");waitFor(engine,errors);
        }
        beginCase("XHR split response and headers");
        eval(engine,R"JS(
            done=false;expect(XMLHttpRequest.DONE===4,'XHR constants');
            var states=[],partialProgress=false,loaded=false,x=new XMLHttpRequest();
            x.onprogress=function(e){expect(e.total===9&&e.loaded<=e.total&&e.lengthComputable,'stable progress total');if(e.loaded<e.total)partialProgress=true;};
            x.onreadystatechange=function(){expect(this===x,'XHR this');states.push(this.readyState);if(this.readyState===2||this.readyState===3)expect(this.status===200,'status available before DONE');};
            x.onload=function(e){loaded=true;expect(e.target===x&&e.type==='load','load event');expect(x.status===200&&x.statusText==='OK','status');expect(x.responseText==='你好\0JS','UTF8/NUL text');expect(x.getResponseHeader('X-CUSTOM')==='first, second','case insensitive repeated headers');expect(x.getResponseHeader('missing')===null,'missing header');expect(x.getAllResponseHeaders().indexOf('x-custom: first')>=0,'all headers');expect(states[0]===1&&states.indexOf(2)>=0&&states.indexOf(3)>=0&&states[states.length-1]===4,'readyState transitions');expect(partialProgress,'split body produced partial progress');};
            x.onerror=function(){throw Error('unexpected XHR transport error');};x.onloadend=function(){expect(loaded,'load occurs before loadend');done=true;};x.open('GET',http+'/text');x.send();
        )JS");waitFor(engine,errors);beginCase("XHR redirect");
        eval(engine,R"JS(
            done=false;var redirected=new XMLHttpRequest();redirected.open('GET',http+'/redirect');redirected.onload=function(){expect(redirected.status===200&&redirected.responseText==='你好\0JS','redirect body excluded');done=true;};redirected.send();
        )JS");waitFor(engine,errors);beginCase("XHR JSON");
        eval(engine,R"JS(
            done=false;var json=new XMLHttpRequest();json.open('GET',http+'/json.json');expect(json.responseType==='json','v3 json URL default');json.onload=function(){expect(json.response.answer===42&&json.response.message==='中文','JSON response');expect(json.response===json.response,'stable JSON identity');done=true;};json.send();
        )JS");waitFor(engine,errors);beginCase("XHR invalid JSON");
        eval(engine,R"JS(
            done=false;var bad=new XMLHttpRequest();bad.open('GET',http+'/bad.json');bad.onload=function(){expect(bad.response===null,'invalid JSON is null');done=true;};bad.send();
        )JS");waitFor(engine,errors);beginCase("XHR binary view");
        eval(engine,R"JS(
            done=false;var binary=new XMLHttpRequest();binary.open('POST',http+'/echo');binary.responseType='arraybuffer';binary.setRequestHeader('X-Test','one');binary.setRequestHeader('x-test','two');
            binary.onload=function(){var data=new Uint8Array(binary.response);expect(data.length===4&&data[0]===0&&data[1]===255&&data[3]===42,'typed array upload/response');expect(binary.response===binary.response,'stable buffer identity');expect(binary.getResponseHeader('x-echo')==='one, two','combined request header');done=true;};var sendData=new Uint8Array([9,0,255,1,42,8]);binary.send(sendData.subarray(1,5));
        )JS");waitFor(engine,errors);beginCase("XHR HTTP 404");
        eval(engine,R"JS(
            done=false;var missing=new XMLHttpRequest();missing.open('GET',http+'/missing');missing.onerror=function(){throw Error('HTTP404 treated as transport error');};missing.onload=function(){expect(missing.status===404&&missing.responseText==='missing','HTTP404 load');done=true;};missing.send();
        )JS");waitFor(engine,errors);beginCase("XHR timeout");
        eval(engine,R"JS(
            done=false;var events=[],slow=new XMLHttpRequest();slow.open('GET',http+'/slow');slow.timeout=15;
            slow.ontimeout=function(){events.push('timeout');expect(slow.status===0&&slow.readyState===4,'timeout status/state');};slow.onload=function(){throw Error('timed out request loaded');};slow.onloadend=function(){events.push('end');done=true;};slow.send();
        )JS");waitFor(engine,errors);eval(engine,"expect(events.join(',')==='timeout,end','timeout event order: '+events.join(','));");beginCase("XHR abort and reuse");
        eval(engine,R"JS(
            done=false;var oldLoad=false,reused=new XMLHttpRequest();reused.open('GET',http+'/slow');reused.onload=function(){oldLoad=true;};reused.send();reused.abort();expect(reused.readyState===0,'abort UNSENT');
            reused.open('GET',http+'/text');reused.onload=function(){expect(!oldLoad&&reused.responseText==='你好\0JS','reuse after abort');done=true;};reused.send();
        )JS");waitFor(engine,errors);beginCase("XHR transport error");
        eval(engine,R"JS(
            done=false;var dropped=new XMLHttpRequest();dropped.open('GET',http+'/drop');dropped.onerror=function(){expect(dropped.status===0,'transport error status');done=true;};dropped.onload=function(){throw Error('dropped transport loaded');};dropped.send();
        )JS");waitFor(engine,errors);beginCase("XHR incomplete response");
        eval(engine,R"JS(
            done=false;var truncated=new XMLHttpRequest();truncated.open('GET',http+'/truncated');truncated.onerror=function(){expect(truncated.status===0,'incomplete HTTP response error');done=true;};truncated.onload=function(){throw Error('incomplete HTTP response loaded');};truncated.send();
        )JS");waitFor(engine,errors);beginCase("XHR validation and GC");
        eval(engine,R"JS(
            var validation=new XMLHttpRequest();validation.open('GET',http+'/text');var rejected=false;try{validation.setRequestHeader('X-Test','bad\r\nInjected: yes');}catch(e){rejected=true;}expect(rejected,'header injection rejected');
            rejected=false;try{validation.timeout=-1;}catch(e){rejected=true;}expect(rejected&&validation.timeout===0,'invalid timeout does not mutate');
            expect(validation.upload===null&&validation.responseXML===null,'v3 nullable properties');validation.retain();validation.release();
            rejected=false;try{validation.release();}catch(e){rejected=true;}expect(rejected,'unbalanced release rejected');
        )JS");
        eval(engine,R"JS(
            done=false;(function(){var temporary=new XMLHttpRequest();temporary.open('GET',http+'/text');temporary.onload=function(){expect(temporary.responseText==='你好\0JS','active XHR survives GC');done=true;};temporary.send();})();
        )JS");engine->garbageCollect();waitFor(engine,errors);
#endif
#if AX_ENABLE_WEBSOCKET
        beginCase("WebSocket text, binary, close and GC");
        eval(engine,R"JS(
            done=false;expect(WebSocket.OPEN===1&&WebSocket.CLOSED===3,'WS constants');(function(){var messages=0,opened=false,socket=new WebSocket(wsUrl,['alpha','beta']);
            expect(socket.readyState===WebSocket.CONNECTING,'WS connecting state');socket.binaryType='arraybuffer';
            socket.onopen=function(e){expect(e.type==='open'&&this===socket&&socket.protocol==='alpha','WS open/protocol');opened=true;socket.send('你好\0WS');};
            socket.onmessage=function(e){if(messages++===0){expect(opened&&this===socket&&e.data==='initial upgrade frame','WS HTTP101 coalesced first frame/open order');}else if(messages===2){expect(e.data==='你好\0WS','WS UTF8/NUL text');var data=new Uint8Array([7,0,255,42,8]);socket.send(data.subarray(1,4));}else{var binary=new Uint8Array(e.data);expect(binary.length===3&&binary[0]===0&&binary[1]===255&&binary[2]===42,'WS binary view');socket.close(3001,'done');expect(socket.readyState===2,'WS closing state');socket.close(3002,'ignored');}};
            socket.onclose=function(e){expect(messages===3&&socket.readyState===3&&e.code===3001&&e.reason==='done'&&e.wasClean,'WS close metadata and all messages');done=true;};socket.onerror=function(){throw Error('unexpected WS error');};})();
        )JS");engine->garbageCollect();waitFor(engine,errors);
        const auto closeDeadline=std::chrono::steady_clock::now()+1s;while(!fixture.sawCloseFrame(3001,"done")){check(std::chrono::steady_clock::now()<closeDeadline,"masked close frame and code/reason were not sent");std::this_thread::sleep_for(3ms);}
        beginCase("WebSocket peer close reason");
        eval(engine,R"JS(
            done=false;var invalidEmptyProtocol=false;try{new WebSocket(wsUrl,['']);}catch(e){invalidEmptyProtocol=true;}expect(invalidEmptyProtocol,'empty protocol array token stays invalid');
            var peer=new WebSocket(wsUrl+'-peer-close','','');peer.onclose=function(e){expect(e.code===3002&&e.reason==='peer'&&e.wasClean,'peer close code/reason');done=true;};peer.onerror=function(){throw Error('unexpected peer close error');};
        )JS");waitFor(engine,errors);
        beginCase("WebSocket invalid negotiated protocol");
        eval(engine,R"JS(
            done=false;var invalidProtocol=new WebSocket(wsUrl+'-invalid',['alpha']),protocolError=false;invalidProtocol.onopen=function(){throw Error('unoffered protocol accepted');};invalidProtocol.onerror=function(){protocolError=true;};invalidProtocol.onclose=function(e){expect(protocolError&&e.code===1006,'invalid protocol error and close');done=true;};
        )JS");waitFor(engine,errors);
        beginCase("WebSocket close while connecting");
        eval(engine,R"JS(
            done=false;var connecting=new WebSocket(wsUrl);connecting.onopen=function(){throw Error('closed connecting socket opened');};connecting.onclose=function(e){expect(connecting.readyState===3&&e.code===1006,'connecting close');connecting.close();done=true;};connecting.close();connecting.close();
        )JS");waitFor(engine,errors);beginCase("WebSocket connection failure");
        eval(engine,R"JS(
            done=false;var failureEvents=[],failure=new WebSocket('ws://127.0.0.1:1/no-server');failure.onerror=function(){failureEvents.push('error');};failure.onclose=function(e){failureEvents.push('close');expect(e.code===1006&&!e.wasClean,'failed WS close');done=true;};
        )JS");waitFor(engine,errors);eval(engine,"expect(failureEvents.join(',')==='error,close','WS failure event order');");
#endif
#if AX_ENABLE_HTTP
        beginCase("Pending request VM restart");
        eval(engine,"var pending=new XMLHttpRequest();pending.open('GET',http+'/slow');pending.onload=function(){throw Error('callback entered restarted VM');};pending.send();");
#endif
        check(engine->restart(),"network runtime restart failed");
        eval(engine,"var done=true;expectNew=typeof XMLHttpRequest==='function';");
        const auto until=std::chrono::steady_clock::now()+250ms;while(std::chrono::steady_clock::now()<until){ax::Director::getInstance()->getScheduler()->update(0.005F);engine->updateFrame(0.005F);std::this_thread::sleep_for(3ms);}
        check(errors.empty(),errors.empty()?"":errors.back());engine->garbageCollect();engine->stop();
#if AX_ENABLE_HTTP
        ax::network::HttpClient::destroyInstance();
#endif
        std::cout<<"JavaScript HTTP/WebSocket network tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Network tests failed: "<<error.what()<<'\n';engine->stop();
#if AX_ENABLE_HTTP
        ax::network::HttpClient::destroyInstance();
#endif
        return 1;}
}
