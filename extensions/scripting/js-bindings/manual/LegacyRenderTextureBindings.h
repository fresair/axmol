// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include <jsapi.h>
namespace ax
{
struct SceneRenderState;
class Vec2;
}
namespace axjs
{
bool registerLegacyRenderTexture(JSContext*, JS::HandleObject);
namespace legacyRenderTexture
{
// Construct the old canvas-to-target projection without changing a Camera.
// Exposed only to native binding tests, not as a JavaScript/native engine API.
bool makeRenderState(const ax::SceneRenderState&, const ax::Vec2& canvasPixels,
                     const ax::Vec2& targetPixels, bool flipY, ax::SceneRenderState&);
}
}
