# The axmol CMake options

## The options for axmol (please sort them alphabetically. Thanks.)
- AX_BUILD_TESTS: whether to build test projects, including cpp-tests, lua-tests, fairygui-tests and js-tests when enabled, default: `TRUE`
- AX_BUILD_JS_RUNTIME_TESTS: build focused headless JS lifecycle and binding regression tests, defaults to `AX_BUILD_TESTS`; useful with the main test applications disabled.
- AX_JS_GRAPHICS_TESTS: register JS scene, legacy HelloWorld and CocoStudio CTest applications on macOS, default: `FALSE`; these tests require a graphical session.
- AX_ENABLE_XXX for core feature: 
  - AX_ENABLE_3D: whether to enable 3D support, default: `TRUE`
  - AX_ENABLE_AUDIO: whether to enable audio support, default: `TRUE`
  - AX_ENABLE_HTTP: whether enable http client, default: `TRUE`
  - AX_ENABLE_MFMEDIA: whether enable microsoft media foundation for windows video player support, default: `TRUE`
  - AX_ENABLE_MSEDGE_WEBVIEW2: whether enable msedge webview2, default: `TRUE`
  - AX_ENABLE_NAVMESH: whether to enable NavMesh support default: `TRUE`
  - AX_ENABLE_OPUS: whether to enable audio engine play .opus files support, default: `TRUE`
  - AX_ENABLE_PHYSICS_3D: whether to enable physics3d support, default: `TRUE`
  - AX_ENABLE_EXT_SVG: whether to enable the SVG extension, default: `FALSE`
  - AX_ENABLE_VIDEO: whether to enable video player, default: `TRUE`
  - AX_ENABLE_VLC_MEDIA: whether enable libvlc media, default: `TRUE on Linux`, `FALSE on Windows`, not support other platforms
  - AX_ENABLE_VR: whether to enable generic VR renderer support, status: `experimental`, default: `FALSE`
  - AX_ENABLE_WAYLAND: whether to enable linux wayland, default: `FALSE`
  - AX_ENABLE_WEBSOCKET: whether enable websockets client, default: `TRUE`
- AX_USE_XXX:
  - AX_USE_LUAJIT: whether use luajit, default: `FALSE`, use plainlua
- AX_ENABLE_EXT_XXX for extensions
  - AX_ENABLE_EXT_ASSETMANAGER: the assetmanager extension, default: `TRUE`
  - AX_ENABLE_EXT_COCOSBUILDER: the legacy native CCBI reader and animation backend, default: `FALSE`; the complete original v3 JS tests enable it.
  - AX_ENABLE_EXT_COCOSTUDIO: the cocosstudio extension for load .csb: `TRUE`
  - AX_ENABLE_EXT_DRAGONBONES: the dragonbones extension: `TRUE`
  - AX_ENABLE_EXT_EFFEKSEER: the effekseer extension, default: `FALSE` 
  - AX_ENABLE_EXT_FAIRYGUI: the fairygui extension, default: `TRUE`
  - AX_ENABLE_EXT_GUI: the traditional GUI extension, default: `TRUE`
  - AX_ENABLE_EXT_IMGUI: the imgui extension, support macos,win,linux,android, default: `TRUE` 
  - AX_ENABLE_EXT_INSPECTOR: the imgui inspector extension, support macos,win,linux,android, default: `TRUE` 
  - AX_ENABLE_EXT_JSONDEFAULT: the UserDefault based on json, default: `FALSE`
  - AX_ENABLE_EXT_JS: the SpiderMonkey 33.1 JavaScript extension, default: `FALSE`; JavaScript project templates enable it automatically. Lua and JS may be compiled together, with one active script engine per application.
  - AX_ENABLE_EXT_LIVE2D: the live2d extension, default: `FALSE` 
  - AX_ENABLE_EXT_LUA: the lua extension, default: `TRUE`
  - AX_ENABLE_EXT_PARTICLE3D: the particle3d extension, default: `TRUE`
  - AX_ENABLE_EXT_PHYSICS_NODE: the physics_node extension, default: `TRUE`
  - AX_ENABLE_EXT_SDFGEN: the SDF atlas generator, default: `TRUE`
  - AX_ENABLE_EXT_SPINE: the spine extension, default: `TRUE`
