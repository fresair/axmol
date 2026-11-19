# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
if(NOT APPLE)
  ax_get_resource_path(APP_RES_DIR ${APP_NAME})
  ax_sync_target_res(${APP_NAME} LINK_TO ${APP_RES_DIR} FOLDERS ${content_folder} SYM_LINK 1)
endif()
include(AXLinkHelpers)
ax_embed_js_runtime_frameworks(${APP_NAME})
ax_setup_app_props(${APP_NAME})
ax_sign_macos_bundle(${APP_NAME})
