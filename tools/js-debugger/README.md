# Axmol JavaScript debugger

This VS Code extension debugs the native SpiderMonkey 33.1 runtime. The adapter
speaks DAP to VS Code and the Cocos v3 actor protocol to Axmol. It implements
source/conditional breakpoints, startup pause, pause/continue, step into/over/out,
call stacks, lexical scopes, object properties, expression evaluation, caught
and uncaught exception stops, and remote source loading. Runtime requests are
handled on the Director thread; paused game code stays stopped while debugger
requests execute in a native nested event loop.

## Install

Use Node.js 20.19+ (the adapter itself also works with Node.js 18):

```sh
cd tools/js-debugger
npm test
npm run package
code --install-extension ../../tmp/js-debugger/axmol-js-debugger-1.0.0.vsix
```

`npm run package` uses the included Python 3 VSIX packer and works offline,
writing the extension to repository `tmp/js-debugger`. The packer also accepts
`--output /path/to/extension.vsix`.
`npm run package:vsce` alternatively downloads and uses the official
`@vscode/vsce` packer. The extension and adapter have no runtime npm
dependencies. To run a DAP client other than VS Code, start
`node tools/js-debugger/src/adapter.js` and exchange DAP messages on stdin/stdout.
All adapter diagnostics use DAP output events; stdout contains only DAP frames.

Copy `templates/launch.json` to your project's `.vscode/launch.json`. For macOS
launch, set `program` to the actual executable inside your application's `.app`.
The template stores scripts in `Content/`, so its `sourceRoot` uses that
directory; adjust it for existing projects whose `main.js` lives elsewhere.
Launch starts the native program with `AXMOL_JS_DEBUG=1` and
`AXMOL_JS_DEBUG_WAIT=1`, then connects and installs breakpoints before allowing
`main.js` to execute. `stopOnEntry` pauses at the first game script frame.

## Runtime startup and resource packaging

Build Debug or RelWithDebInfo with `AX_ENABLE_JS_DEBUGGER` enabled. The JS template
checks `AXMOL_JS_DEBUG` and `AXMOL_JS_DEBUG_WAIT`. Ordinary application startup
does not wait for a debugger unless explicitly enabled. Release has no debugger
listener by default.

Applications using custom startup can use the native API after the JS engine
starts and before running `main.js`:

```cpp
#include "debugger/Debugger.h"

auto* engine = ax::JsEngine::getInstance();
engine->start();
engine->addUpdateCallback([](float) { axjs::debugger::pump(); });
engine->addCleanupCallback([] { axjs::debugger::shutdown(); });
JS::RootedObject global(engine->getContext(), engine->getGlobalObject());
axjs::debugger::Options options;
options.waitForConnection = true;
if (!axjs::debugger::initialize(engine->getContext(), global, options)) {
    // Handle initialization failure before starting game scripts.
    return false;
}
engine->runScript("main.js");
```

Package `script/jsb_debugger.js` with the JS resources. The debugger evaluates
this script in its own compartment; do not `require` it in the game global.
The reference runtime's `CompileOptions` retain logical resource filenames
(`main.js`, `src/app.js`) so local project paths match across platforms.

The default bind address is `127.0.0.1` and port is 5086. `Options.bindAddress`
and `Options.port` support explicit device/network configuration. A startup
wait ends after client configuration and resume, or after a connected client
disconnects. The listener survives disconnect, and a fresh client can attach
again. Restarting the VM requires shutdown before context destruction and a
fresh initialization after startup.

## Android and Apple devices

Android local attach uses ADB forwarding:

```sh
adb forward tcp:5086 tcp:5086
```

Start a development build with the debugger enabled. Android applications do
not normally inherit a desktop shell's environment variables; custom startup
should read a development setting and use the native API above. Choose **Attach
Axmol JavaScript** in VS Code. The template's development flags can instead be
set directly in its AppDelegate for a device development build.

macOS and an iOS simulator use localhost attach. For an iOS device, explicitly
configure the application's debugger bind address (for example `0.0.0.0`) and
set the DAP `host` to the device's development network address. Normal localhost
is the default. iOS debugger operation uses the same SpiderMonkey Debugger API
and does not require a system JavaScriptCore inspector.

## Sources, exceptions and evaluation

`sourceRoot` names the local directory containing `main.js` and `src/`. If
custom compilation uses absolute target filenames, set `remoteRoot` to the
corresponding target root, or use `sourceMap` for multiple prefixes:

```json
"sourceRoot": "${workspaceFolder}",
"sourceMap": { "/device/game/Resources": "${workspaceFolder}" }
```

Breakpoints can be set before loading a script; the adapter reports them as
pending and sends a verified breakpoint event once executable offsets exist.
Blank lines move to the next executable line. Hit counts and logpoints are
reported as unsupported. Step operations follow source lines rather than every
bytecode instruction.

Enable **All thrown exceptions** or **Uncaught exceptions** in the VS Code
breakpoints view. Uncaught detection uses SpiderMonkey 33's `isInCatchScope`.
Evaluation is available while stopped and may invoke game functions or change
state. Property inspection lists descriptors without invoking getters. Resuming
expires all frame, lexical environment and object references; subsequent
inspection requires the new stack. Matching bytecode preserves filenames, but
source inspection requires source text to be present in the compiled script.

Disconnect removes step hooks/breakpoints and resumes the game. Terminate kills
only a process launched by this adapter; an attached external application is
detached. Attach does not auto-connect to a different runtime after disconnect;
start a fresh debug session after VM/application restart.

## Validation

`npm test` checks UTF-8 framing and fragmented packets, connection retry,
request correlation/timeouts, the actual actor script with a Debugger test
double, and DAP interactions. TCP tests explicitly skip when the environment
forbids loopback listeners; memory-transport and DAP tests still run.

Native runtime regressions are registered separately by
[`tests/js-runtime-tests`](../../tests/js-runtime-tests/README.md).
Application and device debugger acceptance additionally require a development
application with the debugger enabled and the matching script resources.
The Node test suite alone does not establish native device behavior.

Protocol shapes are derived from the MIT Cocos2d-x v3 debugger at reference
commit `e4b6a5ef8f`. The actor implementation supplies game debugging APIs;
it does not include browser DOM/XPCOM actors.
