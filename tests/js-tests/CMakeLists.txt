# Copyright (c) 2019-present Simdsoft Limited.
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22...4.4)
set(APP_NAME js-tests)
project(${APP_NAME})

if(NOT DEFINED BUILD_ENGINE_DONE)
  if(XCODE)
    set(CMAKE_XCODE_GENERATE_TOP_LEVEL_PROJECT_ONLY TRUE)
  endif()
  if(NOT DEFINED _AX_ROOT OR _AX_ROOT STREQUAL "")
    set(_AX_ROOT "$ENV{AX_ROOT}")
  endif()
  if(_AX_ROOT STREQUAL "")
    get_filename_component(_AX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  endif()
  get_filename_component(_AX_ROOT "${_AX_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_AX_ROOT}/cmake/Modules/AXBuildSet.cmake")
    message(FATAL_ERROR "Set _AX_ROOT or AX_ROOT to the Axmol engine checkout for this test project")
  endif()
  set(CMAKE_MODULE_PATH "${_AX_ROOT}/cmake/Modules")
  include(AXBuildSet)
  set(AX_ENABLE_EXT_JS ON CACHE BOOL "Build SpiderMonkey JavaScript bindings" FORCE)
  set(AX_ENABLE_EXT_COCOSBUILDER ON CACHE BOOL "Build the complete v3 JS test CocosBuilder reader" FORCE)
  set(AX_WITH_TIFF ON CACHE BOOL "Decode original embedded TIFF particle resources" FORCE)
  set(_AX_JS_PROJ TRUE)
  if(DEFINED AX_PREBUILT_DIR AND NOT AX_PREBUILT_DIR STREQUAL "")
    set(_AX_USE_PREBUILT TRUE)
    include(AXLinkHelpers)
  else()
    set(_AX_USE_PREBUILT FALSE)
    add_subdirectory("${_AX_ROOT}/axmol" "${ENGINE_BINARY_PATH}/axmol")
  endif()
endif()

option(AX_JS_TEST_NATIVE_DIAGNOSTICS "Build the native material reader used by the materialTexture GPU regression" OFF)
option(AX_JS_TEST_PERFORMANCE_METRICS "Build shared Android JSB comparison process timing helper" OFF)
set(GAME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Source/AppDelegate.cpp")
if(AX_JS_TEST_NATIVE_DIAGNOSTICS)
  list(APPEND GAME_SOURCE
    "${_AX_ROOT}/tools/js-tests/runtime-audits/legacy-gl/material-texture-diagnostics.cpp")
endif()
if(AX_JS_TEST_PERFORMANCE_METRICS)
  if(NOT ANDROID AND NOT APPLE)
    message(FATAL_ERROR "JSB comparison process metrics currently target Android and Apple")
  endif()
  list(APPEND GAME_SOURCE "${_AX_ROOT}/tools/js-benchmarks/jsb-comparison/process-stats.cpp")
endif()
set(GAME_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Source/AppDelegate.h")
if(AX_JS_TEST_CCB_TIMELINE_AUDIO_TRACE OR AX_JS_TEST_PARTICLE3D_VK_TRACE)
  message(FATAL_ERROR "Specialized timeline/Particle3D trace observers are not part of js-tests")
endif()
if(APPLE)
  list(APPEND GAME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Source/NativeBridge.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/LegacyNativeBridge.mm")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/LegacyNativeBridge.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
set(GAME_INC_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Source")
if(AX_JS_TEST_NATIVE_DIAGNOSTICS OR AX_JS_TEST_PERFORMANCE_METRICS)
  list(APPEND GAME_INC_DIRS "${_AX_ROOT}")
endif()
set(content_folder "${CMAKE_CURRENT_SOURCE_DIR}/Content")
set(engine_script_folder "${_AX_ROOT}/extensions/scripting/js-bindings/script")
if(NOT EXISTS "${content_folder}/script")
  file(CREATE_LINK "${engine_script_folder}" "${content_folder}/script"
    SYMBOLIC COPY_ON_ERROR RESULT script_link_result)
  if(NOT script_link_result STREQUAL "0")
    message(FATAL_ERROR "Cannot stage JavaScript framework: ${script_link_result}")
  endif()
endif()
include("${_AX_ROOT}/tools/js-tests/Resources.cmake")
ax_js_test_prepare_resources("${_AX_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
file(GLOB_RECURSE js_test_resources CONFIGURE_DEPENDS
  "${content_folder}/*" "${engine_script_folder}/*")
if(APPLE)
  ax_mark_multi_resources(common_content_files RES_TO "Resources" FOLDERS ${content_folder})
endif()
set(platform_source_folder "${_AX_ROOT}/templates/common")
if(ANDROID)
  set(platform_source_folder "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
include("${_AX_ROOT}/templates/js/cmake/modules/AXGameSourceSetup.cmake")
set(APP_SOURCES ${GAME_HEADER} ${GAME_SOURCE})
include("${_AX_ROOT}/templates/js/cmake/modules/AXGameTargetSetup.cmake")
if(MACOSX AND XCODE)
  # The CLI uses Xcode's inherited products directory. Keep TARGET_FILE (used
  # by CTest) in sync with the bundle that xcodebuild actually creates.
  ax_setup_app_config(${APP_NAME} CONSOLE RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/build")
else()
  ax_setup_app_config(${APP_NAME} CONSOLE)
endif()
target_compile_definitions(${APP_NAME} PRIVATE
  AX_JS_TEST_NATIVE_DIAGNOSTICS=$<BOOL:${AX_JS_TEST_NATIVE_DIAGNOSTICS}>
  AX_JS_TEST_PERFORMANCE_METRICS=$<BOOL:${AX_JS_TEST_PERFORMANCE_METRICS}>)
if(MACOSX)
  target_compile_definitions(${APP_NAME} PRIVATE
    AXJS_TEST_WRITABLE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/writable")
endif()

if(APPLE)
  set_target_properties(${APP_NAME} PROPERTIES RESOURCE "${APP_UI_RES}")
  set_xcode_property(${APP_NAME} PRODUCT_BUNDLE_IDENTIFIER "dev.axmol.jstests")
  if(MACOSX)
    set_target_properties(${APP_NAME} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${platform_source_folder}/proj.ios_mac/mac/Info.plist"
      MACOSX_BUNDLE_GUI_IDENTIFIER "dev.axmol.jstests"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
      MACOSX_BUNDLE_BUNDLE_VERSION "1")
  else()
    string(TOLOWER "${CMAKE_OSX_SYSROOT}" _ax_js_ios_sdk_lower)
    if(_ax_js_ios_sdk_lower MATCHES "iphonesimulator")
      set(AX_JS_IOS_BUNDLE_PLATFORM "iPhoneSimulator")
    else()
      set(AX_JS_IOS_BUNDLE_PLATFORM "iPhoneOS")
    endif()
    set_target_properties(${APP_NAME} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/proj.ios/Info.plist"
      MACOSX_BUNDLE_GUI_IDENTIFIER "dev.axmol.jstests")
    set_xcode_property(${APP_NAME} ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
    set_xcode_property(${APP_NAME} TARGETED_DEVICE_FAMILY "1,2")
  endif()
endif()
include("${_AX_ROOT}/templates/js/cmake/modules/AXGameFinalSetup.cmake")

if(MACOSX)
  enable_testing()
  add_test(NAME js-binding-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
  set_tests_properties(js-binding-smoke PROPERTIES
    ENVIRONMENT "AXMOL_JS_BINDING_SMOKE=1"
    TIMEOUT 45)
  option(AX_JS_GRAPHICS_TESTS "Register JavaScript tests requiring a graphical session" OFF)
  if(AX_JS_GRAPHICS_TESTS)
    add_test(NAME js-material-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-material-smoke PROPERTIES
      ENVIRONMENT "AXMOL_JS_MATERIAL_SMOKE=1"
      TIMEOUT 130)
    add_test(NAME js-studio-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-studio-smoke PROPERTIES
      ENVIRONMENT "AXMOL_JS_STUDIO_SMOKE=1"
      TIMEOUT 45)
    add_test(NAME js-scene-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-scene-smoke PROPERTIES
      ENVIRONMENT "AXMOL_JS_SCENE_SMOKE=1"
      TIMEOUT 45)
    add_test(NAME js-legacy-hello-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-legacy-hello-smoke PROPERTIES
      ENVIRONMENT "AXMOL_JS_LEGACY_HELLO=1;AXMOL_JS_SCENE_SMOKE=1"
      TIMEOUT 45)
    add_test(NAME js-v3-full-menu-smoke COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-v3-full-menu-smoke PROPERTIES
      ENVIRONMENT "AXMOL_JS_V3_FULL_SUITE=1;AXMOL_JS_V3_MENU_SMOKE=1"
      TIMEOUT 90)
    add_test(NAME js-v3-full-suite COMMAND $<TARGET_FILE:${APP_NAME}>)
    set_tests_properties(js-v3-full-suite PROPERTIES
      ENVIRONMENT "AXMOL_JS_V3_FULL_SUITE=1;AXMOL_JS_V3_AUTORUN=1;AXMOL_JS_V3_REGISTRY=1"
      TIMEOUT 1850)
  endif()
endif()
