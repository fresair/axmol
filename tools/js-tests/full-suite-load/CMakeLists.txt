# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22...4.4)
project(js_full_suite_load_audit LANGUAGES C CXX)

get_filename_component(_AX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_MODULE_PATH "${_AX_ROOT}/cmake/Modules")
include(AXBuildSet)
include(AXLinkHelpers)
set(AX_PREBUILT_DIR "" CACHE PATH "Existing Axmol SDK built with JavaScript support")
if(AX_PREBUILT_DIR STREQUAL "")
  message(FATAL_ERROR "Set AX_PREBUILT_DIR to a previously built JavaScript SDK")
endif()
if(NOT EXISTS "${_AX_ROOT}/tests/js-tests/Content/legacy/js-tests/project.json")
  message(FATAL_ERROR "The complete js-tests application project is required for this explicit application audit")
endif()

add_executable(js-full-suite-load-tests FullSuiteLoadTests.cpp)
target_compile_features(js-full-suite-load-tests PRIVATE cxx_std_23)
ax_link_js_prebuilt(js-full-suite-load-tests "${_AX_ROOT}" "${AX_PREBUILT_DIR}")
if(APPLE)
  target_link_libraries(js-full-suite-load-tests PRIVATE
    "-framework Cocoa" "-framework Metal" "-framework QuartzCore")
  set_target_properties(js-full-suite-load-tests PROPERTIES
    BUILD_WITH_INSTALL_RPATH FALSE SKIP_BUILD_RPATH FALSE)
  if(TARGET OpenAL)
    get_target_property(_js_openal_framework OpenAL FRAMEWORK)
    if(_js_openal_framework)
      set_property(TARGET js-full-suite-load-tests APPEND PROPERTY
        BUILD_RPATH "$<TARGET_BUNDLE_DIR:OpenAL>/..")
    endif()
  endif()
endif()
enable_testing()
add_test(NAME js-full-suite-load-tests COMMAND js-full-suite-load-tests "${_AX_ROOT}")
set_tests_properties(js-full-suite-load-tests PROPERTIES TIMEOUT 90)
