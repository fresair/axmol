/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include "jsapi.h"

namespace axjs
{
bool registerLegacyAudio(JSContext* cx, JS::HandleObject global);
}
