// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include "axmol/renderer/ProgramStateRegistry.h"
#include "axmol/renderer/ProgramManager.h"
#include "axmol/base/WeakPtr.h"

namespace ax
{

static ProgramStateRegistry* _sharedStateRegistry = nullptr;
namespace
{
struct TextureVariantEntry
{
    WeakPtr<rhi::Program> owner;
    ProgramStateRegistry::TextureVariantFactory factory;
};
// Keep the opt-in extension out of object layouts. Weak generations prevent a
// recycled Program address from inheriting another program's compatibility.
std::unordered_map<rhi::Program*, TextureVariantEntry> textureVariants;
}
/** returns the shared instance */
ProgramStateRegistry* ProgramStateRegistry::getInstance()
{
    if (_sharedStateRegistry)
        return _sharedStateRegistry;

    _sharedStateRegistry = new ProgramStateRegistry();
    return _sharedStateRegistry;
}

/** purges the cache. It releases the retained instance. */
void ProgramStateRegistry::destroyInstance()
{
    textureVariants.clear();
    AX_SAFE_DELETE(_sharedStateRegistry);
}

void ProgramStateRegistry::registerProgram(uint32_t programType, int textureSamplerFlags, uint32_t builtinProgramType)
{
    uint32_t key = (static_cast<uint32_t>(programType) << 16) | textureSamplerFlags;
    auto it      = this->_registry.find(key);
    if (it == this->_registry.end())
        this->_registry.emplace(key, builtinProgramType);
    else
        it->second = builtinProgramType;
}

void ProgramStateRegistry::clearPrograms()
{
    this->_registry.clear();
    // Cache eviction does not destroy Programs still owned by live nodes.
    // Keep their opt-in identity until the Program expires; a later format
    // change must still resolve the appropriate member of the family.
    std::erase_if(textureVariants, [](const auto& entry) { return entry.second.owner.expired(); });
}

void ProgramStateRegistry::registerTextureVariant(rhi::Program* program, TextureVariantFactory factory)
{
    if (!program || !factory || program->getProgramType() < rhi::ProgramType::CUSTOM_PROGRAM)
        return;
    std::erase_if(textureVariants, [](const auto& entry) { return entry.second.owner.expired(); });
    textureVariants.insert_or_assign(program, TextureVariantEntry{WeakPtr<rhi::Program>(program), factory});
}

rhi::ProgramState* ProgramStateRegistry::acquireTextureVariant(rhi::Program* currentProgram,
                                                             uint32_t programType,
                                                             int textureSamplerFlags)
{
    auto it = textureVariants.find(currentProgram);
    if (it == textureVariants.end())
        return nullptr;
    if (it->second.owner.get() != currentProgram)
    {
        textureVariants.erase(it);
        return nullptr;
    }
    // The factory can load and register another member, invalidating iterators.
    auto factory = it->second.factory;
    return factory(programType, textureSamplerFlags);
}

rhi::ProgramState* ProgramStateRegistry::acquireProgramState(uint32_t programType, int textureSamplerFlags)
{
    uint32_t key = ((programType) << 16) | textureSamplerFlags;
    auto it      = this->_registry.find(key);
    if (it != this->_registry.end())
    {
        auto fallback = it->second;
        if (fallback)
            return new rhi::ProgramState(axpm->getBuiltinProgram(fallback));
    }

    return new rhi::ProgramState(axpm->getBuiltinProgram(programType));
}

uint32_t ProgramStateRegistry::getProgramType(uint32_t programType, int textureSamplerFlags)
{
    uint32_t key = (static_cast<uint32_t>(programType) << 16) | textureSamplerFlags;
    auto it      = this->_registry.find(key);
    if (it != this->_registry.end())
    {
        auto fallback = it->second;
        if (fallback)
            return fallback;
    }
    return programType;
}
// end of _renderer group
/// @}
}  // namespace ax
