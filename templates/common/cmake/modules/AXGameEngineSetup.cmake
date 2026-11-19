# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/axmol")
  set(_AX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/axmol")
  set(_is_axmol_embed TRUE)
  message(STATUS "Building isolated project: ${APP_NAME}")
elseif(NOT DEFINED _AX_ROOT OR _AX_ROOT STREQUAL "")
  set(_AX_ROOT "$ENV{AX_ROOT}")
endif()
if(_AX_ROOT STREQUAL "")
  message(FATAL_ERROR "Run setup.ps1 or pass -D_AX_ROOT=/path/to/axmol to select the engine checkout")
endif()
file(TO_CMAKE_PATH "${_AX_ROOT}" _AX_ROOT)
if(NOT EXISTS "${_AX_ROOT}/axmol/CMakeLists.txt")
  message(FATAL_ERROR "_AX_ROOT does not identify an Axmol engine checkout: ${_AX_ROOT}")
endif()

list(APPEND CMAKE_MODULE_PATH
  ${_AX_ROOT}/cmake/Modules/
)

include(AXBuildSet)

if(NOT _is_axmol_embed)
  if(DEFINED AX_PREBUILT_DIR AND NOT AX_PREBUILT_DIR STREQUAL "")
    get_filename_component(_ax_sdk_directory "${AX_PREBUILT_DIR}" ABSOLUTE BASE_DIR "${_AX_ROOT}")
    if(_AX_JS_PROJ AND NOT IS_DIRECTORY "${_ax_sdk_directory}")
      message(FATAL_ERROR "AX_PREBUILT_DIR does not identify an existing SDK: ${_ax_sdk_directory}")
    endif()
    # Keep the established C++/Lua SDK platforms. The JS linker additionally
    # supports Apple SDKs; all languages share engine and SDK path selection.
    if((WIN32 OR LINUX OR _AX_JS_PROJ) AND IS_DIRECTORY "${_ax_sdk_directory}")
      set(_AX_USE_PREBUILT TRUE)
      if(_AX_JS_PROJ)
        include(AXLinkHelpers)
      endif()
    endif()
  endif()
endif()

if(NOT _AX_USE_PREBUILT)
  add_subdirectory("${_AX_ROOT}/axmol" "${ENGINE_BINARY_PATH}/axmol")
endif()
