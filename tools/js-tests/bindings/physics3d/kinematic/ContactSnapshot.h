// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once

#include "jsapi.h"

namespace physics3dtest
{
// Test-App-only read helper. It installs no listeners and changes no physics
// flags. Call synchronously with the live event from a native contact callback.
bool registerContactSnapshot(JSContext* cx, JS::HandleObject global);
}
