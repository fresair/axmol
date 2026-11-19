// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include <jsapi.h>
#include "axmol/2d/ActionInterval.h"
namespace axjs
{
ax::Sequence* createLegacySequence(const ax::Vector<ax::FiniteTimeAction*>&);
ax::Spawn* createLegacySpawn(const ax::Vector<ax::FiniteTimeAction*>&);
bool registerLegacyActionDurations(JSContext*);
}
