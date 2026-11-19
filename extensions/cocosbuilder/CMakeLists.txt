# Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
file(GLOB cocosbuilder_sources CONFIGURE_DEPENDS "*.cpp" "*.h")
add_library(cocosbuilder ${cocosbuilder_sources})
target_include_directories(cocosbuilder PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(cocosbuilder PUBLIC GUI)
ax_setup_ext_config(cocosbuilder LINK_SCOPE PUBLIC)
