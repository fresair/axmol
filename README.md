# Axmol with native JavaScript

[简体中文](README_CN.md)

`axmol-js` adds native JavaScript to the Axmol 2D and 3D game engine alongside C++ and Lua. It runs scripts with SpiderMonkey 33.1 and provides Cocos2d-x v3-style APIs, including `cc`, `ccui`, `ccs`, `sp` and `jsb`. Supported JavaScript targets are Android `arm64-v8a`, macOS on Apple silicon, and iOS on ARM64 devices and simulators.

## Create a JavaScript game

Install PowerShell 7, a C++23 compiler and the tools for your target platform. Android builds use NDK r27d. Clone the JS branch and run the setup script from the engine checkout. [Platform setup guide](docs/DevSetup.md)

```sh
git clone --branch axmol-js https://github.com/fresair/axmol.git
cd axmol
pwsh ./setup.ps1
```

Restart your terminal and return to the checkout. To create a game and build it on an Apple silicon Mac, run the following commands.

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

The game starts in `Content/main.js`. The first native build prepares the pinned SpiderMonkey package if needed. Later builds reuse it. For an Android build, run `axmol build -p android -a arm64` from the game directory. [JavaScript template](templates/js/README.md) · [Runtime prerequisites](3rdparty/spidermonkey/README.md)

## Run the tests

To explore the original Cocos2d-x v3 scenes on an Apple silicon Mac, build the test app from the engine checkout and open it. Its default menu lets you choose a scene.

```sh
axmol build -d tests/js-tests -t js-tests -p osx -a arm64
```

Use `tests/js-tests` to check rendering and interaction. The separate `tests/js-runtime-tests` suite checks the VM, native object lifetime and callbacks. [Interactive test app](tests/js-tests/README.md) · [Runtime tests](tests/js-runtime-tests/README.md) · [VS Code debugger](tools/js-debugger/README.md)

## Project and license

This repository is a fork of Axmol. Ye Liangqi's original contributions and modifications to axmol-js are distributed under the MIT License. Upstream authors retain their notices. Bundled dependencies and sample assets, including the Spine examples, have their own license terms. [Upstream Axmol](https://github.com/axmolengine/axmol) · [Contributing](CONTRIBUTING.md) · [Authors](AUTHORS.md) · [License](LICENSE) · [Spine sample notices](tests/js-tests/Content/legacy-compat/spine/README.md)
