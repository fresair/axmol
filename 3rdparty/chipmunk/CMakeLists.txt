# Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# Pinned Chipmunk 7.0.1 provides the legacy JavaScript cp namespace. Axmol's
# primary Physics2D backend remains Box2D; only axjs consumes this target.
file(GLOB _ax_chipmunk_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/*.c")
add_library(ax_chipmunk STATIC ${_ax_chipmunk_sources})
add_library(axmol::chipmunk ALIAS ax_chipmunk)
target_include_directories(ax_chipmunk PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/include>")
target_compile_features(ax_chipmunk PRIVATE c_std_99)
target_compile_definitions(ax_chipmunk PUBLIC CP_USE_DOUBLES=1 CP_USE_CGTYPES=0)
# The upstream 7.0.1 HastySpace NEON test recognizes Apple's __arm64 spelling;
# Android Clang defines __aarch64__. Keep the native ARM64 solver available.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    target_compile_definitions(ax_chipmunk PRIVATE __arm64=1)
endif()
set_target_properties(ax_chipmunk PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT APPLE)
    target_link_libraries(ax_chipmunk PUBLIC m)
endif()
