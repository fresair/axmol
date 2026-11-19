# Copyright (c) 2019-present Simdsoft Limited.
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.22...4.4)
set(APP_NAME Dummy)
project(${APP_NAME})

if(XCODE)
  set(CMAKE_XCODE_GENERATE_TOP_LEVEL_PROJECT_ONLY TRUE)
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
set(_AX_JS_PROJ TRUE)
set(_AX_USE_PREBUILT FALSE)

include(AXGameEngineOptions)
set(AX_ENABLE_EXT_JS ON CACHE BOOL "Build SpiderMonkey JavaScript bindings" FORCE)
set(AX_WITH_TIFF ON CACHE BOOL "Decode embedded TIFF images in legacy JavaScript particle resources" FORCE)
include(AXGameEngineSetup)

file(GLOB_RECURSE GAME_HEADER CONFIGURE_DEPENDS Source/*.h Source/*.hpp)
file(GLOB_RECURSE GAME_SOURCE CONFIGURE_DEPENDS Source/*.cpp Source/*.c)
if(APPLE)
  file(GLOB_RECURSE GAME_OBJC_SOURCE CONFIGURE_DEPENDS Source/*.mm)
  list(APPEND GAME_SOURCE ${GAME_OBJC_SOURCE})
endif()
set(GAME_INC_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set(content_folder "${CMAKE_CURRENT_SOURCE_DIR}/Content")
file(GLOB_RECURSE js_content_sources CONFIGURE_DEPENDS "${content_folder}/*")

if(APPLE)
  ax_mark_multi_resources(common_content_files RES_TO "Resources" FOLDERS ${content_folder})
elseif(WINDOWS)
  ax_mark_multi_resources(common_content_files RES_TO "Content" FOLDERS ${content_folder})
endif()

include(AXGameSourceSetup)
set(APP_SOURCES ${GAME_HEADER} ${GAME_SOURCE})
include(AXGameTargetSetup)
ax_setup_app_config(${APP_NAME})
include(AXGamePlatformSetup)
include(AXGameFinalSetup)
