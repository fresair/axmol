# Native JavaScript project

This template runs Cocos2d-x v3 style `cc`, `ccui`, `ccs`, `sp` and `jsb` scripts
on Axmol using SpiderMonkey 33.1. Source scripts and resources live in `Content`.
`main.js` starts the game and `project.json` lists its source modules.

Run this engine checkout's `setup.ps1`, then restart the terminal. Create and
build with the same workflow as C++ and Lua. On an Apple Silicon Mac:

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

For Android, use `axmol build -p android -a arm64`. The standard Android SDK/NDK
setup supplies the toolchain; JS validates exactly NDK r27d (`27.3.13750724`),
API 23 or newer and `c++_shared`. No manual NDK link is required.

Creation copies the native entrypoint, compatibility scripts, sample resources,
platform project files and debugger configuration. It does not compile or download
SpiderMonkey. At native configure, `3rdparty/spidermonkey` imports a validated
package or prepares a missing default package from its pinned upstream source.
Cold preparation requires Python 3.9+, curl, patch, make and the platform compiler.
The helper builds an isolated Python 2.7.18 only when no usable interpreter is
available for the legacy build toolchain; it does not change global Python tools.
Warm package imports need neither Python 2 nor the source archives.

C++, Lua and JS use the same engine setup template. An explicit CMake `_AX_ROOT`
selects the engine for a shared project; otherwise the template uses `AX_ROOT`.
An isolated project uses its embedded `axmol` directory. Use this checkout's
`tools/cmdline/axmol` directly if several engine checkouts are installed; it
selects its own engine root for project creation. For example, from the engine
checkout in a POSIX shell:

```sh
AX_ENGINE="$PWD"
"$AX_ENGINE/tools/cmdline/axmol" build -d ./projects/HelloJs -p osx -a arm64 \
  -xc "-D_AX_ROOT=$AX_ENGINE"
```

The default build/download cache and packages live in the selected engine's
`cache/js-runtime`, including for an isolated project's embedded engine. Pass
CMake options through `-xc`, for example:

```sh
axmol build -p osx -a arm64 \
  -xc "-DAX_JS_SPIDERMONKEY_CACHE_DIR=/absolute/path/to/cache,-DAX_JS_SPIDERMONKEY_OFFLINE=ON"
```

Offline preparation requires the checksum-pinned runtime archive and either a
usable Python 2.7.18 or the cached Python source archive. An existing complete
package imports without those prerequisites. Set
`AX_JS_SPIDERMONKEY_AUTO_BUILD=OFF` to prohibit automatic preparation.
`AX_JS_SPIDERMONKEY_ROOT=/absolute/path/to/package` is a strict override: the
package must already contain matching `manifest.json`, `include` and `lib`;
missing, corrupt or incompatible overrides fail without being rebuilt.
An incomplete or incompatible default package also fails for explicit inspection.
See the engine's `3rdparty/spidermonkey/README.md` for package and cache details.

Supported targets are macOS native ARM64, Android `arm64-v8a`, iOS ARM64 devices
and ARM64 simulators. Device and simulator packages are separate. Cold Apple
preparation receives the application's selected SDK and deployment target;
existing packages must support that minimum OS. Builder defaults are macOS 11.0
and iOS 15.0. ARM64 uses the interpreter and iOS disables JIT.

`AX_ENABLE_EXT_JS` is enabled by the template. Set `AX_PREBUILT_DIR` to an Axmol
SDK build directory that was compiled with JavaScript enabled to reuse its
exported library map. Copy the engine compatibility scripts from
`extensions/scripting/js-bindings/script` when upgrading an existing project.

Install the VS Code extension from `tools/js-debugger`, then use the supplied
`.vscode/launch.json`. Its example executable path matches a CMake Makefiles
build in `build`; adjust it for the build directory and generator you use.
`AXMOL_JS_DEBUG=1` enables the native debugging service in a development build;
`AXMOL_JS_DEBUG_WAIT=1` waits for the debugger before loading the entry script.

Call `runNativeReflectionSample()` after startup to execute the Java/Objective-C
bridge samples. The Android class names are preserved by the included R8 rules.
Replace the sample bridge and corresponding keep rules with your own native
entrypoints when integrating platform SDKs.

ARM64 executes through SpiderMonkey's interpreter, and iOS disables JIT. Newer JS language features beyond SpiderMonkey
33.1 are outside this template's language baseline. Old `.jsc` files must be
validated against this runtime; the dependency package's `bin/axmol-jscompile`
can create matching bytecode from source.
