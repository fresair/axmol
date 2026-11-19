<p align="center"><a href="https://axmol.dev"><img width="160" src="https://axmol.dev/assets/img/logo.png" alt="Axmol logo"></a></p>

# Axmol with native JavaScript

The `axmol-js` branch of `fresair/axmol` adds native JavaScript to the Axmol 2D and 3D game engine alongside C++ and Lua. It uses SpiderMonkey 33.1 and provides Cocos2d-x v3-style APIs for existing scripts, including `cc`, `ccui`, `ccs`, `sp` and `jsb`. JavaScript targets are Android ARM64, macOS on Apple silicon, and iOS on ARM64 devices and simulators. [JavaScript project guide](templates/js/README.md) · [简体中文](README_CN.md)

This branch derives from Axmol. Ye Liangqi holds copyright in the original contributions and modifications made for axmol-js in 2026. Project-owned changes use the MIT License. Upstream work, third-party components and sample assets retain their own notices. [Upstream Axmol](https://github.com/axmolengine/axmol) · [License](LICENSE) · [Authors](AUTHORS.md)

## Get started

Install a C++23 compiler, PowerShell 7 and the tools for your target platform. Clone this branch and run setup from the checkout. [Platform setup guide](docs/DevSetup.md)

```sh
git clone --branch axmol-js https://github.com/fresair/axmol.git
cd axmol
pwsh ./setup.ps1
```

Restart your terminal, return to the checkout, then create and build a JavaScript game. This example targets an Apple silicon Mac.

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

Open `Content/main.js` to see how the game starts. On the first JS build, CMake prepares the pinned SpiderMonkey package if needed. Subsequent builds reuse the validated package. Use `-l cpp` or `-l lua` to create a C++ or Lua project. [Runtime prerequisites and cache](3rdparty/spidermonkey/README.md)

## Run the JavaScript tests

From the engine checkout, build the original Cocos2d-x v3 test app and open it to explore the scenes from its default menu. The following command targets an Apple silicon Mac.

```sh
axmol build -d tests/js-tests -t js-tests -p osx -a arm64
```

The separate runtime suite checks VM and native binding behavior. For example, a change to object lifetime belongs in the runtime tests, while a rendering change can also be exercised in the corresponding menu scene. [Test app](tests/js-tests/README.md) · [Runtime regressions](tests/js-runtime-tests/README.md) · [VS Code debugger](tools/js-debugger/README.md) · [Contributing](CONTRIBUTING.md)

## Upstream Axmol demos

Try the upstream C++ WebAssembly tests or the FairyGUI demo in your browser. [WebAssembly tests](https://axmol.netlify.app/wasm/cpp-tests/cpp-tests) · [FairyGUI demo](https://axmol.netlify.app/wasm/fairygui-tests/fairygui-tests) · [Games made with Axmol](https://github.com/axmolengine/axmol/wiki/Made-in-Axmol)

## Upstream Axmol v3 development

Major feature development for v3 beta1 is largely complete on `dev`. Current work focuses on final cleanup and release preparation.

- **Integrated:** C++23, Box2D v3 and Jolt physics, Vulkan/D3D11/D3D12 rendering backends alongside Metal and GL, and high-DPI improvements for Windows, Linux, and WebAssembly.
- **Experimental:** optional VR/OpenXR support.

The following rendering backends are enabled by default on `dev`. A build can include multiple backends.

| Platform | Default-enabled backends |
| --- | --- |
| Windows | D3D11, D3D12, GL, Vulkan |
| Linux | Vulkan, GL |
| Android | Vulkan, GL |
| WinUWP | D3D12, D3D11, GL |
| iOS / macOS | Metal |

GL refers to OpenGL or OpenGL ES depending on the platform. WebAssembly uses WebGL. See [rendering build options](CMakeOptions.md) for configuration and the [v3 roadmap](https://github.com/axmolengine/axmol/discussions/2650) for progress. These v3 capabilities do not describe the stable v2 release.

## Upstream Axmol branches

| Branch | Use it for | C++ standard |
| --- | --- | --- |
| [`dev`](https://github.com/axmolengine/axmol/tree/dev) (default) | v3 development and trying new features; APIs and experimental features may change | C++23 |
| [`release/2.x`](https://github.com/axmolengine/axmol/tree/release/2.x) | Stable v2 LTS releases and production projects | C++20 |

v3 beta1 is in preparation, and `dev` remains the v3 pre-release branch. The [latest stable release](https://github.com/axmolengine/axmol/releases) is from v2 LTS. For changes intended for upstream, see its [contribution guide](https://github.com/axmolengine/axmol/blob/dev/CONTRIBUTING.md).

## Upstream engine capabilities

- **Across platforms:** iOS, Android, Windows, Linux, macOS, tvOS, WebAssembly, and Xbox through UWP.
- **2D and 3D:** graphics and scene APIs, with Box2D v3 for 2D physics and Jolt for 3D physics on v3 dev.
- **Extensions:** FairyGUI, ImGui, Spine, Live2D, and Effekseer. See the [extensions guide](https://github.com/axmolengine/axmol/wiki/Extensions).

See the [documentation](https://axmol.dev/manual/latest/), [tutorials](https://github.com/axmolengine/axmol/wiki/Tutorials), and [FAQ](https://github.com/axmolengine/axmol/wiki/FAQ) for details.

## Background and migration

Axmol began in November 2019 as a fork of Cocos2d-x v4.0 and has continued to evolve with its contributors. If you have an existing Cocos2d-x project, see the [migration guide](https://github.com/axmolengine/axmol/wiki/Cocos2d%E2%80%90x-migration-guide) and [major changes from Cocos2d-x v4.0 to Axmol v3](https://github.com/axmolengine/axmol/wiki/Axmol-v3-VS-Cocos2d%E2%80%90x-4.0).

## Upstream community and support

[![Discord](https://img.shields.io/discord/1099599084895088670?label=discord)](https://discord.gg/QjaQBhFVay)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-blue.svg)](CONTRIBUTING.md)

Ask questions in [GitHub Discussions](https://github.com/axmolengine/axmol/discussions), join [Discord](https://discord.gg/QjaQBhFVay), or browse the [upstream contributors list](https://github.com/axmolengine/axmol/blob/dev/AUTHORS.md). If Axmol is useful to you, a GitHub star helps others discover it. You can also [support development](https://axmol.dev/sponsor).

### Upstream corporate patron

<a href="https://scorewarrior.com/?utm_source=axmol"><img src="https://cdn.prod.website-files.com/633da33305ac754156026dd8/63566f1edf5f0712f94f7f1b_sw-triangle-821890.svg" height="96" alt="Scorewarrior"></a>

## Upstream project activity

Explore [awesome-cpp](https://github.com/fffaraz/awesome-cpp).

![Axmol repository activity](https://repobeats.axiom.co/api/embed/6fcb8168a3af91ba9e797a1f14a3c2edc42ac56a.svg "Repobeats analytics")
