# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
include(${_AX_ROOT}/1k/platforms.cmake)
_1k_deprecated_32bit(axmol 3.0.0)

if(NOT CMAKE_GENERATOR MATCHES "Ninja")
  set(BUILD_CONFIG_DIR "\$\(Configuration\)/")
else()
  set(BUILD_CONFIG_DIR "")
endif()

macro(ax_link_pred EXTENSION_ENABLED)
  cmake_parse_arguments(opt "" "" "LIBS;INCLUDES" ${ARGN})
  if(${EXTENSION_ENABLED})
    list(APPEND LIBS ${opt_LIBS})

    foreach(INCLUDEDIR IN LISTS opt_INCLUDES)
      target_include_directories(${APP_NAME}
        PRIVATE ${INCLUDEDIR}
      )
    endforeach()
  endif()
endmacro()

function(ax_link_cxx_prebuilt APP_NAME AX_ROOT_DIR AX_PREBUILT_DIR)
  message(STATUS "Using Prebuilt feature from path:${_AX_ROOT}/${AX_PREBUILT_DIR}")
  message(STATUS "  AX_PREBUILT_DIR=${AX_PREBUILT_DIR}")
  # stupid: exclude CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG to avoid cmake generate
  # .vcxproj with incorrect debug msvc runtime, should be /MDd but got /MD
  set(AXSLCC_OUT_DIR_PROJ "${AXSLCC_OUT_DIR}")
  load_cache(
    "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}"
    INCLUDE_INTERNALS _NUGET_PACKAGE_DIR
    EXCLUDE thirdparty_LIB_DEPENDS
      CMAKE_CXX_FLAGS_DEBUG
      CMAKE_C_FLAGS_DEBUG
      CMAKE_BUILD_TYPE
  )
  set(AXSLCC_OUT_DIR_ENGINE ${AXSLCC_OUT_DIR})
  set(AXSLCC_OUT_DIR "${AXSLCC_OUT_DIR_PROJ}" CACHE STRING "" FORCE)
  unset(AXSLCC_OUT_DIR_PROJ)

  message(STATUS "AX_RENDER_API=${AX_RENDER_API}")
  message(STATUS "AX_ENABLE_D3D11=${AX_ENABLE_D3D11}")
  message(STATUS "AX_ENABLE_D3D12=${AX_ENABLE_D3D12}")
  message(STATUS "AX_ENABLE_VK=${AX_ENABLE_VK}")
  message(STATUS "AX_ENABLE_GL=${AX_ENABLE_GL}")
  message(STATUS "AX_ENABLE_MTL=${AX_ENABLE_MTL}")

  message(STATUS "AX_ENABLE_OPUS=${AX_ENABLE_OPUS}")
  message(STATUS "AX_ENABLE_MSEDGE_WEBVIEW2=${AX_ENABLE_MSEDGE_WEBVIEW2}")
  message(STATUS "AX_ENABLE_MFMEDIA=${AX_ENABLE_MFMEDIA}")

  message(STATUS "AX_ENABLE_EXT_IMGUI=${AX_ENABLE_EXT_IMGUI}")
  message(STATUS "AX_ENABLE_EXT_INSPECTOR=${AX_ENABLE_EXT_INSPECTOR}")
  message(STATUS "AX_ENABLE_EXT_FAIRYGUI=${AX_ENABLE_EXT_FAIRYGUI}")
  message(STATUS "AX_ENABLE_EXT_LIVE2D=${AX_ENABLE_EXT_LIVE2D}")
  message(STATUS "AX_ENABLE_EXT_GUI=${AX_ENABLE_EXT_GUI}")
  message(STATUS "AX_ENABLE_EXT_ASSETMANAGER=${AX_ENABLE_EXT_ASSETMANAGER}")
  message(STATUS "AX_ENABLE_EXT_PARTICLE3D=${AX_ENABLE_EXT_PARTICLE3D}")
  message(STATUS "AX_ENABLE_EXT_PHYSICS_NODE=${AX_ENABLE_EXT_PHYSICS_NODE}")
  message(STATUS "AX_ENABLE_EXT_SPINE=${AX_ENABLE_EXT_SPINE}")
  message(STATUS "AX_ENABLE_EXT_EFFEKSEER=${AX_ENABLE_EXT_EFFEKSEER}")
  message(STATUS "AX_ENABLE_EXT_LUA=${AX_ENABLE_EXT_LUA}")
  message(STATUS "AX_ENABLE_EXT_DRAGONBONES=${AX_ENABLE_EXT_DRAGONBONES}")
  message(STATUS "_NUGET_PACKAGE_DIR=${_NUGET_PACKAGE_DIR}")

  # compile defines can't inherit when link prebuits, so need add manually
  target_compile_definitions(${APP_NAME}
    PRIVATE YASIO_SSL_BACKEND=1
    PRIVATE OPENSSL_SUPPRESS_DEPRECATED=1
    PRIVATE NOUNCRYPT=1
    PRIVATE P2T_STATIC_EXPORTS=1
    PRIVATE BT_USE_SSE_IN_API=1
  )

  if(AX_GLES_PROFILE)
    target_compile_definitions(${APP_NAME} PRIVATE AX_GLES_PROFILE=${AX_GLES_PROFILE})
  else()
    target_compile_definitions(${APP_NAME} PRIVATE AX_GLES_PROFILE=0)
  endif()

  ax_config_pred(${APP_NAME} AX_ENABLE_MSEDGE_WEBVIEW2)
  ax_config_pred(${APP_NAME} AX_ENABLE_PHYSICS_2D)
  ax_config_pred(${APP_NAME} AX_ENABLE_3D)
  ax_config_pred(${APP_NAME} AX_ENABLE_PHYSICS_3D)
  ax_config_pred(${APP_NAME} AX_ENABLE_NAVMESH)
  ax_config_pred(${APP_NAME} AX_ENABLE_VIDEO)
  ax_config_pred(${APP_NAME} AX_ENABLE_AUDIO)
  ax_config_pred(${APP_NAME} AX_ENABLE_VR)
  ax_config_pred(${APP_NAME} AX_ENABLE_OPENXR)
  ax_config_pred(${APP_NAME} AX_ENABLE_EXT_SVG)
  # compile defines can't inherit when link prebuilts, so need to apply the
  # profiler backend define manually — mirrors the logic in axmol/CMakeLists.txt
  ax_apply_profiler_backend(${APP_NAME} PRIVATE)
  if(AX_ISA_SIMD MATCHES "sse")
    target_compile_definitions(${APP_NAME} PRIVATE AX_SSE_INTRINSICS=1)
  endif()

  if(BUILD_SHARED_LIBS)
    target_compile_definitions(${APP_NAME} PRIVATE AX_DLLIMPORT=1)
  endif()

  if(AX_ENABLE_PHYSICS_3D)
    target_compile_definitions(${APP_NAME} PRIVATE JPH_DEBUG_RENDERER=1)
  endif()

  target_include_directories(${APP_NAME}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/lua
    PRIVATE ${AX_ROOT_DIR}/extensions/scripting
    PRIVATE ${AX_ROOT_DIR}/extensions/scripting/lua-bindings
    PRIVATE ${AX_ROOT_DIR}
    PRIVATE ${AX_ROOT_DIR}/3rdparty
    PRIVATE ${AX_ROOT_DIR}/extensions
    PRIVATE ${AX_ROOT_DIR}/axmol
    PRIVATE ${AX_ROOT_DIR}/axmol/platform
    PRIVATE ${AX_ROOT_DIR}/axmol/base
    PRIVATE ${AX_ROOT_DIR}/axmol/audio
    PRIVATE ${AX_ROOT_DIR}/axmol/platform/win32
    PRIVATE ${AX_ROOT_DIR}/3rdparty/fmt/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/robin-map/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/freetype/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/glfw/include/GLFW
    PRIVATE ${AX_ROOT_DIR}/3rdparty/box2d/include
    PRIVATE ${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/engine/3rdparty/freetype/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/webp/src/webp
    PRIVATE ${AX_ROOT_DIR}/3rdparty/pugixml
    PRIVATE ${AX_ROOT_DIR}/3rdparty/xxhash
    PRIVATE ${AX_ROOT_DIR}/3rdparty/ConvertUTF
    PRIVATE ${AX_ROOT_DIR}/3rdparty/openal/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/ogg/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/glad/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/glfw/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/png
    PRIVATE ${AX_ROOT_DIR}/3rdparty/unzip/.
    PRIVATE ${AX_ROOT_DIR}/3rdparty/llhttp/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/lua/plainlua
    PRIVATE ${AX_ROOT_DIR}/3rdparty/lua/lua-cjson/.
    PRIVATE ${AX_ROOT_DIR}/3rdparty/zlib/_x/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/jpeg-turbo/_x/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/openssl/_x/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/curl/_x/include
    PRIVATE ${AX_ROOT_DIR}/3rdparty/yasio
  )

  SET(CONFIGURATION_SUBFOLDER "")
  target_link_directories(${APP_NAME}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/openssl/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/zlib/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/jpeg-turbo/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/curl/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}
    PRIVATE ${AX_ROOT_DIR}/3rdparty/opus/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}
    PRIVATE ${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/lib/${CMAKE_BUILD_TYPE}
  )

  # Linking platform libs
  if(WINDOWS)
    target_link_libraries(${APP_NAME} winmm Version)
  else()
    target_link_libraries(${APP_NAME} X11 fontconfig)
  endif()

  # Linking engine and 3rdparty libs
  set(LIBS
    axmol
    box2d
    freetype
    webp
    pugixml
    xxhash
    fastlz
    clipper2
    ConvertUTF
    poly2tri
    astcenc
    ogg
    glfw
    png
    unzip
    llhttp
    simdjson
    yasio
    websocket-parser
  )

  if(AX_ENABLE_GL OR AX_ENABLE_VK)
    list(APPEND LIBS glad)
  endif()

  if(AX_ENABLE_VK)
    list(APPEND LIBS vkma)
  endif()

  if(AX_ENABLE_OPUS)
    list(APPEND LIBS opus)
  endif()

  ax_link_pred(AX_WITH_TIFF LIBS tiff)

  if(AX_ENABLE_EXT_LIVE2D)
    include(${AX_ROOT_DIR}/extensions/Live2D/live2d-core.cmake)
  endif()

  ax_link_pred(AX_ENABLE_EXT_DRAGONBONES LIBS DragonBones INCLUDES "${AX_ROOT_DIR}/extensions/DragonBones/src")
  ax_link_pred(AX_ENABLE_EXT_SCENEEXT LIBS sceneext INCLUDES "${AX_ROOT_DIR}/extensions/sceneext/src")
  ax_link_pred(AX_ENABLE_EXT_SCENEIO LIBS sceneio INCLUDES "${AX_ROOT_DIR}/extensions/sceneio/src")
  ax_link_pred(AX_ENABLE_EXT_ASSETMANAGER LIBS assets-manager INCLUDES "${AX_ROOT_DIR}/extensions/assets-manager/src")
  ax_link_pred(AX_ENABLE_EXT_PARTICLE3D LIBS particle3d INCLUDES "${AX_ROOT_DIR}/extensions/Particle3D/src")
  ax_link_pred(AX_ENABLE_EXT_INSPECTOR LIBS Inspector INCLUDES "${AX_ROOT_DIR}/extensions/Inspector/src")
  ax_link_pred(AX_ENABLE_EXT_SDFGEN LIBS SDFGen INCLUDES "${AX_ROOT_DIR}/extensions/SDFGen/src")
  ax_link_pred(AX_ENABLE_EXT_GUI LIBS GUI INCLUDES "${AX_ROOT_DIR}/extensions/GUI/src" "${AX_ROOT_DIR}/extensions/GUI/src/GUI")
  ax_link_pred(AX_ENABLE_EXT_SVG LIBS svg lunasvg plutovg INCLUDES "${AX_ROOT_DIR}/extensions/svg/src")
  ax_link_pred(AX_ENABLE_EXT_FAIRYGUI LIBS fairygui INCLUDES "${AX_ROOT_DIR}/extensions/fairygui/src" "${AX_ROOT_DIR}/extensions/fairygui/src/fairygui")
  ax_link_pred(AX_ENABLE_EXT_LIVE2D LIBS Live2D Live2DCubismCore INCLUDES "${AX_ROOT_DIR}/extensions/Live2D/Framework/src" "${AX_ROOT_DIR}/extensions/Live2D/Core/include")
  ax_link_pred(AX_ENABLE_EXT_EFFEKSEER LIBS EffekseerAxmol Effekseer EffekseerRendererCommon INCLUDES
    "${AX_ROOT_DIR}/extensions/Effekseer"
    "${AX_ROOT_DIR}/extensions/Effekseer/EffekseerAxmol"
  )
  ax_link_pred(AX_ENABLE_EXT_PHYSICS_NODE LIBS physics-nodes INCLUDES "${AX_ROOT_DIR}/extensions/physics-nodes/src")
  ax_link_pred(AX_ENABLE_NAVMESH LIBS recast INCLUDES "${AX_ROOT_DIR}/3rdparty/recast")
  ax_link_pred(AX_ENABLE_PHYSICS_3D LIBS Jolt INCLUDES "${AX_ROOT_DIR}/3rdparty/jolt")
  ax_link_pred(AX_ENABLE_OPENXR LIBS openxr_loader INCLUDES "${OPENXR_SOURCE_DIR}/include")

  ax_link_pred(AX_ENABLE_EXT_IMGUI LIBS ImGui INCLUDES
    "${AX_ROOT_DIR}/extensions/ImGui/src" "${AX_ROOT_DIR}/extensions/ImGui/src/ImGui/imgui"
  )

  ax_link_pred(AX_ENABLE_EXT_SPINE LIBS spine INCLUDES
    "${AX_ROOT_DIR}/extensions/spine/runtime/include" "${AX_ROOT_DIR}/extensions/spine/src"
  )

  if(AX_PROFILER_BACKEND STREQUAL "TRACY")
    list(APPEND LIBS "TracyClient")
    target_include_directories(${APP_NAME} PRIVATE "${Tracy_SOURCE_DIR}/public")
  endif()

  if(WINDOWS)
    target_link_libraries(${APP_NAME}
      ${LIBS}
      zlib
      jpeg-static
      libcrypto
      libssl
      libcurl_imp
    )

    if(AX_ENABLE_AUDIO)
      target_link_libraries(${APP_NAME}
        OpenAL32
      )
    endif()
  else()
    target_link_libraries(${APP_NAME}
      ${LIBS}
      z
      jpeg
      curl
      ssl
      crypto
    )

    if(AX_ENABLE_AUDIO)
      target_link_libraries(${APP_NAME}
        openal
      )
    endif()
  endif()

  target_link_libraries(${APP_NAME} debug fmtd optimized fmt)

  # Copy dlls to app bin dir
  if(WINDOWS)
    set(ssl_dll_suffix "")

    if(WIN64)
      set(ssl_dll_suffix "-${ARCH_ALIAS}")
    endif()

    set(_prebuilt_dlls
      "${AX_ROOT_DIR}/3rdparty/openssl/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/libcrypto-3${ssl_dll_suffix}.dll"
      "${AX_ROOT_DIR}/3rdparty/openssl/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/libssl-3${ssl_dll_suffix}.dll"
      "${AX_ROOT_DIR}/3rdparty/curl/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/libcurl.dll"
      "${AX_ROOT_DIR}/3rdparty/zlib/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/zlib1.dll"
    )

    if(AX_ENABLE_AUDIO)
      list(APPEND _prebuilt_dlls "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}OpenAL32.dll")
    endif()

    if(BUILD_SHARED_LIBS)
      list(APPEND _prebuilt_dlls
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}glad.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}glfw.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}pugixml.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}freetype.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}axmol.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}sceneext.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}sceneio.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}GUI.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}particle3d.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}physics-nodes.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}spine.dll"
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}assets-manager.dll")
    endif()

    # Copy windows angle binaries
    if(AX_GLES_PROFILE)
      list(APPEND _prebuilt_dlls
        "${AX_ROOT_DIR}/3rdparty/angle/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/libGLESv2.dll"
        "${AX_ROOT_DIR}/3rdparty/angle/_x/lib/${PLATFORM_NAME}/${ARCH_ALIAS}/libEGL.dll")
    endif()

    if(AX_GLES_PROFILE OR AX_ENABLE_D3D12 OR AX_ENABLE_D3D11)
      find_windows_sdk_bin(_winsdk_bin_dir ${ARCH_ALIAS})
      list(APPEND _prebuilt_dlls "${_winsdk_bin_dir}/d3dcompiler_47.dll")
      if(AX_ENABLE_D3D12)
        list(APPEND all_depend_dlls "${_winsdk_bin_dir}/dxcompiler.dll")
      endif()
    endif()

    if(AX_ENABLE_MSEDGE_WEBVIEW2)
      if(CMAKE_GENERATOR MATCHES "Ninja")
        target_link_libraries(${APP_NAME} "${_NUGET_PACKAGE_DIR}/Microsoft.Web.WebView2/build/native/${ARCH_ALIAS}/WebView2Loader.dll.lib")
        target_include_directories(${APP_NAME} PRIVATE "${_NUGET_PACKAGE_DIR}/Microsoft.Web.WebView2/build/native/include")
        list(APPEND _prebuilt_dlls "${_NUGET_PACKAGE_DIR}/Microsoft.Web.WebView2/build/native/${ARCH_ALIAS}/WebView2Loader.dll")
      else()
        target_link_libraries(${APP_NAME} "${_NUGET_PACKAGE_DIR}/Microsoft.Web.WebView2/build/native/Microsoft.Web.WebView2.targets")
      endif()
    endif()

    add_custom_command(TARGET ${APP_NAME} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_prebuilt_dlls}
          $<TARGET_FILE_DIR:${APP_NAME}>
        )
  endif()

  # prebuilt, need copy axslc folder to target output directory
  get_target_property(rt_output ${APP_NAME} RUNTIME_OUTPUT_DIRECTORY)
  ax_sync_target_res(${APP_NAME} LINK_TO "${rt_output}/${CMAKE_CFG_INTDIR}/axslc" FOLDERS ${AXSLCC_OUT_DIR_ENGINE} SYNC_TARGET_ID axslc-builtin)