- AX_WITH_XXX: usually user don't need care it
- AX_WITH_TIFF: decode TIFF images, including embedded legacy particle textures; default: `FALSE`. JavaScript templates and the full v3 JS tests enable it. Prebuilt JavaScript SDKs for these resources must also be built with it enabled.
- AX_JS_SPIDERMONKEY_ROOT: optional path to a complete, source-built SpiderMonkey package. By default, JS builds reuse a validated package in `cache/js-runtime/packages` or prepare the missing ARM64 package through `3rdparty/spidermonkey`. An explicit path must already contain a matching package.
- AX_JS_SPIDERMONKEY_AUTO_BUILD: enable first-use preparation for a missing default ARM64 package (`ON` by default). `AX_JS_SPIDERMONKEY_CACHE_DIR` selects its cache and `AX_JS_SPIDERMONKEY_OFFLINE=ON` restricts preparation to locally available source archives and tools. See [SpiderMonkey build options](3rdparty/spidermonkey/README.md).
- AX_ENABLE_JS_DEBUGGER: build the JavaScript developer debugger, default: `TRUE`; enabled only in Debug and RelWithDebInfo configurations. The JS project template starts it only when `AXMOL_JS_DEBUG=1` is set.
- AX_RENDER_API: specify render api, valid values are: `auto`, `gl`, `mtl`, `d3d11`, `d3d12`, `vk`, default: `auto`.  
  You may also specify multiple APIs separated by semicolons, e.g. "-DAX_RENDER_API=vk;gl" This will enable both Vulkan and OpenGL backends in the build.
- AX_GLES_PROFILE: specify GLES profile version, only works when selected render api is `gl`, default: `300`, valid value `300`, `310`
- AX_CORE_PROFILE: whether strip all deprecated features, default `FALSE`, Setting this to `TRUE` before upgrading your game project helps ensure compatibility with future Axmol versions.
- AX_ISA_LEVEL: specifiy SIMD Instructions Acceleration Level: 0~4, 0: disabled, 1: SSE2, 2: SSE4.1/NEON, 3: SSE4.2, 4: AVX2, default: 2
- AX_PROFILER_BACKEND: specifiy profiler backend, valid values are: `TRACY`, `NONE`, default: `NONE`
- AX_WASM_THREADS: specify wasm thread count, valid value: number: `>=0` , string: must be: `auto` or `navigator.hardwareConcurrency`(default), 
   - number: explicit set thread count, `0` means disable wasm thread support
   - string: 
     - `navigator.hardwareConcurrency`: will be emitted in the JS code which will use the number of cores the browser reports
     - `auto`: Use cmake to detect host processor count
default is: `navigator.hardwareConcurrency`
- AX_WASM_ASSETS_PRELOAD_FILE: Whether enable to bundle the assets to a .data file and preload them into an IndexedDB cache at first launch (`--preload-file`), otherwise bundle them into the wasm file itself and don't use the IndexedDB at all (`--embed-file`)", default `TRUE`
- AX_WASM_ENABLE_DEVTOOLS: whether enable web devtools aka `pause`, `resume`, `step` buttons in webpage, default: `TRUE`
- AX_WASM_GENERATE_SYMBOL_FILE: Generate a symbol file for the built wasm file, default `FALSE`
- AX_WASM_INITIAL_MEMORY: set the wasm initial memory size, default `1024MB`
- AX_WASM_ISA_SIMD: specify the wasm simd intrinsics type, default `none`, supports `sse`, `neon`, note the `wasm-simd` not support by axmol yet
- AX_WASM_SHELL_FILE: specify the wasm shell file, by default use `${_AX_ROOT}/axmol/platform/wasm/shell_minimal.html`
- AX_WASM_TIMING_USE_TIMEOUT: force the wasm emscripten loop timing to use `Timeout` with FPS value of `Application::setAnimationInterval` (app still run on focus lost), default is `FALSE` with timing `requestAnimationFrame` (no run on focus lost). See https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_set_main_loop_timing


## The options for axmol apps

- AX_PREBUILT_DIR: specify the prebuilt dir (relative to `AX_ROOT`), it's very useful for fast linking apps with prebuilt engine libs
