# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
if(NOT DEFINED platform_source_folder)
  set(platform_source_folder "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(ANDROID)
  list(APPEND GAME_SOURCE "${platform_source_folder}/proj.android/app/jni/main.cpp")
elseif(MACOSX)
  set(APP_UI_RES
    "${platform_source_folder}/proj.ios_mac/mac/Icon.icns")
  list(APPEND GAME_SOURCE
    "${platform_source_folder}/proj.ios_mac/mac/main.cpp"
    "${platform_source_folder}/proj.ios_mac/mac/Prefix.pch" ${APP_UI_RES})
elseif(IOS)
  set(_ios_folder "${platform_source_folder}/proj.ios_mac/ios")
  list(APPEND GAME_HEADER
    "${_ios_folder}/GameAppController.h" "${_ios_folder}/GameViewController.h"
    "${_ios_folder}/GameSceneDelegate.h")
  set(APP_UI_RES
    "${_ios_folder}/targets/ios/LaunchScreenBackground.png"
    "${_ios_folder}/targets/ios/LaunchScreen.storyboard"
    "${_ios_folder}/targets/ios/Images.xcassets")
  list(APPEND GAME_SOURCE "${_ios_folder}/main.mm" "${_ios_folder}/GameAppController.mm"
    "${_ios_folder}/GameViewController.mm" "${_ios_folder}/GameSceneDelegate.mm"
    "${_ios_folder}/Prefix.pch" ${APP_UI_RES})
else()
  message(FATAL_ERROR "SpiderMonkey 33.1 JavaScript applications support macOS, iOS and Android")
endif()
list(APPEND GAME_SOURCE ${common_content_files})
