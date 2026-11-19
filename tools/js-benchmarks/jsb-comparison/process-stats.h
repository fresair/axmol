// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include "jsapi.h"

// Measurement only: the same implementation is compiled into both JSB Apps.
bool registerJsbComparisonProcessStats(JSContext* cx, JS::HandleObject global);
