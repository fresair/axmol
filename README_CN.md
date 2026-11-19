# Axmol 原生 JavaScript 支持

[English](README.md)

`axmol-js` 在 Axmol 2D 和 3D 游戏引擎已有的 C++、Lua 支持上加入原生 JavaScript。脚本由 SpiderMonkey 33.1 执行，可以使用 Cocos2d-x v3 风格的 `cc`、`ccui`、`ccs`、`sp` 和 `jsb` 接口。JavaScript 支持 Android `arm64-v8a`、搭载 Apple 芯片的 Mac，以及 ARM64 的 iOS 真机和模拟器。

## 创建 JavaScript 游戏

先安装 PowerShell 7、支持 C++23 的编译器和目标平台的工具链。Android 构建使用 NDK r27d。克隆 JS 分支，然后在引擎目录运行安装脚本。[平台环境配置](docs/DevSetup.md)

```sh
git clone --branch axmol-js https://github.com/fresair/axmol-js.git
cd axmol-js
pwsh ./setup.ps1
```

重新打开终端，回到引擎目录。下面的命令会创建一个游戏，并为搭载 Apple 芯片的 Mac 构建。

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

游戏入口是 `Content/main.js`。首次原生构建时，如果缺少固定版本的 SpiderMonkey 依赖包，构建系统会自动准备，后续构建复用该包。要构建 Android 版本，在游戏目录运行 `axmol build -p android -a arm64`。[JavaScript 模板说明](templates/js/README.md) · [运行时构建要求](3rdparty/spidermonkey/README.md)

## 运行测试

要在搭载 Apple 芯片的 Mac 上查看原版 Cocos2d-x v3 场景，请回到引擎目录，构建并打开测试 App，从默认菜单选择场景。

```sh
axmol build -d tests/js-tests -t js-tests -p osx -a arm64
```

用 `tests/js-tests` 检查渲染和交互。独立的 `tests/js-runtime-tests` 用于检查虚拟机、原生对象生命周期和回调。[交互测试 App](tests/js-tests/README.md) · [运行时测试](tests/js-runtime-tests/README.md) · [VS Code 调试器](tools/js-debugger/README.md)

## 项目与许可

本仓库基于 Axmol 二次开发。Ye Liangqi 为 axmol-js 创作的新增代码及修改部分采用 MIT 许可证。上游作者保留原有版权声明，随附的第三方依赖和示例资源（包括 Spine 示例）分别适用各自的许可条款。[上游 Axmol](https://github.com/axmolengine/axmol) · [贡献指南](CONTRIBUTING.md) · [作者](AUTHORS.md) · [许可证](LICENSE) · [Spine 示例许可说明](tests/js-tests/Content/legacy-compat/spine/README.md)
