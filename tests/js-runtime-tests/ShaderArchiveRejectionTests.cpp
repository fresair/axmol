/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 * SPDX-License-Identifier: MIT */
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/WeakPtr.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/renderer/ProgramManager.h"
#include "axmol/renderer/Shaders.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/rhi/SamplerRegistry.h"
#include "axmol/rhi/ShaderCache.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Inspect the real caches without changing their production interfaces.
class ShaderCacheProbe final : public ax::rhi::ShaderCache
{
public:
    size_t size() const { return _cachedShaders.size(); }
};
class ProgramManagerProbe final : public ax::ProgramManager
{
public:
    size_t size() const { return _cachedPrograms.size(); }
};

std::vector<uint8_t> read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(input.good(), "Cannot read shader fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(output.good(), "Cannot write shader fixture");
}

}

int main(int argc, char** argv)
{
#ifndef NDEBUG
    // The existing parser/load assertions intentionally abort Debug builds.
    // Test the release failure contract with those original assertions intact.
    std::cerr << "SHADER_ARCHIVE_REJECTION_SKIP requires a Release or RelWithDebInfo engine\n";
    return 77;
#else
    if (argc != 3 && argc != 5)
    {
        std::cerr << "usage: shader-test <current-axslc-dir> <fixture-dir> [<original-old-vertex> <original-old-fragment>]\n";
        return 2;
    }
    int result = 1;
    try
    {
        ax::AutoreleasePool pool("shader archive rejection");
        ax::rhi::GraphicsCore::setPreferredBackend(ax::rhi::GraphicsBackend::Metal);
        ax::rhi::GraphicsCore::initialize();
        check(ax::rhi::GraphicsCore::device() && ax::rhi::GraphicsCore::isMetal(), "Metal device unavailable");
        auto* files = ax::FileUtils::getInstance();
        const auto vertex = std::filesystem::absolute(std::filesystem::path(argv[1]) / ax::positionTextureColor_vs);
        const auto fragment = std::filesystem::absolute(std::filesystem::path(argv[1]) / ax::positionTextureColor_fs);
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        const auto vertexBytes = read(vertex);
        const auto fragmentBytes = read(fragment);
        check(vertexBytes.size() >= 16 && fragmentBytes.size() >= 16, "Positive archive header missing");

        ShaderCacheProbe cache;
        {
            auto source = files->getDataFromFile(vertex.string());
            auto* first = cache.acquireVertexShaderModule(source);
            check(first && first->isCompiled() && first->getReferenceCount() == 2, "Valid shader/cache ownership");
            ax::WeakPtr<ax::rhi::ShaderModule> weak(first);
            source = files->getDataFromFile(vertex.string());
            auto* second = cache.acquireVertexShaderModule(source);
            check(first == second && first->getReferenceCount() == 3 && cache.size() == 1, "Valid shader cache identity");
            second->release();
            first->release();
            cache.removeUnusedShader();
            check(cache.size() == 0 && !weak.get(), "Unused valid module released exactly once");
        }

        ProgramManagerProbe manager;
        auto* valid = manager.loadProgram(vertex.string(), fragment.string(), ax::rhi::VertexLayoutKind::Sprite);
        check(valid && valid->isValid() && manager.size() == 1 && valid->getReferenceCount() == 1,
              "Valid program/cache ownership");
        check(manager.loadProgram(vertex.string(), fragment.string(), ax::rhi::VertexLayoutKind::Sprite) == valid &&
                  valid->getReferenceCount() == 1,
              "Valid program cache identity");

        size_t rejected = 0;
        const auto reject = [&](const std::filesystem::path& badVertex, const std::filesystem::path& badFragment,
                                bool fragmentFailure) {
            // Reacquire fresh Data each time: module constructors consume it.
            // A failed first acquisition must not poison a subsequent attempt.
            for (unsigned attempt = 0; attempt < 2; ++attempt)
            {
                auto source = files->getDataFromFile((fragmentFailure ? badFragment : badVertex).string());
                auto* module = fragmentFailure ? cache.acquireFragmentShaderModule(source) : cache.acquireVertexShaderModule(source);
                check(!module && cache.size() == 0, "Failed shader escaped or entered the module cache");
                auto* program = manager.loadProgram(badVertex.string(), badFragment.string(), ax::rhi::VertexLayoutKind::Sprite);
                check(!program && manager.size() == 1, "Failed program escaped or entered the program cache");
            }
            ++rejected;
        };

        // The retained pre-4.x archives have the AXSC magic. Mutating only
        // that field supplies a durable fixture without checking in binaries.
        auto bad = vertexBytes;
        bad[3] = 'C';
        write(output / "old-magic.vert.sc", bad);
        reject(output / "old-magic.vert.sc", fragment, false);
        write(output / "short-header.vert.sc", {vertexBytes.begin(), vertexBytes.begin() + 7});
        reject(output / "short-header.vert.sc", fragment, false);
        write(output / "empty.vert.sc", {});
        reject(output / "empty.vert.sc", fragment, false);
        write(output / "empty.frag.sc", {});
        reject(vertex, output / "empty.frag.sc", true);
        if (argc == 5)
        {
            check(read(argv[3]).size() > 4 && read(argv[4]).size() > 4, "Original old archives are missing");
            reject(argv[3], fragment, false);
            reject(vertex, argv[4], true);
        }

        // Correcting a failed resource at the same path must work on retry.
        write(output / "empty.vert.sc", vertexBytes);
        auto* repaired = manager.loadProgram((output / "empty.vert.sc").string(), fragment.string(), ax::rhi::VertexLayoutKind::Sprite);
        check(repaired && repaired->isValid() && manager.size() == 2, "Failed resource poisoned the program cache");
        check(manager.loadProgram(vertex.string(), fragment.string(), ax::rhi::VertexLayoutKind::Sprite) == valid && valid->isValid(),
              "Negative loads changed the valid cached program");
        std::cout << "SHADER_ARCHIVE_REJECTION_PASS cases=" << rejected
                  << " retries=2 valid_module_identity=1 valid_program_identity=1 recovery=1\n";
        result = 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SHADER_ARCHIVE_REJECTION_FAIL " << error.what() << '\n';
    }
    ax::ProgramManager::destroyInstance();
    ax::rhi::ShaderCache::destroyInstance();
    ax::rhi::SamplerRegistry::destroyInstance();
    ax::rhi::GraphicsCore::shutdown();
    return result;
#endif
}
