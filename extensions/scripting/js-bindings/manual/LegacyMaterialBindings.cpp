/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyMaterialBindings.h"
#include "manual/LegacyGLBindings.h"
#include "manual/ManualUtils.h"
#include "axmol/base/Properties.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/renderer/Material.h"
#include "axmol/renderer/Pass.h"
#include "axmol/rhi/ProgramState.h"

#include <memory>
#include <string>
#include <vector>

namespace axjs
{
namespace
{
struct ShaderReplacement
{
    ax::Properties* properties;
    std::string vertexPath;
    std::string fragmentPath;
};

// Properties iteration is stateful. Every walk leaves the complete owned tree
// ready for the native Material parser, including its nested shader namespaces.
void rewindTree(ax::Properties* properties)
{
    properties->rewind();
    while (auto* child = properties->getNextNamespace())
        rewindTree(child);
    properties->rewind();
}

class RewindOnExit
{
public:
    explicit RewindOnExit(ax::Properties* properties) : _properties(properties) {}
    ~RewindOnExit() { rewindTree(_properties); }

private:
    ax::Properties* _properties;
};

bool readShader(JSContext* cx, std::string_view path, std::string& source)
{
    auto* files = ax::FileUtils::getInstance();
    if (path.empty() || !files->isFileExist(path))
    {
        JS_ReportError(cx, "Material shader file is missing: %s", std::string(path).c_str());
        return false;
    }
    auto data = files->getDataFromFile(path);
    if (data.isNull() || data.size() == 0)
    {
        JS_ReportError(cx, "Material shader file is empty or unreadable: %s", std::string(path).c_str());
        return false;
    }
    source.assign(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

bool isShaderArchive(std::string_view data)
{
    return data.size() >= 4 && data.substr(0, 4) == "AXSC";
}

bool collectShaders(JSContext* cx, ax::Properties* properties, std::vector<ShaderReplacement>& replacements)
{
    RewindOnExit rewind(properties);
    properties->rewind();
    if (properties->getNamespace() == "shader")
    {
        const std::string vertexPath(properties->getString("vertexShader"));
        const std::string fragmentPath(properties->getString("fragmentShader"));
        std::string vertexSource, fragmentSource;
        if (!readShader(cx, vertexPath, vertexSource) || !readShader(cx, fragmentPath, fragmentSource))
            return false;

        // Modern Axmol archives take the original native path. An old GLSL
        // pair must match the checked source bytes and preprocessor definitions.
        const bool vertexArchive = isShaderArchive(vertexSource);
        const bool fragmentArchive = isShaderArchive(fragmentSource);
        if (!vertexArchive || !fragmentArchive)
        {
            ShaderReplacement replacement{properties, {}, {}};
            if (vertexArchive != fragmentArchive ||
                !findLegacyShaderArchives(vertexSource, fragmentSource, properties->getString("defines"),
                                          replacement.vertexPath, replacement.fragmentPath))
            {
                JS_ReportError(cx, "Material shader source/defines have no compiled legacy archive: %s, %s",
                               vertexPath.c_str(), fragmentPath.c_str());
                return false;
            }
            auto* files = ax::FileUtils::getInstance();
            if (!files->isFileExist(replacement.vertexPath) || !files->isFileExist(replacement.fragmentPath))
                return manual::error(cx, "Material's compiled legacy shader archive is missing");
            replacements.emplace_back(std::move(replacement));
        }
    }
    while (auto* child = properties->getNextNamespace())
        if (!collectShaders(cx, child, replacements))
            return false;
    return true;
}

bool createMaterial(JSContext* cx, std::unique_ptr<ax::Properties> properties, JS::MutableHandleValue result)
{
    if (!properties)
    {
        result.setNull();
        return true;
    }
    // Copying captures inherited variables and preserves raw ${variable}
    // expressions in the caller's Properties. Nothing in that tree is mutated
    // or iterated by either this adapter or the native Material parser.
    std::vector<ShaderReplacement> replacements;
    if (!collectShaders(cx, properties.get(), replacements))
        return false;
    for (const auto& replacement : replacements)
    {
        if (!prepareLegacyShaderProgramAliases(replacement.vertexPath, replacement.fragmentPath))
            return manual::error(cx, "Material's legacy shader requires an initialized graphics device and valid archives");
        replacement.properties->setString("vertexShader", replacement.vertexPath);
        replacement.properties->setString("fragmentShader", replacement.fragmentPath);
    }
    rewindTree(properties.get());
    auto* material = ax::Material::createWithProperties(properties.get());
    if (material)
        for (auto* technique : material->getTechniques())
            for (auto* pass : technique->getPasses())
                if (auto* programState = pass->getProgramState())
                {
                    // Old material samplers are explicit per-pass bindings. A legacy
                    // program may also alias its sampler to the mesh diffuse name;
                    // preserve the parsed texture instead of replacing it with the
                    // mesh's default/dummy texture when setMaterial is called.
                    const auto diffuse = programState->getUniformLocation("u_tex0");
                    const auto sampler = programState->getUniformLocation("u_sampler0");
                    if (diffuse && sampler && diffuse == sampler &&
                        programState->getTextureBindingSets().contains(sampler.location))
                        pass->setAutomaticDiffuseTextureBindingEnabled(false);
                    if (!configureLegacyShaderProgramState(cx, programState))
                        return manual::error(cx, "Material's legacy shader uniforms could not be configured");
                }
    return wrapNative(cx, material, result, NativeOwnership::Factory);
}

bool materialFromProperties(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 1)
        return manual::error(cx, "Material.createWithProperties requires one live Properties object");
    ax::Properties* properties{};
    if (!manual::argument(cx, args, 0, properties) || !properties)
        return manual::error(cx, "Material.createWithProperties requires one live Properties object");
    return createMaterial(cx, std::unique_ptr<ax::Properties>(properties->clone()), args.rval());
}

bool materialFromFilename(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string url;
    if (argc != 1 || !manual::argument(cx, args, 0, url))
        return manual::error(cx, "Material.createWithFilename requires a file URL");
    auto* files = ax::FileUtils::getInstance();
    const auto file = std::string_view(url).substr(0, url.find('#'));
    const auto path = file.empty() ? std::string{} : files->fullPathForFilename(file);
    // Android's asset size lookup requires the resolved assets/... path; the
    // original URL can name a file found through an application's search paths.
    if (path.empty() || files->getFileSize(path) <= 0)
    {
        args.rval().setNull();
        return true;
    }
    std::unique_ptr<ax::Properties> properties(ax::Properties::createNonRefCounted(url));
    if (!properties)
    {
        args.rval().setNull();
        return true;
    }
    // A filename loads the first material when no #namespace selector was used,
    // matching the native Material filename factory's selection rule.
    if (properties->getNamespace().empty())
    {
        properties->rewind();
        auto* first = properties->getNextNamespace();
        if (!first)
        {
            args.rval().setNull();
            return true;
        }
        std::unique_ptr<ax::Properties> selected(first->clone());
        properties = std::move(selected);
    }
    return createMaterial(cx, std::move(properties), args.rval());
}

bool passProgramState(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0)
        return manual::error(cx, "Pass.getGLProgramState requires no arguments");
    return invoke<&ax::Pass::getProgramState>(cx, args);
}
}

bool registerLegacyMaterial(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject constructor(cx, getClassObject<ax::Material>(cx));
    if (!constructor)
        return manual::error(cx, "Material native class has not been registered");
    return manual::function(cx, constructor, "createWithProperties", materialFromProperties, 1) &&
           manual::function(cx, constructor, "createWithFilename", materialFromFilename, 1) &&
           manual::method<ax::Pass>(cx, "getGLProgramState", passProgramState);
}
}  // namespace axjs
