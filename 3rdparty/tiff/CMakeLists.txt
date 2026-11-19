# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
# LibTIFF is optional; this directory is included only with AX_WITH_TIFF=ON.
# Reuse Axmol's imported codecs instead of finding a second system copy.
if(NOT TARGET ZLIB::ZLIB)
  message(FATAL_ERROR "LibTIFF requires Axmol's ZLIB::ZLIB target")
endif()

get_target_property(ZLIB_INCLUDE_DIR ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
set(ZLIB_LIBRARY zlib)

if(AX_WITH_JPEG)
  if(NOT TARGET jpeg-turbo)
    message(FATAL_ERROR "TIFF JPEG support requires Axmol's jpeg-turbo target")
  endif()
  if(NOT TARGET JPEG::JPEG)
    add_library(JPEG::JPEG ALIAS jpeg-turbo)
  endif()
  get_target_property(JPEG_INCLUDE_DIRS jpeg-turbo INTERFACE_INCLUDE_DIRECTORIES)
  set(JPEG_LIBRARIES JPEG::JPEG)
  set(JPEG_FOUND TRUE)
else()
  set(CMAKE_DISABLE_FIND_PACKAGE_JPEG TRUE)
  set(JPEG_FOUND FALSE)
endif()

# Upstream resets policies to CMake 3.10, so its option() calls do not always
# preserve normal variables. These codec choices are deliberate and pinned.
foreach(_ax_tiff_option tiff-static zlib jpeg-prefer-standard)
  set(${_ax_tiff_option} ON CACHE BOOL "Axmol LibTIFF configuration" FORCE)
endforeach()
set(jpeg ${AX_WITH_JPEG} CACHE BOOL "Use Axmol's JPEG codec in LibTIFF" FORCE)
set(old-jpeg ${AX_WITH_JPEG} CACHE BOOL "Decode legacy JPEG TIFFs" FORCE)
foreach(_ax_tiff_option tiff-tools tiff-tests tiff-contrib tiff-docs
    tiff-cxx tiff-install tiff-framework tiff-opengl
    libdeflate jbig lerc lzma zstd webp jpeg12
    cxx-compat-mode cxx-compat-warnings fatal-warnings)
  set(${_ax_tiff_option} OFF CACHE BOOL "Axmol LibTIFF configuration" FORCE)
endforeach()
foreach(_ax_tiff_package Deflate JBIG LERC liblzma ZSTD WebP OpenGL GLUT)
  set(CMAKE_DISABLE_FIND_PACKAGE_${_ax_tiff_package} TRUE)
endforeach()

add_subdirectory(upstream)
add_library(TIFF::TIFF ALIAS tiff)
set_target_properties(tiff PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE YES)
