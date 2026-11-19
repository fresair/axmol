# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(axmol_spidermonkey_smoke LANGUAGES CXX)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." spidermonkey)
add_executable(axmol-spidermonkey-smoke runtime.cpp)
target_compile_features(axmol-spidermonkey-smoke PRIVATE cxx_std_23)
target_link_libraries(axmol-spidermonkey-smoke PRIVATE axmol::spidermonkey)
# The macro is propagated from the imported package, even across directory scopes.
target_compile_definitions(axmol-spidermonkey-smoke PRIVATE AX_SM_JIT=AX_JS_SPIDERMONKEY_JIT)
add_executable(axmol-jscompile ../bytecode.cpp)
target_compile_features(axmol-jscompile PRIVATE cxx_std_23)
target_link_libraries(axmol-jscompile PRIVATE axmol::spidermonkey)
enable_testing()
add_test(NAME spidermonkey-runtime COMMAND axmol-spidermonkey-smoke)
