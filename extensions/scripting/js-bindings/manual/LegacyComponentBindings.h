/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <jsapi.h>

namespace axjs
{
/** Register the real ax::Component-backed legacy cc.ComponentJS type. */
bool registerLegacyComponent(JSContext* cx, JS::HandleObject global);
}
