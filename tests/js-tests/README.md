# JavaScript test application

A normal launch opens the complete original Cocos2d-x v3 native test menu with
Axmol's `axjs` library and SpiderMonkey 33.1. The imported scripts and assertions
remain unchanged. This app is independent of `tests/js-runtime-tests`.

Build from the Axmol checkout after the normal engine setup:

```sh
axmol build -d tests/js-tests -t js-tests -p osx -a arm64
```

Use the corresponding Android ARM64, iOS device or ARM64 simulator toolchain for
other supported targets. Build products belong under `tmp/`.

`Content/legacy/js-tests/` contains the complete original scripts, licenses and
per-file provenance. CMake prepares all 1,232 original resource files from pinned
upstream revisions into the engine cache and verifies their recorded SHA256
before packaging. The installed application includes the complete resource set.
`Content/legacy-compat/` supplies the Spine 4.3 example-resource overlay with its
own licenses and provenance. Retain all original and overlay notices.

Desktop and Apple environment variables, or matching Android Intent extras,
select focused modes:

- `AXMOL_JS_BINDING_SMOKE`, `AXMOL_JS_NATIVE_SMOKE`, `AXMOL_JS_FILE_ASSETS_SMOKE`.
- `AXMOL_JS_SCENE_SMOKE`, `AXMOL_JS_AUDIO_SMOKE`, `AXMOL_JS_STUDIO_SMOKE`,
  `AXMOL_JS_MATERIAL_SMOKE`, `AXMOL_JS_LEGACY_HELLO`.
- `AXMOL_JS_V3_FULL_SUITE`, `AXMOL_JS_V3_AUTORUN`, `AXMOL_JS_V3_MENU_SMOKE`,
  `AXMOL_JS_V3_RESTART_SMOKE`. The full-suite harness can also select a category,
  registry entry, run nonce and captures through its `AXMOL_JS_V3_*` options.
- `AXMOL_JS_GPU_COVERAGE` selects an additional GPU regression, such as
  `legacyGL`, `legacyGLTexture`, `filterClear`, `resource`, `remoteResource`,
  `ttfConfig`, `labelAtlasSampler`, `spriteTextureFormats`, `renderTexture`,
  `componentLose`, `componentWin` or `materialTexture`. `remoteResource` requires
  a separately supplied HTTP fixture service on `127.0.0.1:19093`.
- `AXMOL_JS_BENCHMARK` and `AXMOL_JS_V3_PERFORMANCE` select performance workloads;
  `AXMOL_JS_SCALE9_FRAME_PROBE` selects the focused Scale9 frame check.

Native acceptance diagnostics are off by default. Configure
`AX_JS_TEST_NATIVE_DIAGNOSTICS=ON` for the `materialTexture` GPU mode.
`AX_JS_TEST_PERFORMANCE_METRICS` separately enables process metrics.
Release builds do not enable the debugger.

The app ships the original suite. Requests for corrected variants, specialized
observer sidecars or their service configurations fail explicitly because those
assets are outside this sample. Menu startup, source loading and rendered frames
are distinct from functional acceptance. Visual effects, real input and external
platform services require their respective checks.
