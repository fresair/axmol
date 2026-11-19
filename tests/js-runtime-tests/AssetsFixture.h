/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace axjs::tests {
// Real HTTP fixture for the engine's curl downloader. HEAD and Range requests
// are supported so failed verification can exercise the native retry path.
class AssetsFixture {
    struct Endpoint {std::string body;int status{200};unsigned delayMs{};};
    int listener{-1};std::atomic<bool> stopped{false};std::thread acceptThread;
    std::mutex mutex;std::map<std::string,Endpoint> endpoints;
    std::map<std::string,unsigned> requests;std::vector<int> connections;
    std::vector<std::thread> workers;
    static bool writeAll(int fd,std::string_view data){while(!data.empty()){
#if defined(MSG_NOSIGNAL)
        const auto count=send(fd,data.data(),data.size(),MSG_NOSIGNAL);
#else
        const auto count=send(fd,data.data(),data.size(),0);
#endif
        if(count<=0)return false;data.remove_prefix(static_cast<size_t>(count));}return true;}
    static std::string lower(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
    void serve(int fd){std::string request;char buffer[1024];size_t boundary;
        while((boundary=request.find("\r\n\r\n"))==std::string::npos&&request.size()<65536){const auto size=recv(fd,buffer,sizeof(buffer),0);if(size<=0)return;request.append(buffer,static_cast<size_t>(size));}
        const auto first=request.find(' '),second=request.find(' ',first+1);if(first==std::string::npos||second==std::string::npos)return;
        const auto method=request.substr(0,first),path=request.substr(first+1,second-first-1);
        Endpoint endpoint;{std::lock_guard lock(mutex);if(auto it=endpoints.find(path);it!=endpoints.end())endpoint=it->second;else endpoint={"missing",404,0};if(method=="GET")++requests[path];}
        if(endpoint.delayMs)std::this_thread::sleep_for(std::chrono::milliseconds(endpoint.delayMs));
        size_t start=0;bool partial=false;const auto folded=lower(request);const auto range=folded.find("\r\nrange: bytes=");
        if(range!=std::string::npos){try{start=static_cast<size_t>(std::stoull(folded.substr(range+15)));partial=start<endpoint.body.size();}catch(...){} }
        if(!partial)start=0;
        const int status=partial?206:endpoint.status;const auto length=endpoint.body.size()-start;
        std::string response="HTTP/1.1 "+std::to_string(status)+(status==200?" OK":status==206?" Partial Content":" Not Found")+"\r\nContent-Length: "+std::to_string(length)+"\r\nAccept-Ranges: bytes\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n";
        if(partial)response+="Content-Range: bytes "+std::to_string(start)+"-"+std::to_string(endpoint.body.size()-1)+"/"+std::to_string(endpoint.body.size())+"\r\n";
        response+="\r\n";if(!writeAll(fd,response)||method=="HEAD")return;
        const auto body=std::string_view(endpoint.body).substr(start);
        if(body.size()>512){if(!writeAll(fd,body.substr(0,512)))return;std::this_thread::sleep_for(std::chrono::milliseconds(20));writeAll(fd,body.substr(512));}
        else writeAll(fd,body);
    }
public:
    uint16_t port{};
    AssetsFixture(){listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0)throw std::runtime_error("assets fixture socket failed");int enable=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))){const auto error=std::string(std::strerror(errno));close(listener);listener=-1;throw std::runtime_error("assets fixture loopback bind failed: "+error);}
        if(listen(listener,16))throw std::runtime_error("assets fixture listen failed");socklen_t length=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);port=ntohs(address.sin_port);
        acceptThread=std::thread([this]{while(!stopped){const int fd=accept(listener,nullptr,nullptr);if(fd<0)break;
#if defined(SO_NOSIGPIPE)
            int enable=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&enable,sizeof(enable));
#endif
            int noDelay=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));timeval timeout{5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            std::lock_guard lock(mutex);connections.push_back(fd);workers.emplace_back([this,fd]{try{serve(fd);}catch(...){}shutdown(fd,SHUT_RDWR);{std::lock_guard lock(mutex);std::erase(connections,fd);}close(fd);});}});
    }
    ~AssetsFixture(){stopped=true;shutdown(listener,SHUT_RDWR);close(listener);if(acceptThread.joinable())acceptThread.join();{std::lock_guard lock(mutex);for(int fd:connections)shutdown(fd,SHUT_RDWR);}for(auto& thread:workers)if(thread.joinable())thread.join();}
    void add(std::string path,std::string body,int status=200,unsigned delayMs=0){std::lock_guard lock(mutex);endpoints[std::move(path)]={std::move(body),status,delayMs};}
    unsigned count(const std::string& path){std::lock_guard lock(mutex);return requests[path];}
    std::string url() const{return "http://127.0.0.1:"+std::to_string(port);}
};
}
