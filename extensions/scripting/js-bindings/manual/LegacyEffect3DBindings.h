// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include <jsapi.h>

namespace axjs
{
/** Native v3 js-tests EffectSprite3D/Effect3DOutline extension, implemented
 * using MeshRenderer's real ordered material passes on the selected RHI. */
bool registerLegacyEffect3D(JSContext*, JS::HandleObject);
}
