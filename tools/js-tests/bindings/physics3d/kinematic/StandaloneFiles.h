// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include "axmol/platform/FileUtils.h"
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
#include <sys/stat.h>
namespace physics3dtest
{
// Native CLI has neither Activity nor AssetManager. These real pushed files
// use the same POSIX FileUtils operations as the existing native runtime tests.
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(std::string root)
    {
        _defaultResRootPath=std::move(root);_writablePath=_defaultResRootPath;
        if(!init())throw std::runtime_error("Physics3D standalone filesystem initialization failed");
    }
    std::string getWritablePath() const override {return _writablePath;}
    std::string getNativeWritableAbsolutePath() const override {return _writablePath;}
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat status{};
        return stat(std::string(path).c_str(),&status)==0&&S_ISREG(status.st_mode);
    }
};
}
#endif
namespace physics3dtest
{
inline std::filesystem::path storagePath(const std::filesystem::path& fallback)
{
    const char* requested=std::getenv("AX_JS_TEST_TEMP_DIR");
    return requested&&*requested?std::filesystem::absolute(requested):fallback;
}
inline void installFiles(const std::filesystem::path& root,const std::filesystem::path& storage)
{
    std::filesystem::create_directories(storage);
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root.string()+"/"));
#endif
    ax::FileUtils::getInstance()->setWritablePath(storage.string()+"/");
}
}
