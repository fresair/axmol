// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#pragma once
#include <jsapi.h>
#include <string>
#include <string_view>
namespace ax::rhi { class ProgramState; }
namespace axjs {
/** The v3 GL API records real RHI commands; shader source must have a matching
 * compiled archive in script/legacy-gl/shaders. */
bool registerLegacyGL(JSContext*, JS::HandleObject);
bool findLegacyShaderArchives(std::string_view vertexSource, std::string_view fragmentSource,
                              std::string_view defines, std::string& vertexPath, std::string& fragmentPath);
/** Preload the actual compiled program and add the old sampler name as a
 * reflected alias for native Pass texture binding, before creating a Material. */
bool prepareLegacyShaderProgramAliases(std::string_view vertexPath, std::string_view fragmentPath);
/** Install reflected legacy time/random uniforms on the actual draw-time state. */
bool configureLegacyShaderProgramState(JSContext*, ax::rhi::ProgramState*);
}
