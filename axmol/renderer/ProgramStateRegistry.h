// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#pragma once

#include "axmol/platform/PlatformMacros.h"
#include "axmol/rhi/Program.h"
#include "axmol/rhi/ProgramState.h"

#include <string>
#include <unordered_map>

namespace ax
{

/**
 * @addtogroup _renderer
 * @{
 * #todo: Rename to ProgramStateRegistry
 */
class AX_DLL ProgramStateRegistry
{
public:
    /** returns the shared instance */
    static ProgramStateRegistry* getInstance();

    /** purges the cache. It releases the retained instance. */
    static void destroyInstance();

    void clearPrograms();

    void registerProgram(uint32_t programType, int textureSamplerFlags, uint32_t builtinProgramType);

    rhi::ProgramState* acquireProgramState(uint32_t programType, int textureSamplerFlags);
    uint32_t getProgramType(uint32_t programType, int textureSamplerFlags);

    // Opt-in custom program families. The factory returns a newly owned state,
    // or nullptr when it cannot provide the requested logical sprite program.
    // A plain function pointer deliberately cannot capture a VM or a Node.
    using TextureVariantFactory = rhi::ProgramState* (*)(uint32_t programType, int textureSamplerFlags);
    void registerTextureVariant(rhi::Program* program, TextureVariantFactory factory);
    rhi::ProgramState* acquireTextureVariant(rhi::Program* currentProgram,
                                            uint32_t programType,
                                            int textureSamplerFlags);

protected:
    std::unordered_map<uint32_t, uint32_t> _registry;
};

// end of _renderer group
/// @}
}  // namespace ax
