#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
"""Compile the unchanged v3 OpenGLTest shader sources to reflected RHI archives.

The manifest binds exact source bytes to each archive. Runtime source lookup
never substitutes an unrelated shader for an unknown or invalid source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from archive import read_archive
from combined import lower as lower_combined_samplers
from bridge import (run, translate, restore_attribute_names, validate, validate_spirv_interfaces,
                    SPIRV_CROSS_TAG, SPIRV_CROSS_COMMIT)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def parse_defines(text):
    result = {}
    for token in re.split(r"[;,]", text):
        parts = token.strip().replace("=", " ", 1).split(None, 1)
        if parts:
            result[parts[0]] = parts[1].strip() if len(parts) == 2 else "1"
    return result


def define_key(text):
    return ";".join(f"{name}={value}" for name, value in sorted(parse_defines(text).items()))


def preprocessed(source, defines):
    # Expand conditional light/varying declarations before moving uniforms into
    # UBOs. Otherwise an inactive declaration can become an invalid zero array.
    macros = {"GL_ES": "1", "MAX_DIRECTIONAL_LIGHT_NUM": "1", "MAX_POINT_LIGHT_NUM": "1", "MAX_SPOT_LIGHT_NUM": "1"}
    macros.update(parse_defines(defines))
    original = re.sub(r"^\s*#version[^\n]*", "", source, flags=re.M)
    command = ["clang", "-E", "-P", "-x", "c", "-undef", "-"] + [f"-D{name}={value}" for name, value in macros.items()]
    result = subprocess.run(command, input=original, text=True, capture_output=True, check=True)
    return result.stdout


def normalized(source, stage, varying_locations, defines):
    source = preprocessed(source, defines)
    # Resolve the old precision-only branches. These shaders have no other
    # platform-dependent implementation; the numerical expressions are intact.
    source = re.sub(r"#ifdef GL_ES\s*(.*?)#else\s*(.*?)#endif", r"\2", source, flags=re.S)
    source = re.sub(r"#ifdef GL_ES\s*(.*?)#endif", "", source, flags=re.S)
    source = re.sub(r"^\s*#version[^\n]*", "", source, flags=re.M)
    source = re.sub(r"\b(lowp|mediump|highp)\s+", "", source)
    source = re.sub(r"\bprecision\s+\w+\s*;", "", source)
    source = source.replace("texture2D", "texture").replace("textureCube", "texture")
    aliases = {"CC_MVPMatrix": "u_MVPMatrix", "CC_PMatrix": "u_MVPMatrix", "CC_Texture0": "u_tex0", "CC_MVMatrix": "u_MVMatrix", "CC_NormalMatrix": "u_NormalMatrix"}
    if "CC_MVMatrix" in source:
        # Lighting computes eye-space positions, then applies projection.
        aliases["CC_PMatrix"] = "u_PMatrix"
    for old, new in aliases.items():
        source = re.sub(r"\b" + old + r"\b", new, source)
    uniform_pattern = re.compile(r"\buniform\s+(\w+)\s+(\w+)(\s*\[[^\]]+\])?\s*;")
    uniforms = []
    samplers = []

    def take_uniform(match):
        kind, name, array = match.groups()
        (samplers if kind.startswith("sampler") else uniforms).append((kind, name, array or ""))
        return ""

    source = uniform_pattern.sub(take_uniform, source)
    stripped = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    legacy_fragment_coordinates = stage == "frag" and re.search(r"\bgl_FragCoord\b", stripped) is not None
    if legacy_fragment_coordinates:
        # Original GL fragment coordinates have a lower-left origin. Vulkan
        # and Metal fragment positions are top-left. The native draw supplies
        # the actual framebuffer Y scale/bias, including offscreen mip targets.
        source = re.sub(r"\bgl_FragCoord\b", "axLegacyFragCoord()", source)
    known = {name for _, name, _ in uniforms + samplers}
    for name, kind in (("u_MVPMatrix", "mat4"), ("u_MVMatrix", "mat4"), ("u_PMatrix", "mat4"), ("u_NormalMatrix", "mat3"), ("CC_Time", "vec4"), ("CC_SinTime", "vec4"), ("CC_CosTime", "vec4"), ("CC_Random01", "vec4")):
        if name not in known and re.search(r"\b" + name + r"\b", stripped):
            uniforms.append((kind, name, ""))
    if "u_tex0" not in known and re.search(r"\bu_tex0\b", stripped):
        samplers.append(("sampler2D", "u_tex0", ""))
    if legacy_fragment_coordinates:
        # Append the private parameter after the original uniforms/builtins so
        # their std140 member order and native reflected offsets stay intact.
        uniforms.append(("vec2", "_axLegacyFragCoordY", ""))
    attributes = []

    def attribute(match):
        kind, name = match.groups()
        # Native Sprite uses fixed RHI POSITION0/TEXCOORD1/COLOR2 bindings.
        # Public old-GL slots remain POSITION0/COLOR1/TEXCOORD2; LegacyGLBindings
        # maps those logical slots through reflected semantics for raw draws.
        index = {"a_position": 0, "aVertexPosition": 0, "a_color": 2, "aVertexColor": 2, "a_texCoord": 1, "a_normal": 3, "a_blendWeight": 4, "a_blendIndex": 5, "a_tangent": 6}.get(name, len(attributes))
        attributes.append({"name": name, "type": kind, "location": index})
        return f"layout(location={index}) in {kind} {name};"

    source = re.sub(r"\battribute\s+(\w+)\s+(\w+)\s*;", attribute, source)

    def varying(match):
        kind, name, array = match.groups()
        qualifier = "out" if stage == "vert" else "in"
        return f"layout(location={varying_locations[name]}) {qualifier} {kind} {name}{array or ''};"

    source = re.sub(r"\bvarying\s+(\w+)\s+(\w+)(\s*\[[^\]]+\])?\s*;", varying, source)
    prelude = "#version 450\n"
    if uniforms:
        binding = 0 if stage == "vert" else 1
        prelude += f"layout(std140,set=0,binding={binding}) uniform LegacyUniforms {{\n"
        constants = dict(re.findall(r"\bconst\s+int\s+(\w+)\s*=\s*(\d+)\s*;", source))
        for kind, name, array in uniforms:
            for constant, value in constants.items():
                array = re.sub(r"\b" + constant + r"\b", value, array)
            prelude += f"    {kind} {name}{array};\n"
        prelude += "};\n"
    prelude += "".join(f"layout(set=1,binding={index}) uniform {kind} {name}{array};\n" for index, (kind, name, array) in enumerate(samplers))
    if stage == "frag":
        prelude += "layout(location=0) out vec4 legacyFragmentColor;\n"
        source = source.replace("gl_FragColor", "legacyFragmentColor")
        if legacy_fragment_coordinates:
            prelude += "vec4 axLegacyFragCoord() { vec4 value=gl_FragCoord; value.y=value.y*_axLegacyFragCoordY.x+_axLegacyFragCoordY.y; return value; }\n"
    return prelude + source, attributes


def verify_input_pins(root, project):
    provenance = json.loads((project / "provenance.json").read_text())
    revision = "e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f"
    if provenance["revision"] != revision:
        raise ValueError("Legacy test revision changed")
    pinned = {record["path"]: record["sha256"] for record in provenance["files"]}
    paths = [project / "src/OpenGLTest/OpenGLTest.js"]
    for directory in ("res/resjs/Shaders", "res/Shaders", "res/Shaders3D", "res/Sprite3DTest", "res/Materials"):
        paths += [path for path in (project / directory).iterdir()
                  if path.suffix in (".vsh", ".fsh", ".vert", ".frag", ".material")]
    for path in paths:
        label = str(path.relative_to(project))
        if digest(path.read_bytes()) != pinned[label]:
            raise ValueError("Pinned legacy shader/material input changed: " + label)
    builtins = root / "tools/bindings-generator/js/legacy-gl/builtins"
    builtin_provenance = json.loads((builtins / "provenance.json").read_text())
    if builtin_provenance["commit"] != revision:
        raise ValueError("Builtin shader revision changed")
    for record in builtin_provenance["files"]:
        if digest((builtins / record["sourceFile"]).read_bytes()) != record["sourceSha256"]:
            raise ValueError("Pinned builtin shader input changed: " + record["sourceFile"])
    return revision


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--glslc", type=Path, default=Path("glslc"), help="Maintainer GLSL frontend (shaderc)")
    parser.add_argument("--spirv-cross", type=Path, required=True, help="Maintainer SPIRV-Cross CLI at the pinned SDK tag")
    parser.add_argument("--validation-report", type=Path, help="Optional detailed archive/reflection validation JSON")
    parser.add_argument("--output-root", type=Path, help="Stage generated files outside the source tree")
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = args.compiler or root / "tools/external/axslcc/bin/axslcc"
    output_root = args.output_root.resolve() if args.output_root else root
    project = root / "tests/js-tests/Content/legacy/js-tests"
    destination = output_root / "extensions/scripting/js-bindings/script/legacy-gl/shaders"
    destination.mkdir(parents=True, exist_ok=True)
    upstream_revision = verify_input_pins(root, project)
    original_js = project / "src/OpenGLTest/OpenGLTest.js"
    items = []
    for file in sorted((project / "res/resjs/Shaders").iterdir()):
        if file.suffix in (".vsh", ".fsh"):
            items.append((str(file.relative_to(project)), "vert" if file.suffix == ".vsh" else "frag", file.read_bytes().decode(), ""))
    for file in sorted((project / "res/Shaders3D").iterdir()):
        if file.suffix in (".vert", ".frag"):
            items.append((str(file.relative_to(project)), "vert" if file.suffix == ".vert" else "frag", file.read_bytes().decode(), ""))
    for material in sorted((project / "res/Materials").glob("*.material")):
        for block in re.finditer(r"\bshader\s*\{([^}]+)", material.read_text()):
            # An empty native `defines =` property must remain empty. Using
            # \s here would consume the following vertexShader property.
            defines = re.search(r"^[ \t]*defines[ \t]*=[ \t]*([^\r\n]*)", block.group(1), re.M)
            definitions = define_key(defines.group(1)) if defines else ""
            for key, stage in (("vertexShader", "vert"), ("fragmentShader", "frag")):
                match = re.search(r"\b" + key + r"\s*=\s*([^\s]+)", block.group(1))
                if match:
                    file = project / "res" / match.group(1)
                    items.append((str(file.relative_to(project)), stage, file.read_bytes().decode(), definitions))
    # Native Sprite3D and camera shader cases use these exact imported files.
    # Keep original source bytes; shader-language translation happens below.
    for directory in ("Sprite3DTest",):
        for file in sorted((project / "res" / directory).iterdir()):
            if file.suffix in (".vert", ".frag", ".vsh", ".fsh"):
                items.append((str(file.relative_to(project)), "vert" if file.suffix in (".vert", ".vsh") else "frag", file.read_bytes().decode(), ""))
    # Native old-GL builtins use combined texture samplers so texture sampling
    # parameters remain dynamic. Preserve the original payloads; the explicitly
    # named gray variants are new compatibility sources, not upstream originals.
    for file in sorted((root / "tools/bindings-generator/js/legacy-gl/builtins").iterdir()):
        if file.suffix in (".vert", ".frag"):
            items.append((str(file.relative_to(root)), "vert" if file.suffix == ".vert" else "frag", file.read_text(), ""))
    expression = r'var\s+(vsh|fsh)\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*\+\s*)*"(?:[^"\\]|\\.)*")\s*;'
    for match in re.finditer(expression, original_js.read_text()):
        source = "".join(json.loads(token.group(0)) for token in re.finditer(r'"(?:[^"\\]|\\.)*"', match.group(2)))
        items.append((f"src/OpenGLTest/OpenGLTest.js:{match.group(1)}", "vert" if match.group(1) == "vsh" else "frag", source, ""))
    items = list({(stage, source, definitions): (label, stage, source, definitions) for label, stage, source, definitions in items}.values())
    # Locations must match across separately compiled stages. Reserve the
    # actual maximum array/matrix width instead of four slots per name, which
    # needlessly exceeds GLES' minimum 16 varying-vector locations.
    widths = {}
    for _, _, source, definitions in items:
        expanded = preprocessed(source, definitions)
        pattern = r"\bvarying\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)(?:\s*\[\s*(\d+)\s*\])?\s*;"
        for kind, name, array in re.findall(pattern, expanded):
            columns = int(kind[3]) if kind.startswith("mat") else 1
            widths[name] = max(widths.get(name, 0), columns * (int(array) if array else 1))
    varying_locations = {}
    next_location = 0
    for name, width in sorted(widths.items()):
        varying_locations[name] = next_location
        next_location += width
    if next_location > 16:
        raise ValueError(f"Original shaders need {next_location} global varying locations; partition their shader families")
    compiler_version = run([compiler, "--version"]).strip()
    if not compiler_version.startswith("axslcc v4."):
        raise ValueError("This recipe requires axslcc 4.x")
    cross_revision = subprocess.check_output([str(args.spirv_cross), "--revision"], stderr=subprocess.STDOUT, text=True).strip()
    if "Git commit: " + SPIRV_CROSS_TAG + " " not in cross_revision:
        raise ValueError("SPIRV-Cross must be built from " + SPIRV_CROSS_TAG)
    glslc_version = run([args.glslc, "--version"]).strip()
    records = []
    validations = []
    (root / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="legacy-gl-", dir=root / "tmp") as temporary:
        for label, stage, source, definitions in items:
            key = digest(source.encode() + (b"\0" + definitions.encode() if definitions else b""))
            converted, attributes = normalized(source, stage, varying_locations, definitions)
            input_path = Path(temporary) / f"{key}.{stage}"
            input_path.write_text(converted)
            output_path = destination / f"{key}.{stage}.sc"
            hlsl_path = Path(temporary) / f"{key}_{'vs' if stage == 'vert' else 'fs'}.hlsl"
            record = {"source": label, "sourceSha256": digest(source.encode()), "defines": definitions, "stage": stage, "attributes": attributes,
                      "normalizedSha256": digest(converted.encode()), "legacyFragmentCoordinates": stage == "frag" and "axLegacyFragCoord()" in converted,
                      "archive": f"script/legacy-gl/shaders/{output_path.name}", "compiled": False}
            try:
                reflection, hlsl = translate(input_path, hlsl_path, attributes, args.glslc, args.spirv_cross)
                record["hlslSha256"] = digest(hlsl.encode())
                command = [compiler, "-a", "-O2", "-t", "vk", "-t", "gl", "-t", "gles", "-t", "mtl", "-o", output_path, hlsl_path]
                run(command)
                if stage == "vert":
                    restore_attribute_names(output_path, attributes)
                combined_associations = lower_combined_samplers(output_path, input_path, reflection, args.glslc, args.spirv_cross)
                report = validate(output_path, stage, attributes, reflection)
                report["textureOwnedSamplerAssociations"] = combined_associations
                report["spirvInterfaces"] = validate_spirv_interfaces(output_path, reflection, args.spirv_cross, Path(temporary))
                # These shaders use portable MSL 2.0 source. Compile iOS too,
                # and require its full Metal code/reflection to equal desktop.
                # One shared archive then serves macOS, iOS device/simulator.
                ios_path = Path(temporary) / "ios.sc"
                run([compiler, "-a", "-O2", "-t", "mtl", "--msl-ios", "-o", ios_path, hlsl_path])
                if stage == "vert":
                    restore_attribute_names(ios_path, attributes)
                lower_combined_samplers(ios_path, input_path, reflection, args.glslc, args.spirv_cross)
                desktop_metal = next(x for x in read_archive(output_path.read_bytes())["targets"] if x["language"] == 2)
                if read_archive(ios_path.read_bytes())["targets"] != [desktop_metal]:
                    raise ValueError("Metal desktop/iOS output differs; separate platform assets required")
                report["metalDesktopDeviceSimulatorEquivalent"] = True
                validations.append(dict(source=label, archive=record["archive"], **report))
                record["archiveVersion"] = report["version"]
                record["samplerModel"] = "texture-owned-combined"
                record["vulkanCodeSource"] = "glslc-normalized-glsl" if combined_associations else "axslcc-hlsl"
                record["inactiveDeclarations"] = {key: report[key] for key in ("inactiveUniforms", "inactiveTextures", "inactiveAttributes")}
                record["compiled"] = True
                record["archiveSha256"] = digest(output_path.read_bytes())
                print(f"PASS {label}")
            except ValueError as error:
                # Preserve the malformed, unused original Twist diagnostic.
                # No host paths leak into generated provenance.
                record["diagnostic"] = str(error).replace(str(temporary), "<temporary>").replace(str(root), "<source-root>")
                print(f"FAIL {label}: {record['diagnostic'][:2000]}")
            records.append((record, source))
    manifest = {"compiler": compiler_version, "upstreamRevision": upstream_revision,
                "bridge": {"glslc": glslc_version, "spirvCrossTag": SPIRV_CROSS_TAG, "spirvCrossCommit": SPIRV_CROSS_COMMIT},
                "targets": ["vk-100", "gl-330", "gles-300", "mtl-20000"],
                "metalPlatforms": ["macOS", "iOS-device", "iOS-simulator"],
                "samplerModel": "texture-owned-combined",
                "vulkanCompiler": "axslcc HLSL normally; glslc normalized GLSL for texture-owned combined samplers",
                "sources": [record for record, _ in records]}
    if args.validation_report:
        args.validation_report.write_text(json.dumps({"gpuVerified": False, "archives": validations}, indent=2) + "\n")
    (destination / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    header = output_root / "extensions/scripting/js-bindings/manual/LegacyGLShaderManifest.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    lines = ['// Generated by tools/bindings-generator/js/legacy-gl/compile.py. Exact original source lookup.', '// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).', '#pragma once', '#include <string_view>', 'namespace axjs::legacygl {', 'struct ShaderArchive { std::string_view source; std::string_view path; bool vertex; std::string_view defines; };', 'inline constexpr ShaderArchive archives[] = {']
    for index, (record, source) in enumerate(records):
        if record["compiled"]:
            tag = f"AXGL{index}"
            lines.append(f'    {{R"{tag}({source}){tag}", "{record["archive"]}", {"true" if record["stage"] == "vert" else "false"}, "{record["defines"]}"}},')
    lines += ['};', '} // namespace axjs::legacygl', '']
    header.write_text("\n".join(lines))
    # Keep derived source constants separate from the frozen upstream table and
    # its provenance. Runtime lookup must use exactly the bytes compiled above.
    variant_header = output_root / "extensions/scripting/js-bindings/manual/LegacyGLBuiltinVariants.h"
    lines = ['// Generated by tools/bindings-generator/js/legacy-gl/compile.py from derived compatibility shaders.', '// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).',
             '#pragma once', '#include "manual/LegacyGLBuiltinSources.h"',
             'namespace axjs::legacygl {', 'inline constexpr BuiltinShaderSource builtinShaderVariants[]={']
    for index, name in enumerate(("position-texture-gray-no-mvp.frag", "position-texture-gray-alpha-no-mvp.frag")):
        source = (root / "tools/bindings-generator/js/legacy-gl/builtins" / name).read_text()
        tag = f"AXVARIANT{index}"
        lines.append(f'    {{"{name}",false,R"{tag}({source}){tag}"}},')
    lines += ['};', '} // namespace axjs::legacygl', '']
    variant_header.write_text("\n".join(lines))
    required = [record for record, _ in records if not record["compiled"] and "example_Twist.fsh" not in record["source"]]
    return 1 if required else 0


if __name__ == "__main__":
    raise SystemExit(main())