endfunction(ax_link_cxx_prebuilt)

function(ax_link_lua_prebuilt APP_NAME AX_ROOT_DIR AX_PREBUILT_DIR)
  if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${APP_NAME}
      PRIVATE _USRLUASTATIC=1
    )
  endif()

  target_link_libraries(${APP_NAME} axlua lua-cjson plainlua)

  ax_link_cxx_prebuilt(${APP_NAME} ${AX_ROOT_DIR} ${AX_PREBUILT_DIR})

  if(WINDOWS)
    if(MSVC)
      add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/${BUILD_CONFIG_DIR}plainlua.dll"
        $<TARGET_FILE_DIR:${APP_NAME}>)
    else()
      add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}/bin/$<CONFIG>/plainlua.dll"
        $<TARGET_FILE_DIR:${APP_NAME}>)
    endif()
  endif()
endfunction(ax_link_lua_prebuilt)

# JavaScript SDKs use the exported CMake map so the exact runtime package and
# transitive extension interfaces survive SDK linking on Apple and Android.
function(ax_link_js_prebuilt APP_NAME AX_ROOT_DIR AX_PREBUILT_DIR)
  if(IS_ABSOLUTE "${AX_PREBUILT_DIR}")
    set(_js_sdk_dir "${AX_PREBUILT_DIR}")
  else()
    set(_js_sdk_dir "${AX_ROOT_DIR}/${AX_PREBUILT_DIR}")
  endif()
  if(NOT EXISTS "${_js_sdk_dir}/AxmolTargets.cmake")
    message(FATAL_ERROR "JavaScript SDK target map not found: ${_js_sdk_dir}/AxmolTargets.cmake. Build axmol-sdk with AX_ENABLE_EXT_JS=ON first.")
  endif()

  find_package(Threads REQUIRED)
  set(_1kfetch_cache_dir "${AX_ROOT_DIR}/cache")
  include("${AX_ROOT_DIR}/1k/fetch.cmake")
  load_cache("${_js_sdk_dir}" READ_WITH_PREFIX _js_sdk_
    AX_WITH_JPEG AX_WITH_TIFF AX_WITH_CURL AX_ENABLE_OPUS AX_WITH_CARES
    BUILD_SHARED_LIBS ANDROID_SHARED_LOADS)
  set(BUILD_SHARED_LIBS "${_js_sdk_BUILD_SHARED_LIBS}" PARENT_SCOPE)
  if(ANDROID)
    set(ANDROID_SHARED_LOADS "${_js_sdk_ANDROID_SHARED_LOADS}" PARENT_SCOPE)
    if(NOT COMMAND config_android_shared_libs)
      function(config_android_shared_libs package_name target_folder)
        string(REPLACE "." "/" package_path "${package_name}")
        configure_file("${_AX_ROOT}/3rdparty/SharedLoader.java.in"
          "${target_folder}/${package_path}/SharedLoader.java" @ONLY)
      endfunction()
    endif()
  endif()
  set(_js_sdk_imports zlib openssl)
  if(_js_sdk_AX_WITH_JPEG)
    list(APPEND _js_sdk_imports jpeg-turbo)
  endif()
  if(_js_sdk_AX_WITH_CURL)
    list(APPEND _js_sdk_imports curl)
  endif()
  if(_js_sdk_AX_ENABLE_OPUS)
    list(APPEND _js_sdk_imports opus)
  endif()
  if(_js_sdk_AX_WITH_CARES)
    list(APPEND _js_sdk_imports c-ares)
  endif()
  foreach(_js_import IN LISTS _js_sdk_imports)
    if(_js_import STREQUAL "openssl")
      set(_js_import_target OpenSSL::SSL)
    elseif(_js_import STREQUAL "c-ares")
      set(_js_import_target cares)
    else()
      set(_js_import_target "${_js_import}")
    endif()
    if(NOT TARGET ${_js_import_target})
      add_subdirectory("${AX_ROOT_DIR}/3rdparty/${_js_import}"
        "${CMAKE_CURRENT_BINARY_DIR}/js-sdk-import/${_js_import}")
    endif()
  endforeach()

  # The runtime is an imported target, so it is intentionally not included in
  # AxmolTargets.cmake. Import it with the same manifest validation as a source
  # build, before loading the SDK map which references it.
  if(NOT TARGET axmol::spidermonkey)
    add_subdirectory("${AX_ROOT_DIR}/3rdparty/spidermonkey"
      "${CMAKE_CURRENT_BINARY_DIR}/js-runtime-import")
  endif()
  include("${_js_sdk_dir}/AxmolTargets.cmake")
  if(NOT TARGET axjs)
    message(FATAL_ERROR "The selected Axmol SDK was built without JavaScript support")
  endif()
  target_link_libraries(${APP_NAME} axjs axmol)

  load_cache("${_js_sdk_dir}" READ_WITH_PREFIX _js_sdk_ AXSLCC_OUT_DIR)
  if(DEFINED _js_sdk_AXSLCC_OUT_DIR AND IS_DIRECTORY "${_js_sdk_AXSLCC_OUT_DIR}")
    if(APPLE)
      ax_mark_multi_resources(_js_sdk_shaders RES_TO "Resources/axslc"
        FOLDERS "${_js_sdk_AXSLCC_OUT_DIR}")
      target_sources(${APP_NAME} PRIVATE ${_js_sdk_shaders})
    else()
      add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_js_sdk_AXSLCC_OUT_DIR}"
          "$<TARGET_FILE_DIR:${APP_NAME}>/axslc"
        VERBATIM)
    endif()
  endif()
