/*
 * Copyright (c) 2026 Axmol Engine contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <string>
#include "jsapi.h"

namespace axjs::debugger
{
struct Options
{
    std::uint16_t port = 5086;
    std::string bindAddress = "127.0.0.1";
    // Hold the caller before main.js until the client has installed breakpoints
    // and resumed the thread. A disconnected client releases this startup wait.
    bool waitForConnection = false;
};

// All methods run on the JS/Director thread. The transport thread never uses JSAPI.
// Initialize only after FileUtils can resolve script/jsb_debugger.js.
bool initialize(JSContext* cx, JS::HandleObject gameGlobal, const Options& options = {});
void pump();
// Required before the game global, context, or runtime is destroyed.
void shutdown();
bool isEnabled();
}  // namespace axjs::debugger
