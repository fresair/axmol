/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include "extensions/ExtensionExport.h"
#include "axmol/base/Object.h"
#include "axmol/base/Logging.h"
#include "axmol/base/Director.h"
#include "axmol/2d/Layer.h"
#include "axmol/2d/Sprite.h"
#include "axmol/2d/Label.h"
#include "axmol/2d/Menu.h"
#include "axmol/2d/MenuItem.h"
#include "axmol/2d/ParticleSystemQuad.h"
#include <cstdarg>
#include <cstdio>

namespace ax::ext {
inline void ccbLog(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    AXLOGD("{}", buffer);
}
inline ax::rhi::BlendFactor ccbBlendFactor(int value)
{
    using Factor = ax::rhi::BlendFactor;
    switch (value)
    {
    case 0: return Factor::ZERO;
    case 1: return Factor::ONE;
    case 0x0300: return Factor::SRC_COLOR;
    case 0x0301: return Factor::ONE_MINUS_SRC_COLOR;
    case 0x0302: return Factor::SRC_ALPHA;
    case 0x0303: return Factor::ONE_MINUS_SRC_ALPHA;
    case 0x0304: return Factor::DST_ALPHA;
    case 0x0305: return Factor::ONE_MINUS_DST_ALPHA;
    case 0x0306: return Factor::DST_COLOR;
    case 0x0307: return Factor::ONE_MINUS_DST_COLOR;
    case 0x0308: return Factor::SRC_ALPHA_SATURATE;
    default: return Factor::ONE;
    }
}
inline void ccbSetColorRGB(ax::Node* node, ax::Color32 color)
{
    color.a = node->getOpacity();
    node->setColor(color);
}
}  // namespace ax::ext
#define CCB_LOG(...) ::ax::ext::ccbLog(__VA_ARGS__)
