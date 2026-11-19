/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <mozilla/SHA1.h>
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
// A real HTTP + RFC6455 server speaking the exact legacy wire protocols used
// by the original Cocos2d-x v3 SocketIOTest.js. No fake JS transport is injected.
class SocketIOFixture {
public:
    enum class Mode { Normal, MalformedHandshake, RejectHandshake, DelayedHandshake, MalformedEvent };
private:
    bool version1;
    Mode mode;
    int listener{-1};
    std::atomic<bool> stopped{false};
    std::thread acceptThread;
    std::mutex mutex;
    std::vector<int> connections;
    std::vector<std::thread> workers;
    std::vector<std::string> received;
    static bool writeAll(int fd, std::string_view data)
    {
        while (!data.empty())
        {
#if defined(MSG_NOSIGNAL)
            const auto count = send(fd, data.data(), data.size(), MSG_NOSIGNAL);
#else
            const auto count = send(fd, data.data(), data.size(), 0);
#endif
            if (count <= 0) return false;
            data.remove_prefix(static_cast<size_t>(count));
        }
        return true;
    }
    static bool readAll(int fd, char* out, size_t size)
    {
        while (size)
        {
            const auto count = recv(fd, out, size, 0);
            if (count <= 0) return false;
            out += count;
            size -= static_cast<size_t>(count);
        }
        return true;
    }
    static std::string base64(const uint8_t* bytes, size_t length)
    {
        constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < length; i += 3)
        {
            const uint32_t word = (bytes[i] << 16) | (i + 1 < length ? bytes[i + 1] << 8 : 0) |
                                  (i + 2 < length ? bytes[i + 2] : 0);
            out += table[(word >> 18) & 63]; out += table[(word >> 12) & 63];
            out += i + 1 < length ? table[(word >> 6) & 63] : '=';
            out += i + 2 < length ? table[word & 63] : '=';
        }
        return out;
    }
    static std::string lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    }
    static std::string encodedFrame(unsigned opcode, std::string_view body)
    {
        std::string packet(1, static_cast<char>(0x80 | opcode));
        if (body.size() < 126) packet += static_cast<char>(body.size());
        else
        {
            packet += char(126); packet += static_cast<char>(body.size() >> 8);
            packet += static_cast<char>(body.size());
        }
        packet += body;
        return packet;
    }
    static bool frame(int fd, unsigned opcode, std::string_view body)
    {
        return writeAll(fd, encodedFrame(opcode, body));
    }
    bool connectEvent(int fd, const std::string& ns)
    {
        if (!frame(fd, 1, version1 ? "40" + (ns.empty() ? "" : ns + ",") : "1::" + ns)) return false;
        if (mode == Mode::MalformedEvent)
            return frame(fd, 1, version1 ? "42" + (ns.empty() ? "" : ns + ",") + "[invalid"
                                        : "5::" + ns + ":{invalid");
        return frame(fd, 1, version1 ? "42" + (ns.empty() ? "" : ns + ",") + "[\"testevent\",\"server data\"]"
                                    : "5::" + ns + ":{\"name\":\"testevent\",\"args\":[\"server data\"]}");
    }
    void websocket(int fd, const std::map<std::string, std::string>& headers, const std::string& path)
    {
        const auto it = headers.find("sec-websocket-key");
        if (it == headers.end()) return;
        if (path != (version1 ? "/socket.io/1/websocket/?EIO=2&transport=websocket&sid=legacy_session"
                              : "/socket.io/1/websocket/legacy_session")) return;
        ++websocketRequests;
        const auto challenge = it->second + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        mozilla::SHA1Sum hash; hash.update(challenge.data(), static_cast<uint32_t>(challenge.size()));
        mozilla::SHA1Sum::Hash bytes; hash.finish(bytes);
        std::string upgrade = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                              base64(bytes, sizeof(bytes)) + "\r\n\r\n";
        if (!version1)
        {
            // Real servers may send the default namespace connect immediately
            // after their HTTP upgrade. Use one write so trailing protocol
            // bytes cannot be accidentally discarded by a handshake parser.
            upgrade += encodedFrame(1, "1::");
            upgrade += encodedFrame(1, mode == Mode::MalformedEvent ? "5:::{invalid" :
                                      "5:::{\"name\":\"testevent\",\"args\":[\"server data\"]}");
            upgrade += encodedFrame(1, "2::");
            ++coalescedUpgrades;
        }
        if (!writeAll(fd, upgrade)) return;
        bool probed = false;
        while (!stopped)
        {
            unsigned char prefix[2], mask[4]{};
            if (!readAll(fd, reinterpret_cast<char*>(prefix), 2)) return;
            const unsigned opcode = prefix[0] & 15;
            uint64_t length = prefix[1] & 127;
            if (length == 126)
            {
                unsigned char data[2]; if (!readAll(fd, reinterpret_cast<char*>(data), 2)) return;
                length = (data[0] << 8) | data[1];
            }
            if (length == 127 || length > 65535 || !(prefix[1] & 128)) return;
            if (!readAll(fd, reinterpret_cast<char*>(mask), 4)) return;
            std::string body(static_cast<size_t>(length), '\0');
            if (length && !readAll(fd, body.data(), body.size())) return;
            for (size_t i = 0; i < body.size(); ++i) body[i] ^= mask[i % 4];
            if (opcode == 8) { ++closeFrames; frame(fd, 8, body); return; }
            if (opcode == 9) { frame(fd, 10, body); continue; }
            if (opcode != 1) return;
            { std::lock_guard lock(mutex); received.emplace_back(body); }
            if (version1)
            {
                if (body == "2probe") { probed = true; ++probes; frame(fd, 1, "3probe"); }
                else if (body == "5")
                {
                    if (!probed) return;
                    ++upgrades;
                    if (!connectEvent(fd, "") || !frame(fd, 1, "2serverPing")) return;
                }
                else if (body == "3serverPing") ++heartbeatReplies;
                else if (body == "2") frame(fd, 1, "3");
                else if (body.starts_with("40/"))
                {
                    auto ns = body.substr(2); if (!ns.empty() && ns.back() == ',') ns.pop_back();
                    if (!connectEvent(fd, ns)) return;
                }
                else if (body.starts_with("42")) { ++messages; frame(fd, 1, body); }
                else if (body.starts_with("41")) { ++disconnectPackets; frame(fd, 1, body); }
            }
            else
            {
                if (body == "2::") ++heartbeatReplies;
                else if (body.starts_with("1::/")) { if (!connectEvent(fd, body.substr(3))) return; }
                else if (body.starts_with("3::") || body.starts_with("5::")) { ++messages; frame(fd, 1, body); }
                else if (body.starts_with("0::")) { ++disconnectPackets; frame(fd, 1, body); }
            }
        }
    }
    void serve(int fd)
    {
        std::string request; char bytes[1024]; size_t boundary;
        while ((boundary = request.find("\r\n\r\n")) == std::string::npos && request.size() < 65536)
        {
            const auto count = recv(fd, bytes, sizeof(bytes), 0); if (count <= 0) return;
            request.append(bytes, static_cast<size_t>(count));
        }
        const auto first = request.find(' '), second = request.find(' ', first + 1);
        if (first == std::string::npos || second == std::string::npos) return;
        const auto path = request.substr(first + 1, second - first - 1);
        std::map<std::string, std::string> headers;
        size_t start = request.find("\r\n") + 2;
        while (start < boundary)
        {
            const auto end = request.find("\r\n", start), colon = request.find(':', start);
            if (colon < end)
            {
                auto value = request.substr(colon + 1, end - colon - 1);
                while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                headers[lower(request.substr(start, colon - start))] = std::move(value);
            }
            start = end + 2;
        }
        if (headers.contains("upgrade")) { websocket(fd, headers, path); return; }
        if (path != "/socket.io/1/?EIO=2&transport=polling&b64=true") return;
        ++handshakeRequests;
        if (mode == Mode::DelayedHandshake) std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::string response = version1 ? "0{\"sid\":\"legacy_session\",\"upgrades\":[\"websocket\"],\"pingInterval\":100,\"pingTimeout\":2000}"
                                       : "legacy_session:1:2:websocket,xhr-polling";
        if (version1) response = std::to_string(response.size()) + ":" + response;
        if (mode == Mode::MalformedHandshake) response = version1 ? "0{\"sid\":17}" : "not a handshake";
        const bool rejected = mode == Mode::RejectHandshake;
        writeAll(fd, std::string("HTTP/1.1 ") + (rejected ? "403 Forbidden" : "200 OK") +
                     "\r\nContent-Length: " + std::to_string(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
    }
public:
    uint16_t port{};
    std::atomic<unsigned> handshakeRequests{}, websocketRequests{}, coalescedUpgrades{}, probes{}, upgrades{}, heartbeatReplies{},
                          messages{}, disconnectPackets{}, closeFrames{};
    explicit SocketIOFixture(bool v1, Mode behavior = Mode::Normal) : version1(v1), mode(behavior)
    {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) throw std::runtime_error("Socket.IO fixture socket failed");
        int enable = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
        {
            const auto error = std::string(std::strerror(errno)); close(listener); listener = -1;
            throw std::runtime_error("Socket.IO fixture loopback bind failed: " + error);
        }
        if (listen(listener, 16)) throw std::runtime_error("Socket.IO fixture listen failed");
        socklen_t length = sizeof(address); getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
        acceptThread = std::thread([this] {
            while (!stopped)
            {
                const int fd = accept(listener, nullptr, nullptr); if (fd < 0) break;
#if defined(SO_NOSIGPIPE)
                int enable = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif
                int noDelay = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
                timeval timeout{5, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                std::lock_guard lock(mutex); connections.emplace_back(fd);
                workers.emplace_back([this, fd] {
                    try { serve(fd); } catch (...) {}
                    shutdown(fd, SHUT_RDWR);
                    { std::lock_guard lock(mutex); std::erase(connections, fd); }
                    close(fd);
                });
            }
        });
    }
    ~SocketIOFixture()
    {
        stopped = true; shutdown(listener, SHUT_RDWR); close(listener);
        if (acceptThread.joinable()) acceptThread.join();
        { std::lock_guard lock(mutex); for (int fd : connections) shutdown(fd, SHUT_RDWR); }
        for (auto& worker : workers) if (worker.joinable()) worker.join();
    }
    bool saw(std::string_view wire)
    {
        std::lock_guard lock(mutex);
        return std::find(received.begin(), received.end(), wire) != received.end();
    }
    std::vector<std::string> packets()
    {
        std::lock_guard lock(mutex);
        return received;
    }
    std::string url() const { return "ws://127.0.0.1:" + std::to_string(port); }
};
}  // namespace axjs::tests
