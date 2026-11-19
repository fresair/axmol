# Legacy GL shader asset compiler

`compile.py` translates unchanged imported Cocos2d-x v3 shader inputs into
reflected Axmol RHI shader archives and native exact-source lookup headers.
This is a maintainer regeneration tool; normal engine/application builds use
the committed archives and need neither shaderc nor SPIRV-Cross.
The runtime audits and GPU fixtures are in
[`tools/js-tests/runtime-audits/legacy-gl`](../../../js-tests/README.md).

Axslcc 4.x accepts HLSL and infers stages from `_vs.hlsl` / `_fs.hlsl` names.
It no longer accepts the old `-x glsl` frontend or `.vert` / `.frag` inputs.
The regeneration pipeline therefore preprocesses/normalizes the legacy GLSL,
compiles it with `glslc` to Vulkan 1.0 SPIR-V, translates that SPIR-V to HLSL
with SPIRV-Cross, then invokes current axslcc with repeated `-t` targets.
No shader expressions are rewritten by a GLSL-to-HLSL regex translator.

Use shaderc `glslc` (the recorded regeneration used v2026.4) and a SPIRV-Cross
CLI built from tag `vulkan-sdk-1.4.350.0`, exact commit
`1a6169566c73d3da552748fc372fe2bbb856e46e`. For example, in a temporary
maintainer tool directory under this checkout's `tmp/`:

```sh
git clone --depth 1 --branch vulkan-sdk-1.4.350.0 --single-branch \
  https://github.com/KhronosGroup/SPIRV-Cross.git tmp/legacy-gl-tools/SPIRV-Cross
git -C tmp/legacy-gl-tools/SPIRV-Cross rev-parse HEAD
cmake -S tmp/legacy-gl-tools/SPIRV-Cross -B tmp/legacy-gl-tools/build \
  -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF \
  -DSPIRV_CROSS_ENABLE_C_API=OFF -DSPIRV_CROSS_SKIP_INSTALL=ON
cmake --build tmp/legacy-gl-tools/build --target spirv-cross --parallel 2
python3 -B tools/bindings-generator/js/legacy-gl/compile.py --help
python3 -B tools/bindings-generator/js/legacy-gl/compile.py \
  --spirv-cross tmp/legacy-gl-tools/build/spirv-cross \
  --output-root tmp/legacy-gl-assets \
  --validation-report tmp/legacy-gl-validation.json
```

`--source-root` selects this checkout; `--compiler`, `--glslc`, and
`--spirv-cross` select explicit executables. `--output-root` stages generated
files outside the source tree. The default source root is inferred from this
script's location. The imported resource cache must already be prepared at
`tests/js-tests/Content/legacy/js-tests/res`. Production outputs are
`extensions/scripting/js-bindings/script/legacy-gl/shaders/` and
`extensions/scripting/js-bindings/manual/LegacyGL{ShaderManifest,BuiltinVariants}.h`.
Package the full binding `script/` directory with the application.

The bridge retains std140 `packoffset` declarations, restores original uniform
names, and annotates every user input/output with its original location. This
prevents DXC from compacting sparse attribute/varying locations independently
in separate stages. Axslcc assigns texture/custom-sampler bindings itself;
the generated HLSL's explicit resource registers are removed. The generated HLSL named samplers are an intermediate representation only.
Original attribute names are restored in reflection only after
checking actual semantic/location agreement; GPU attribute locations are never
patched. Eliminated inactive declarations are recorded in the manifest.

Legacy GLSL sampling parameters belong to each texture. A named HLSL custom
sampler requires SamplerRegistry registration and cannot represent that
contract by itself. `combined.py` therefore verifies the compiler's one-to-one
texture/sampler associations and emits the existing RHI combined-image ABI:
zero named sampler entries and invalid texture sampler references. GL/GLES
already expose the original combined texture uniforms. Metal source retains
one sampler argument at each matching texture slot; every actual sample call
must use that texture's own sampler. Existing RHI combined paths bind each
texture's current native sampler, preserving independent filter/wrap changes.
For Vulkan, `glslc -O` compiles the unchanged normalized GLSL directly to real
combined sampled-image descriptors in set 1. The lowering verifies descriptor
binding/type/array identity and the complete active std140 block layout before
repacking the AXSA 4 archive. No static/default sampler is registered. The
manifest records this Vulkan compilation path and sampler model explicitly.

Every successful output is parsed with the current `axslc-spec.h` wire layout:
AXSA 4.x, version patch and target count at their correct offsets, complete
stage/code/reflection bounds, GL 330, GLES 300, Vulkan SPIR-V 1.0, and MSL 2.0.
Validation checks vertex names/slots/semantics, original uniform names/std140
offsets and texture names. Metal must export `main0`, with actual MSL attribute
indices matching reflection. A second axslcc `--msl-ios` compilation must
produce exactly the same Metal code and reflection as desktop; this common
source serves macOS and iOS device/simulator. A difference fails regeneration.
These are archive/interface checks; actual platform shader compilation and GPU
output require runtime validation on the corresponding platform.

The imported shader, material and OpenGLTest.js inputs are checked against the
fixed v3 provenance hashes before regeneration. `builtins/provenance.json`
preserves the pinned upstream revision, payload hashes and extraction method;
its original source hashes are also checked. The named gray variants remain
derived compatibility sources. [`builtins/LICENSE.txt`](builtins/LICENSE.txt)
and [`cocos2d-x-MIT-LICENSE.txt`](cocos2d-x-MIT-LICENSE.txt) retain source notices.
The unreferenced malformed original `example_Twist.fsh` remains a recorded
failure; required shaders must all compile. Exact original bytes, shader stage
and canonical definitions determine runtime lookup; unknown source strings
fail explicitly.
