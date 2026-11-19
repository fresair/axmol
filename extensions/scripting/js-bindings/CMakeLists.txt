# Copyright (c) 2019-present Simdsoft Limited.
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT

set(_AX_JS_LIB axjs CACHE INTERNAL "The axmol JavaScript bindings library name")
option(AX_ENABLE_JS_DEBUGGER "Build the JavaScript development debugger" ON)
if(AX_ENABLE_GC_FOR_NATIVE_OBJECTS)
  message(FATAL_ERROR "SpiderMonkey 33.1 bindings require AX_ENABLE_GC_FOR_NATIVE_OBJECTS=0")
endif()

# Generated modules are split into stable, independently compiled shards. Keep
# the runtime, adapters and generated output in one explicit library while
# letting the generator add shards without changing this build description.
file(GLOB_RECURSE js_binding_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/manual/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.cpp")
file(GLOB_RECURSE js_binding_headers CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/manual/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.h")

if(APPLE)
  file(GLOB_RECURSE js_apple_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/manual/*.mm")
  list(APPEND js_binding_sources ${js_apple_sources})
elseif(ANDROID)
  file(GLOB_RECURSE js_android_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.cpp")
  list(APPEND js_binding_sources ${js_android_sources})
endif()

add_library(${_AX_JS_LIB} STATIC ${js_binding_sources} ${js_binding_headers})
add_library(axmol::js ALIAS ${_AX_JS_LIB})

get_target_property(js_dependencies 3rdparty AX_JS_DEPEND)
if(NOT js_dependencies)
  message(FATAL_ERROR "AX_ENABLE_EXT_JS requires a validated SpiderMonkey package")
endif()
target_link_libraries(${_AX_JS_LIB} PUBLIC ${js_dependencies})

# JS is linked only by JS applications. This preserves the C++/Lua application
# dependency graph when both scripting extensions are compiled into an SDK.
ax_setup_ext_config(${_AX_JS_LIB} DNTLINK LINK_SCOPE PUBLIC)
target_include_directories(${_AX_JS_LIB}
  PUBLIC "${_AX_ROOT}/extensions/scripting"
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${_AX_ROOT}/extensions"
  PRIVATE "${_AX_ROOT}/3rdparty")
target_compile_definitions(${_AX_JS_LIB}
  PUBLIC AXMOL_JS_BINDINGS=1
  PUBLIC AX_ENABLE_EXT_JS=1 AX_ENABLE_GC_FOR_NATIVE_OBJECTS=0
  PUBLIC AX_ENABLE_JS_DEBUGGER=$<IF:$<AND:$<BOOL:${AX_ENABLE_JS_DEBUGGER}>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>,1,0>)

foreach(js_extension IN ITEMS GUI assets-manager spine sceneext sceneio particle3d physics-nodes cocosbuilder)
  if(TARGET ${js_extension})
    target_link_libraries(${_AX_JS_LIB} PUBLIC ${js_extension})
  endif()
endforeach()
foreach(js_feature IN ITEMS AX_ENABLE_AUDIO AX_ENABLE_HTTP AX_ENABLE_WEBSOCKET
    AX_ENABLE_3D AX_ENABLE_PHYSICS_2D AX_ENABLE_PHYSICS_3D AX_ENABLE_NAVMESH AX_ENABLE_VIDEO
    AX_ENABLE_EXT_GUI AX_ENABLE_EXT_ASSETMANAGER AX_ENABLE_EXT_SPINE
    AX_ENABLE_EXT_SCENEEXT AX_ENABLE_EXT_SCENEIO AX_ENABLE_EXT_COCOSBUILDER
    AX_ENABLE_EXT_PARTICLE3D AX_ENABLE_EXT_PHYSICS_NODE)
  ax_config_pred(${_AX_JS_LIB} ${js_feature})
endforeach()

if(APPLE)
  set_xcode_property(${_AX_JS_LIB} CLANG_ENABLE_OBJC_ARC "YES")
  target_compile_options(${_AX_JS_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()
if(MSVC)
  target_compile_options(${_AX_JS_LIB} PRIVATE /bigobj)
endif()

option(AX_BUILD_JS_RUNTIME_TESTS "Build focused JavaScript runtime regression tests" ${AX_BUILD_TESTS})
if(AX_BUILD_JS_RUNTIME_TESTS)
  enable_testing()
  add_subdirectory("${_AX_ROOT}/tests/js-runtime-tests" "${CMAKE_CURRENT_BINARY_DIR}/runtime-tests")
endif()
