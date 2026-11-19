// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include <jsapi.h>

namespace axjs::tests
{
// Test-only native inspection. It reads the actual Material/ProgramState and
// optional MeshRenderer; it does not alter shaders, textures or render state.
bool registerLegacyMaterialTextureDiagnostics(JSContext* cx, JS::HandleObject global);
}
