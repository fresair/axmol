// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "material-texture-diagnostics.h"
#include "manual/ManualUtils.h"
#include "axmol/3d/Mesh.h"
#include "axmol/3d/MeshRenderer.h"
#include "axmol/3d/VertexInputBinding.h"
#include "axmol/renderer/Material.h"
#include "axmol/renderer/Pass.h"
#include "axmol/renderer/Texture2D.h"
#include "axmol/renderer/TextureCache.h"
#include "axmol/rhi/ProgramState.h"
#include "axmol/rhi/Texture.h"
#include <cstring>
#include <stdexcept>

namespace axjs::tests
{
namespace
{
using ax::Value;
using ax::ValueMap;
using ax::ValueVector;

struct KnownTextures
{
    ax::rhi::Texture* boss{};
    ax::rhi::Texture* dummy{};
};

Value textureFacts(ax::rhi::Texture* texture, const KnownTextures& known)
{
    ValueMap fields{{"present", Value(texture != nullptr)}, {"matchesBoss", Value(texture && texture == known.boss)},
                    {"matchesDummy", Value(texture && texture == known.dummy)}};
    if (texture)
    {
        fields.emplace("width", Value(texture->getWidth()));
        fields.emplace("height", Value(texture->getHeight()));
        fields.emplace("pixelFormat", Value(static_cast<int>(texture->getPixelFormat())));
    }
    return Value(std::move(fields));
}

Value uniformFacts(ax::rhi::ProgramState* state, std::string_view name, unsigned count)
{
    const auto location = state->getUniformLocation(name);
    ValueMap facts{{"present", Value(static_cast<bool>(location))}};
    if (!location)
        return Value(std::move(facts));
    facts.emplace("binding", Value(location.location));
    facts.emplace("offset", Value(location.offset));
    facts.emplace("cpuOffset", Value(location.cpuOffset));
    if (location.offset >= 0 && location.cpuOffset >= 0)
    {
        const auto start = static_cast<size_t>(location.offset) + location.cpuOffset;
        const auto& bytes = state->getUniformBuffer();
        if (start <= bytes.size() && count * sizeof(float) <= bytes.size() - start)
        {
            ValueVector values;
            for (unsigned index = 0; index < count; ++index)
            {
                float number;
                std::memcpy(&number, bytes.data() + start + index * sizeof(float), sizeof(number));
                values.emplace_back(number);
            }
            facts.emplace("values", Value(std::move(values)));
        }
    }
    return Value(std::move(facts));
}

Value materialFacts(ax::Material* material, const KnownTextures& known)
{
    ValueMap result{{"name", Value(material->getName())},
                    {"currentTechnique", Value(material->getTechnique()->getName())}};
    ValueVector techniques;
    for (auto* technique : material->getTechniques())
    {
        ValueVector passes;
        for (auto* pass : technique->getPasses())
        {
            auto* state = pass->getProgramState();
            auto* program = state->getProgram();
            const auto builtin = state->getUniformLocation("u_tex0");
            const auto explicitSampler = state->getUniformLocation("u_sampler0");
            ValueMap facts{{"name", Value(pass->getName())}, {"programValid", Value(program->isValid())},
                           {"programType", Value(static_cast<int>(program->getProgramType()))},
                           {"builtinSamplerPresent", Value(static_cast<bool>(builtin))},
                           {"explicitSamplerPresent", Value(static_cast<bool>(explicitSampler))},
                           {"builtinAliasesExplicitSampler", Value(builtin && explicitSampler &&
                               builtin.location == explicitSampler.location && builtin.offset == explicitSampler.offset &&
                               builtin.cpuOffset == explicitSampler.cpuOffset)},
                           {"colorUniform", uniformFacts(state, "u_color", 4)},
                           {"mvpUniform", uniformFacts(state, "u_MVPMatrix", 16)}};
            ValueVector textures;
            for (const auto& [binding, set] : state->getTextureBindingSets())
            {
                ValueVector entries;
                for (size_t index = 0; index < set.texs.size(); ++index)
                {
                    auto texture = textureFacts(set.texs[index], known).asValueMap();
                    texture.emplace("slot", Value(index < set.slots.size() ? set.slots[index] : -1));
                    entries.emplace_back(std::move(texture));
                }
                textures.emplace_back(ValueMap{{"binding", Value(binding)},
                                               {"runtimeLocation", Value(set.runtimeLocation)},
                                               {"textures", Value(std::move(entries))}});
            }
            facts.emplace("textureBindings", Value(std::move(textures)));
            ValueVector inputs;
            for (const auto& [semantic, descriptor] : program->getActiveVertexInputs())
                inputs.emplace_back(ValueMap{{"semantic", Value(semantic.getName())},
                                             {"semanticIndex", Value(static_cast<int>(semantic.index))},
                                             {"location", Value(descriptor.location)},
                                             {"variableType", Value(descriptor.varType)}});
            facts.emplace("reflectedInputs", Value(std::move(inputs)));
            auto* binding = pass->getVertexAttributeBinding();
            facts.emplace("nativeVertexBinding", Value(binding != nullptr));
            if (binding)
                facts.emplace("nativeAttributeMask", Value(binding->getVertexAttribsFlags()));
            passes.emplace_back(std::move(facts));
        }
        techniques.emplace_back(ValueMap{{"name", Value(technique->getName())}, {"passes", Value(std::move(passes))}});
    }
    result.emplace("techniques", Value(std::move(techniques)));
    return Value(std::move(result));
}

bool inspectMaterial(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc < 1 || argc > 2)
        return manual::error(cx, "legacyMaterialTextureFacts expects a live Material and an optional MeshRenderer");
    ax::Material* material{};
    ax::MeshRenderer* renderer{};
    if (!toNative(cx, args[0], &material) || !material ||
        (argc == 2 && !args[1].isNullOrUndefined() && (!toNative(cx, args[1], &renderer) || !renderer)))
        return manual::error(cx, "legacyMaterialTextureFacts requires valid native receivers");
    auto* files = ax::FileUtils::getInstance();
    auto* cache = ax::Director::getInstance()->getTextureCache();
    auto* boss = cache->getTextureForKey(files->fullPathForFilename("Sprite3DTest/boss.png"));
    auto* dummy = cache->getTextureForKey("/dummy-texture");
    KnownTextures known{boss ? boss->getRHITexture() : nullptr, dummy ? dummy->getRHITexture() : nullptr};
    ValueMap facts{{"material", materialFacts(material, known)}, {"bossCached", Value(boss != nullptr)},
                   {"dummyCached", Value(dummy != nullptr)},
                   {"guessedBoss1TextureExists", Value(files->isFileExist("Sprite3DTest/boss1.png"))}};
    if (renderer)
    {
        ValueVector meshes;
        for (auto* mesh : renderer->getMeshes())
        {
            ValueMap fields{{"name", Value(mesh->getName())}, {"stride", Value(mesh->getVertexSizeInBytes())},
                            {"attributeCount", Value(static_cast<int>(mesh->getMeshVertexAttribCount()))}};
            // Mesh::getTexture uses map::at for its diffuse entry. A genuinely
            // untextured model has no entry; this fact is preserved explicitly.
            ax::Texture2D* diffuse{};
            try { diffuse = mesh->getTexture(); }
            catch (const std::out_of_range&) {}
            fields.emplace("diffuse", textureFacts(diffuse ? diffuse->getRHITexture() : nullptr, known));
            if (diffuse)
                fields.emplace("diffusePath", Value(diffuse->getPath()));
            if (auto* meshMaterial = mesh->getMaterial())
                fields.emplace("material", materialFacts(meshMaterial, known));
            meshes.emplace_back(std::move(fields));
        }
        facts.emplace("meshes", Value(std::move(meshes)));
    }
    return toJsValue(cx, facts, args.rval());
}
}

bool registerLegacyMaterialTextureDiagnostics(JSContext* cx, JS::HandleObject global)
{
    return manual::function(cx, global, "legacyMaterialTextureFacts", inspectMaterial, 1);
}
}  // namespace axjs::tests
