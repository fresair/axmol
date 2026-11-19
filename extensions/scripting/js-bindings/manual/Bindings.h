/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <jsapi.h>
namespace axjs
{
bool registerAll(JSContext*, JS::HandleObject);
bool registerCore(JSContext*, JS::HandleObject);
bool registerInput(JSContext*, JS::HandleObject);
bool registerNetwork(JSContext*, JS::HandleObject);
bool registerExtensions(JSContext*, JS::HandleObject);
bool registerPlatform(JSContext*, JS::HandleObject);
}  // namespace axjs
