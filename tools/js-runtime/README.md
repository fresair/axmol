# SpiderMonkey compatibility entry points

The production SpiderMonkey source recipe, patches, Python bootstrap, smoke
consumer and bytecode compiler now live in
[`3rdparty/spidermonkey`](../../3rdparty/spidermonkey/README.md).
`build.py` and `bootstrap-python2.py` here preserve their old command-line and
Python import entry points; they delegate to that component.

For a new game, run the engine's `setup.ps1`, restart the terminal, then use the
standard project workflow (example for Apple Silicon macOS):

```sh
axmol new -p dev.axmol.hellojs -d ./projects -l js HelloJs
cd ./projects/HelloJs
axmol build -p osx -a arm64
```

CMake imports a validated existing package or automatically prepares a missing
default package from pinned source, including an isolated Python 2.7.18 when
needed. The default cache remains the selected engine's `cache/js-runtime`.
An explicit `AX_JS_SPIDERMONKEY_ROOT` remains a strict package override and is
never automatically built. Cache, offline, toolchain and manual maintenance
options are documented in the component guide above. C++ and Lua use the same
engine setup template; JS is enabled by its own template.

Runtime compatibility checks are registered by
[`tests/js-runtime-tests`](../../tests/js-runtime-tests/README.md).
Production legacy GL shader generation lives in
[`tools/bindings-generator/js/legacy-gl`](../bindings-generator/js/legacy-gl/README.md).
The interactive [`tests/js-tests`](../../tests/js-tests/README.md) application
uses its own resource preparation and acceptance entry points.
