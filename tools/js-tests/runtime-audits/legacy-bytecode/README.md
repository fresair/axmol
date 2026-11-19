# Historical SpiderMonkey `.jsc` fixture

`legacy-sample.jsc` was generated with the historical precompiled Mac library
from [`cocos2d-x-3rd-party-libs-bin` at
`a85b979dcd6b5cb8aa2bf97e52b73a2d827b3cfb`](https://github.com/cocos2d/cocos2d-x-3rd-party-libs-bin/tree/a85b979dcd6b5cb8aa2bf97e52b73a2d827b3cfb/spidermonkey).
It was not generated with Axmol's rebuilt runtime. The historical helper used
that dependency's public headers, C++11, and the library's x86_64 slice through
Rosetta 2 on an ARM64 host. No additional x86_64 build target is required to run
the checked-in fixture on the supported ARM64 runtime.

`manifest.json` preserves the source and bytecode SHA-256 values, historical
artifact hashes and compiler definitions.
The old library reports `JavaScript-C33.1.1`; its upstream source commit is
unknown. The manifest does not claim that it was built from Axmol's pinned
SpiderMonkey source revision.

The current verifier is registered as `js-legacy-bytecode-tests` by
`tests/js-runtime-tests/CMakeLists.txt`. After configuring the JavaScript runtime
regressions as described in their [README](../../../../tests/js-runtime-tests/README.md):

```sh
cmake --build tmp/js-runtime-tests --target js-legacy-bytecode-tests
ctest --test-dir tmp/js-runtime-tests --output-on-failure \
  --no-tests=error -R '^js-legacy-bytecode-tests$'
```

CTest supplies the checked-in historical fixture. The verifier checks decoding
and execution without a source file, Unicode constants, preference for valid
historical bytecode, fallback from corrupt bytecode to source, failure when
neither bytecode nor source is usable, and VM restart. All original assertions
remain required. `AX_JS_TEST_TEMP_DIR` can select writable scratch storage.
Passing these checks establishes compatibility for this fixture, not every
older bytecode format, encryption layer or runtime fork.

The verifier and fixture source are MIT licensed. `LICENSE-MPL-2.0.txt` and
`LICENSE-UPSTREAM-NOTICE.txt` preserve Mozilla's license and notice. Historical
archives and headers remain external artifacts; they are not shipped here.
The documents were obtained from the
[pinned Mozilla source mirror](https://github.com/cocos2d/Spidermonkey/tree/1d8c1d7eb188db095b5a7ce838ba0a933a0d414b),
which is a licensing reference, not an assertion about the old binary's source.
