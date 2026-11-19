# SpiderMonkey 33.1 JavaScript bindings

These module configurations select the current Axmol C++ API and map its
classes into the Cocos2d-x v3 namespaces `cc`, `ccui`, `ccs`, `sp`, and `jsb`.
They use the same C# and libclang declaration collector as Lua, with an
independent SpiderMonkey C++ emitter. Lua remains the default language.

```powershell
axmol genbindings -l js
axmol genbindings -l js -m base
axmol genbindings -l js -Verify
```

The checked-in modules cover core 2D and 3D, UI, audio, networking utilities,
legacy scroll/table views and controls, scene I/O, scene extensions and timelines,
Spine, asset management, 2D and 3D physics, navigation, RHI, particle 3D,
platform video/web views, and CocosBuilder. JS overloads have checked argument conversion, explicit C++ method
pointer signatures, and shortened candidates for native default arguments.
Constructors use the shared runtime ownership policy; factories and getters
use its native identity registry. Parent types register before their subclasses
regardless of output shard layout. Nested enums attach to their owning class.

Outputs are `extensions/scripting/js-bindings/generated/axjs_*_gen.{h,cpp}`,
C++ shards, and `generated/manifests/axjs_*.json`. A successful complete run also
writes `axjs_register_gen.{h,cpp}` with `axjs::registerAllGenerated()`. Individual
modules expose `axjs::register_all_ax_<module>(JSContext*, JS::HandleObject)`.
C++ generation errors preserve the previous valid module output. `-Verify`
parses and checks the selected modules without writing generated files.

The JSON schema is adjacent to these configs. JS renames use `jsName`; Lua's
`luaName` schema remains unchanged. Manifests record the exact emitted surface
and signatures needing explicit manual conversion or lifetime adapters. Raw
internal records, opaque pointers, scalar out-parameters, variadic C++ APIs,
and C++ callbacks returned as values are not automatically exported. Public
fields must be explicitly whitelisted. Callback fields keep an assigned JS
function on the wrapper, so reads preserve the JS function's identity without
attempting to reconstruct a function from a native `std::function`.

`manual-adapters.json` grants a reasoned directory policy to the reviewed
legacy adapters. `manual-policy.md` describes which behaviors belong there.
Generated bindings do not implement alternate native reference counting or
embed a runtime-specific ownership policy in each module.

The normal parser uses Android NDK headers (JS defaults to ARM64/API 23; Lua
retains its existing ARMv7/API 21 defaults). `-NdkRoot` selects a local NDK.
`-LibClangRoot` and `-ClangSharpRoot` select explicit tool caches without
installing them globally. The managed assembly is compiled by PowerShell 7.4+
with its built-in Roslyn compiler; no dotnet SDK is required.

For host SDK verification on macOS, `-HostHeaders` uses the selected Xcode SDK
and its libc++ headers. The generator discovers the resource headers next to
an explicitly selected libclang, or accepts `-ClangResourceDirectory` (also
`AXMOL_CLANG_RESOURCE_DIR`). Matching parser and resource header versions avoids
mixing the NDK's compiler builtins with a newer parser. Use `-Verify` for host
checks so generated output retains the portable Android API selection:

```powershell
pwsh ./tools/cmdline/plugins/genbindings.ps1 -l js -HostHeaders -Verify `
  -LibClangRoot '<directory-containing-libclang>' `
  -ClangResourceDirectory '<matching-clang-resource-directory>'
```

The generator contract fixture checks both backends, defaults, overloads,
constructor emission, inheritance, callback typedefs, fields, nested enums,
and deterministic regeneration without a platform SDK:

```powershell
pwsh ./tools/bindings-generator/tests/run-tests.ps1 `
  -LibClangRoot <directory-containing-libclang>
```

The generator contract also checks public default constructor allocation and
namespace-qualified enum typedefs. Generated API counts describe the exposed
surface; they do not establish original test scenes or rendering results.
Manual-only native additions use `@script{ignore}`; the parser retains comments
in system headers, so these annotations apply when module include roots use
`-isystem`. The historical Lua binding of `Properties.getPath` remains an
explicit exception, with its pointer output handled manually for JS. JS span
arguments use owned array storage during native calls; unsupported span
references and view-element inputs remain explicit manual signatures.

This directory contains generator configuration and manual adapter policy.
The [runtime regression suite](../../tests/js-runtime-tests/README.md) exercises
native bindings, lifetime rules, original source assertions and historical
bytecode loading. The [application test tools](../js-tests/README.md) prepare
pinned resources and audit the complete original project definitions.
Production shader generation is documented in
[legacy-gl](../bindings-generator/js/legacy-gl/README.md).
