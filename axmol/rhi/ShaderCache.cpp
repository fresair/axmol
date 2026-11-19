/****************************************************************************
 Copyright (c) 2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "axmol/rhi/ShaderCache.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/rhi/axslc-spec.h"

#include "xxhash/xxhash.h"

namespace ax::rhi
{

static ShaderCache* s_instance;

ShaderCache* ShaderCache::getInstance()
{
    if (s_instance)
        return s_instance;
    return (s_instance = new ShaderCache());
}

void ShaderCache::destroyInstance()
{
    AX_SAFE_DELETE(s_instance);
}

ShaderCache::~ShaderCache()
{
    removeAllShaders();
}

void ShaderCache::removeAllShaders()
{
    for (auto& [_, shaderModule] : _cachedShaders)
    {
        AX_SAFE_RELEASE(shaderModule);
    }
    _cachedShaders.clear();
    AXLOGV("purging ShaderCache");
}

rhi::ShaderModule* ShaderCache::acquireVertexShaderModule(Data& shaderSource)
{
    return acquireShaderModule(rhi::ShaderStage::VERTEX, shaderSource);
}

rhi::ShaderModule* ShaderCache::acquireFragmentShaderModule(Data& shaderSource)
{
    return acquireShaderModule(rhi::ShaderStage::FRAGMENT, shaderSource);
}

rhi::ShaderModule* ShaderCache::acquireComputeShaderModule(Data& shaderSource)
{
    return acquireShaderModule(rhi::ShaderStage::COMPUTE, shaderSource);
}

rhi::ShaderModule* ShaderCache::acquireShaderModule(rhi::ShaderStage stage, Data& data)
{
    if (data.size() < 2 * sizeof(uint32_t) + sizeof(axslc::sc_chunk))
        return nullptr;

    const auto key  = XXH64(data.data(), data.size(), 0);
    const auto iter = _cachedShaders.find(key);

    if (_cachedShaders.end() != iter)
    {
        AX_SAFE_RETAIN(iter->second);
        return iter->second;
    }

    ShaderModule* const shader = axdrv->createShaderModule(stage, data);
    if (!shader || !shader->isCompiled())
    {
        // Failed modules must not be reflected, retained by the cache, or
        // returned as usable shaders on a subsequent acquisition.
        AX_SAFE_RELEASE(shader);
        return nullptr;
    }
    shader->setHashValue(key);
    _cachedShaders.emplace(key, shader);

    AX_SAFE_RETAIN(shader);

    return shader;
}

void ShaderCache::removeUnusedShader()
{
    for (auto iter = _cachedShaders.cbegin(); iter != _cachedShaders.cend();)
    {
        auto shaderModule = iter->second;
        if (shaderModule->getReferenceCount() == 1)
        {
            //            AXLOGD("TextureCache: removing unused program");
            shaderModule->release();
            iter = _cachedShaders.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

#if AX_ENABLE_CONTEXT_LOSS_RECOVERY
void ShaderCache::recompileAll()
{
    for (auto& [_, shaderModule] : _cachedShaders)
        shaderModule->recompileShader();
}
#endif

}  // namespace ax::rhi