endfunction()

function(ax_embed_js_runtime_frameworks APP_NAME)
  if(NOT APPLE OR NOT TARGET OpenAL)
    return()
  endif()
  get_target_property(_js_openal_framework OpenAL FRAMEWORK)
  if(NOT _js_openal_framework)
    return()
  endif()
  if(XCODE)
    set_target_properties(${APP_NAME} PROPERTIES
      XCODE_LINK_BUILD_PHASE_MODE KNOWN_LOCATION
      XCODE_EMBED_FRAMEWORKS OpenAL
      XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
      XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON)
  else()
    if(MACOSX)
      set(_js_framework_source "$<TARGET_FILE_DIR:OpenAL>/../..")
      set(_js_framework_dest "$<TARGET_BUNDLE_DIR:${APP_NAME}>/Contents/Frameworks/soft_oal.framework")
      set_property(TARGET ${APP_NAME} APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
      add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_js_framework_dest}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_js_framework_dest}/Versions/C/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_js_framework_source}/Versions/C/soft_oal"
          "${_js_framework_dest}/Versions/C/soft_oal"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${_js_framework_source}/Versions/C/Resources"
          "${_js_framework_dest}/Versions/C/Resources"
        COMMAND ${CMAKE_COMMAND} -E create_symlink C
          "${_js_framework_dest}/Versions/Current"
        COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/soft_oal
          "${_js_framework_dest}/soft_oal"
        COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Resources
          "${_js_framework_dest}/Resources"
        VERBATIM)
    else()
      set(_js_framework_source "$<TARGET_FILE_DIR:OpenAL>")
      set(_js_framework_dest "$<TARGET_BUNDLE_DIR:${APP_NAME}>/Frameworks")
      set_property(TARGET ${APP_NAME} APPEND PROPERTY BUILD_RPATH "@executable_path/Frameworks")
      add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_js_framework_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_js_framework_source}"
          "${_js_framework_dest}/soft_oal.framework"
        VERBATIM)
    endif()
  endif()
endfunction()

function(ax_sign_macos_bundle APP_NAME)
  if(NOT MACOSX OR XCODE)
    return()
  endif()
  if(NOT DEFINED AX_MACOS_BUNDLE_CODESIGN_IDENTITY)
    set(AX_MACOS_BUNDLE_CODESIGN_IDENTITY "-" CACHE STRING
      "Code-sign identity for non-Xcode macOS application bundles; empty disables signing")
    mark_as_advanced(AX_MACOS_BUNDLE_CODESIGN_IDENTITY)
  endif()
  if(NOT AX_MACOS_BUNDLE_CODESIGN_IDENTITY STREQUAL "")
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
      COMMAND /usr/bin/codesign --force --deep --sign
        "${AX_MACOS_BUNDLE_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:${APP_NAME}>"
      COMMENT "Code signing macOS application bundle"
      VERBATIM)
  endif()
endfunction()
