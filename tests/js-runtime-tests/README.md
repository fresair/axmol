# JavaScript runtime regression tests

Focused C++/JavaScript regression programs for the real Axmol engine and
SpiderMonkey 33.1. This directory is independent of the interactive application
in `tests/js-tests`; neither test entry point runs the other automatically.

## Test targets

- `js-runtime-tests`: VM lifetime, GC, native wrapper identity and callbacks.
- `js-bindings-tests`: generated/manual bindings and compatibility contracts.
- `js-network-tests` and `js-socketio-tests`: real local HTTP/WebSocket and legacy protocol flows.
- `js-assets-tests`, `js-file-tests`: hot-update and actual filesystem behavior.
- Additional focused targets cover audio/listener lifetime, properties, material,
  RenderTexture, TMX, TIFF, scheduling and physics, depending on enabled features.

The scripts and small fixtures here belong to these regressions. The application
has its own copies of the fixtures it needs, so it can configure and package
without this directory. `fixtures/` contains the runtime suite's own small JS/TMX/plist inputs, with
upstream license notices and a source/SHA manifest. These are required test
inputs, not records of previous test results. Full application definition-loading
audits live separately in `tools/js-tests/full-suite-load`.

## Build and run

After the normal Axmol setup, CMake prepares a missing ARM64 runtime package
using the [SpiderMonkey component](../../3rdparty/spidermonkey/README.md). Configure the
engine with `AX_ENABLE_EXT_JS=ON` and `AX_BUILD_JS_RUNTIME_TESTS=ON`. To omit the
interactive test applications, use `AX_BUILD_TESTS=OFF`.

```sh
cmake -S . -B tmp/js-runtime-tests \
  -DAX_ENABLE_EXT_JS=ON -DAX_BUILD_JS_RUNTIME_TESTS=ON \
  -DAX_BUILD_TESTS=OFF -DAX_UPDATE_BUILD_VERSION=OFF
cmake --build tmp/js-runtime-tests --target js-runtime-tests js-bindings-tests
ctest --test-dir tmp/js-runtime-tests --output-on-failure -R '^js-(runtime|bindings)-tests$'
```

Use the platform toolchain and enabled engine features appropriate to the target.
Desktop hosts run the executables directly; Android command-line test executables
need the matching ARM64 runtime libraries and test fixtures. iOS interactive and
platform integration checks use the `js-tests` application. Loopback networking
must be available for network tests; a sandbox restriction is not a test pass.

Keep builds, temporary server files and new run output under repository `tmp/`.
Results must be generated for the current source and matching runtime package.
A successful build does not establish test execution or graphical behavior.
The workflow in `.github/workflows/js-bindings.yml` builds and runs its selected
runtime tests separately from the complete application definition loader.
