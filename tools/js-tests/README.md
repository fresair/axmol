# JavaScript test resources and native regressions

The interactive application is in `tests/js-tests`. Commands below run from the
Axmol repository root. CMake calls `prepare-resources.py` through
`Resources.cmake` to verify or fetch checksum-pinned upstream application assets.
The authored suite's `Content/legacy/js-tests/provenance.json` records the exact
source revisions, resource sizes, hashes and source paths. The shared cache is
`cache/js-tests`; generated resource links and downloaded containers are not
source files. Set `AX_JS_TEST_RESOURCE_OFFLINE=ON` to require an existing cache.

Copy the built-in test app with:

```sh
./tools/cmdline/axmol new --from-test js-tests -d tmp/projects/js-tests
```

The copy retains its authored files. CMake prepares framework script and pinned
resource links for the selected engine checkout. Configure the copied project
with an explicit engine root when it cannot be inferred from its location.

The complete original project definition loader uses an existing JavaScript SDK
and the prepared application fixtures:

```sh
cmake -S tools/js-tests/full-suite-load -B tmp/js-full-suite-load \
  -DAX_PREBUILT_DIR=/absolute/path/to/verified-js-sdk \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build tmp/js-full-suite-load --target js-full-suite-load-tests
ctest --test-dir tmp/js-full-suite-load --output-on-failure --no-tests=error
```

This checks the original script load list and menu definitions. It does not
prove graphical output, animation, input or every menu subcase.
The [runtime regression suite](../../tests/js-runtime-tests/README.md) registers
separate executable tests. Its CMake sources also select the required small
fixtures and native checks under this directory. Keep those original assertions
and fixture bytes when changing compatibility behavior.

`tests/js-tests` optionally compiles native observers with
`AX_JS_TEST_NATIVE_DIAGNOSTICS=ON`. Audio and pipeline tracing and process
metrics have their own explicit CMake options. Their native sources and local
headers remain test instrumentation. Ordinary app configuration leaves these
options off. Store temporary SDKs, applications and test output under `tmp/`.
