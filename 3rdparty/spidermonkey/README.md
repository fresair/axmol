# SpiderMonkey 33.1 third-party dependency

Axmol's native JavaScript dependency lives here alongside the other third-party
components. The source recipe uses Cocos SpiderMonkey commit
`1d8c1d7eb188db095b5a7ce838ba0a933a0d414b` (upstream version `33.1.1`), its pinned
archive checksum and the patches in `patches/`. It does not substitute old Cocos
binary packages. SpiderMonkey compiles internally as C++11; Axmol, `axjs` and
public consumers remain C++23.

## Normal project workflow

Run the engine's `setup.ps1`, restart the terminal, then use the same workflow as
C++ and Lua. For example, on an Apple Silicon Mac:

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

Use `axmol build -p android -a arm64` for Android. Android JS requires NDK r27d
(`27.3.13750724`), API 23 or newer and `c++_shared`. Standard Axmol SDK/NDK setup
selects the installed toolchain; no manual NDK directory link is required. The
exact NDK revision check belongs to the JS runtime integration.

Project creation does not download or compile the runtime. When JS is enabled,
CMake imports an existing package after validation. If the default package is
absent, configure prepares it automatically before creating the imported target.
A cold preparation needs Python 3.9+, curl, patch, make and the platform compiler.
It downloads checksum-pinned source and, when no usable Python 2.7.18 is available,
builds an isolated interpreter for upstream mozbuild. No pip or global Python
installation is changed. A warm import needs neither Python 2 nor source archives.

`AX_ENABLE_EXT_JS` defaults to OFF; the JS template enables it. With JS disabled,
ordinary engine builds do not prepare, build or link SpiderMonkey.
C++, Lua and JS projects share the engine setup template: `_AX_ROOT` explicitly
selects a shared engine, otherwise `AX_ROOT` supplies it, and an isolated project
uses its embedded `axmol` directory. See the [template guide](../../templates/js/README.md).

## Package selection and cache

CMake provides `ax_spidermonkey` and its alias `axmol::spidermonkey`. It validates
source/patch pins, platform, architecture, JIT/GC policy, C++ ABI, public compile
definitions, generated configuration and library checksums, and minimum OS.
An explicit `AX_JS_SPIDERMONKEY_ROOT` must identify an existing complete package;
it is never built or overwritten automatically. Missing, corrupt or incompatible
packages fail. An incomplete or incompatible default package also fails for
inspection rather than being silently replaced.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `AX_JS_SPIDERMONKEY_ROOT` | Empty | Strict override for a complete source-built package. |
| `AX_JS_SPIDERMONKEY_CACHE_DIR` | Selected engine's `cache/js-runtime` | Runtime sources, tools, intermediate builds and default packages. |
| `AX_JS_SPIDERMONKEY_AUTO_BUILD` | `ON` | Prepare a missing default package at configure. |
| `AX_JS_SPIDERMONKEY_OFFLINE` | `OFF` | Forbid downloads during automatic preparation. |
| `AX_JS_SPIDERMONKEY_BUILD_JOBS` | `2` | Jobs for automatic source builds. |

Pass options through the normal CLI `-xc` option, for example:

```sh
axmol build -p osx -a arm64 \
  -xc "-DAX_JS_SPIDERMONKEY_CACHE_DIR=/absolute/path/to/cache,-DAX_JS_SPIDERMONKEY_OFFLINE=ON"
```

Default packages are `packages/<platform>-<architecture>` inside the cache,
containing `manifest.json`, `include`, `lib`, `LICENSE`, and, for macOS,
`bin/axmol-jscompile`. Header symlinks are dereferenced. Downloads and source
provenance stay in the cache; intermediate builds use its `tmp` subdirectory.
An isolated project owns its embedded engine's cache, independently of the CLI
installation. Preparation and explicit builders serialize mutations within one
cache and publish packages from staging directories after verification. Inspect
failures and their logs before explicitly removing incomplete output.

Offline cold preparation requires the verified SpiderMonkey archive plus either
a usable Python 2.7.18 or its verified cached source archive. Warm imports need
only the package. `spidermonkey-prepare.log` in the third-party CMake binary
directory records automatic preparation output; detailed compiler logs live in
the cache. Downloads respect the caller's proxy configuration;
`AX_JS_DOWNLOAD_NO_PROXY=1` explicitly disables proxy use for those downloads.

Current targets are native macOS ARM64, Android `arm64-v8a`, and iOS ARM64 device
and simulator. Automatic preparation supports ARM64 only. ARM64 uses the
interpreter and all iOS variants disable JIT. Historical x86_64 evidence is
retained; it does not extend the current platform acceptance scope. Device and
simulator packages are separate, and universal packages are not implied.

Cold CMake preparation forwards the selected Apple SDK and deployment target, or
the actual Android NDK directory. Builder defaults are macOS 11.0 and iOS 15.0;
existing packages must support the application's minimum OS. Android runtime
compilation remains API 23 with `c++_shared` and 16 KiB LOAD segment alignment.
Compile/link and cross-build checks do not establish device execution.

## Explicit package maintenance

Run these commands from the engine root when deliberately preparing packages
outside a normal project build. `prepare.py` also bootstraps Python when needed:

```sh
python3 -B 3rdparty/spidermonkey/prepare.py --platform macos --arch arm64
python3 -B 3rdparty/spidermonkey/prepare.py --platform ios --arch arm64
python3 -B 3rdparty/spidermonkey/prepare.py --platform ios-simulator --arch arm64
python3 -B 3rdparty/spidermonkey/prepare.py --platform android --arch arm64 --ndk /path/to/android-sdk/ndk/27.3.13750724
```

Use `--cache-dir`, `--output`, `--offline` and `--jobs` to select the working
cache, package destination and preparation policy. Apple commands also accept
`--deployment-target` and `--sysroot` for an explicit SDK path. Existing packages
are checked and reused; repair or rebuild is an explicit maintenance action.
`build.py` is the lower-level rebuild entry point; it requires an available
Python 2.7.18 (`--python2` can select one). `bootstrap-python2.py` builds the
isolated interpreter. The old `tools/js-runtime` Python paths remain import and
command-line compatibility wrappers.

## Smoke and bytecode tools

On macOS ARM64, from the engine root:

```sh
cmake -S 3rdparty/spidermonkey/smoke -B tmp/js-runtime-cmake-smoke -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build tmp/js-runtime-cmake-smoke --parallel
ctest --test-dir tmp/js-runtime-cmake-smoke --output-on-failure
cache/js-runtime/packages/macos-arm64/bin/axmol-jscompile input.js output.jsc
```

The C++23 embedder checks script evaluation, allocation/GC, exceptions, hot loops,
bytecode round trips and repeated VM lifetimes. `JS_Init` is process-wide until
final `JS_ShutDown`. Native macOS package building additionally tests ad-hoc
Hardened Runtime signing; that does not validate a distributable app's identity.
`axmol-jscompile` writes this runtime's SpiderMonkey XDR bytecode, without
encryption. Old `.jsc` artifacts need their own compatibility test.

Focused runtime audit drivers live in
[`tools/js-tests/runtime-audits`](../../tools/js-tests/README.md).
The production legacy GL shader generator lives in
[`tools/bindings-generator/js/legacy-gl`](../../tools/bindings-generator/js/legacy-gl/README.md).
Engine runtime tests remain under `tests/js-runtime-tests`; the complete native
interactive application remains under `tests/js-tests`. These are separate from
the production third-party package recipe.
