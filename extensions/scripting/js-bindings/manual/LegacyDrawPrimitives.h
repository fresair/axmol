// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once

#include "axmol/2d/DrawNode.h"
#include <span>

struct cpSpace;

namespace axjs
{
// Legacy DrawNode widths are radii/half-widths, independently of the modern
// DrawNode thickness scale and hairline shortcut. No JS execution is required.
ax::Color legacyPrimitiveColor(const ax::Color& value);
[[nodiscard]] bool appendLegacySegment(ax::DrawNode& node, const ax::Vec2& from,
                                       const ax::Vec2& to, float radius, const ax::Color& color);
// nullptr denotes success; errors use the existing JS adapter's exact messages.
[[nodiscard]] const char* appendLegacyPolygon(ax::DrawNode& node, std::span<const ax::Vec2> points,
                                             const ax::Color& fill, float borderWidth,
                                             const ax::Color& border);
// Append the fixed-v3 PhysicsDebugNode shapes/constraints. The caller clears;
// no renderer, JS callback, simulation step, or ownership transfer is performed.
void appendLegacyPhysicsDebugSpace(ax::DrawNode& node, cpSpace* space);
}
