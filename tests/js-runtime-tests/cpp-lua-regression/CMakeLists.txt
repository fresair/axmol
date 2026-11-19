# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(axmol_cpp_lua_headless_regression LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()
get_filename_component(_AX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(AX_REGRESSION_SDK "${_AX_ROOT}/build_js" CACHE PATH "Existing tested Axmol SDK; imported read-only")
set(MACOSX TRUE)
set(_1kfetch_cache_dir "${_AX_ROOT}/cache")
find_package(Threads REQUIRED)
include("${_AX_ROOT}/1k/fetch.cmake")
foreach(_dependency IN ITEMS zlib jpeg-turbo openssl curl opus)
    add_subdirectory("${_AX_ROOT}/3rdparty/${_dependency}" "sdk-import/${_dependency}")
endforeach()
include("${AX_REGRESSION_SDK}/AxmolTargets.cmake")
set(_unit_sources
    axmol/2d/NodeTests.cpp
    axmol/base/MapTests.cpp
    axmol/base/UTF8Tests.cpp
    axmol/base/UtilsTests.cpp
    axmol/base/ValueTests.cpp
    axmol/base/VectorTests.cpp
    axmol/base/WeakPtrTests.cpp
    axmol/math/FastRNGTests.cpp
    axmol/math/MathUtilTests.cpp
    axmol/network/UriTests.cpp
    axmol/ui/UIHelperTests.cpp
    axmol/tlx/ContainerTests.cpp
    axmol/tlx/SplitTests.cpp
)
list(TRANSFORM _unit_sources PREPEND "${_AX_ROOT}/tests/unit-tests/Source/")
add_executable(cpp-headless-regression CppMain.cpp NativeLifecycleTests.cpp
    "${_AX_ROOT}/tests/unit-tests/Source/TestUtils.cpp" ${_unit_sources})
target_include_directories(cpp-headless-regression PRIVATE "${_AX_ROOT}/tests/unit-tests/Source")
target_link_libraries(cpp-headless-regression PRIVATE axmol doctest)
target_precompile_headers(cpp-headless-regression PRIVATE "<axmol/axmol.h>")

add_executable(lua-headless-regression LuaMain.cpp "${_AX_ROOT}/tests/lua-tests/Source/LuaBindingSmoke.cpp")
add_executable(lua-restart-regression LuaRestartMain.cpp "${_AX_ROOT}/tests/lua-tests/Source/LuaBindingSmoke.cpp")
option(AX_LUA_RUNTIME_OVERLAY "Compile current Lua runtime while awaiting a read-only SDK refresh" OFF)
if(AX_LUA_RUNTIME_OVERLAY)
    target_sources(lua-headless-regression PRIVATE "${_AX_ROOT}/extensions/scripting/lua-bindings/runtime/axlua_runtime.cpp")
endif()
target_include_directories(lua-headless-regression PRIVATE "${_AX_ROOT}/tests/lua-tests/Source")
target_compile_definitions(lua-headless-regression PRIVATE SOL_NO_CHECK_NUMBER_PRECISION=1 AXMOL_LUA_BINDINGS=1)
target_link_libraries(lua-headless-regression PRIVATE axlua GUI assets-manager spine fairygui sceneext sceneio
    particle3d physics-nodes plainlua lua-cjson)
target_include_directories(lua-restart-regression PRIVATE "${_AX_ROOT}/tests/lua-tests/Source")
target_compile_definitions(lua-restart-regression PRIVATE SOL_NO_CHECK_NUMBER_PRECISION=1 AXMOL_LUA_BINDINGS=1)
target_link_libraries(lua-restart-regression PRIVATE axlua GUI assets-manager spine fairygui sceneext sceneio
    particle3d physics-nodes plainlua lua-cjson)
foreach(_target IN ITEMS cpp-headless-regression lua-headless-regression lua-restart-regression)
    set_target_properties(${_target} PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE SKIP_BUILD_RPATH FALSE
        BUILD_RPATH "${AX_REGRESSION_SDK}/engine/3rdparty/openal")
    add_test(NAME ${_target} COMMAND ${_target})
    set_tests_properties(${_target} PROPERTIES TIMEOUT 90)
endforeach()
