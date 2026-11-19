# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Maintainer-only GLSL -> SPIR-V -> HLSL bridge for axslcc 4.x."""
import json
import re
import subprocess
import struct
from archive import read_archive

SPIRV_CROSS_TAG = 'vulkan-sdk-1.4.350.0'
SPIRV_CROSS_COMMIT = '1a6169566c73d3da552748fc372fe2bbb856e46e'
SEMANTICS = {'a_position': 'POSITION', 'aVertexPosition': 'POSITION',
             'a_color': 'COLOR0', 'aVertexColor': 'COLOR0', 'a_texCoord': 'TEXCOORD0',
             'a_normal': 'NORMAL', 'a_blendWeight': 'BLENDWEIGHT',
             'a_blendIndex': 'BLENDINDICES', 'a_tangent': 'TANGENT'}


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if result.returncode:
        raise ValueError(result.stdout + result.stderr)
    return result.stdout


def translate(input_path, hlsl_path, attributes, glslc, spirv_cross):
    spv_path = input_path.with_suffix('.spv')
    run([glslc, '--target-env=vulkan1.0', '-fshader-stage=' + ('vert' if input_path.suffix == '.vert' else 'frag'),
         '-o', spv_path, input_path])
    reflection = json.loads(run([spirv_cross, spv_path, '--reflect']))
    command = [spirv_cross, spv_path, '--hlsl', '--shader-model', '50', '--output', hlsl_path]
    for attribute in attributes:
        semantic = SEMANTICS[attribute['name']]
        command += ['--set-hlsl-vertex-input-semantic', attribute['location'], semantic]
    run(command)
    source = hlsl_path.read_text()
    # SPIRV-Cross prefixes members of an anonymous GLSL UBO with its SPIR-V id.
    # Restore the original reflected names used by native/JS uniform lookup.
    for ubo in reflection.get('ubos', []):
        for member in reflection['types'][ubo['type']]['members']:
            source = re.sub(r'\b_\d+_' + re.escape(member['name'].lstrip('_')) + r'\b', member['name'], source)
    # DXC otherwise compacts locations independently in each stage. Annotate
    # every user interface member, including sparse varying locations, so the
    # original normalized shader ABI survives separate-stage optimization.
    for structure, entries in (('SPIRV_Cross_Input', reflection.get('inputs', [])),
                               ('SPIRV_Cross_Output', reflection.get('outputs', []))):
        locations = {entry['name']: entry['location'] for entry in entries}
        def interface(match):
            body = match[2]
            for name, location in locations.items():
                pattern = r'(^[ \t]*)([^;\n]+\b' + re.escape(name) + r'(?:\[\d+\])?\s*:\s*\w+\s*;)'
                body = re.sub(pattern, lambda field: field[1] + f'[[vk::location({location})]] ' + field[2], body, flags=re.M)
            return match[1] + body + match[3]
        source = re.sub(r'(struct ' + structure + r'\s*\{)(.*?)(\};)', interface, source, flags=re.S)
    # v4 assigns resource registers and custom sampler space 2 itself. Explicit
    # SPIRV-Cross registers are rejected; packoffset remains the GLSL std140 ABI.
    source = re.sub(r'\s*:\s*register\([^)]*\)', '', source)
    hlsl_path.write_text(source)
    return reflection, source


def restore_attribute_names(path, attributes):
    data = bytearray(path.read_bytes())
    by_semantic = {}
    for attribute in attributes:
        semantic = SEMANTICS[attribute['name']]
        match = re.fullmatch(r'([A-Z]+)(\d*)', semantic)
        by_semantic[(match[1], int(match[2] or '0'))] = attribute
    archive = read_archive(data)
    for i, target in enumerate(archive['targets']):
        start = struct.unpack_from('<I', data, 16 + i * 12 + 8)[0]
        code_size = struct.unpack_from('<I', data, start + 16)[0]
        pos = start + 20 + code_size + 8 + 66
        for descriptor in target['inputs']:
            attribute = by_semantic[(descriptor['semantic'], descriptor['semanticIndex'])]
            if descriptor['location'] != attribute['location']:
                raise ValueError(f'Attribute location changed: {attribute} -> {descriptor}')
            data[pos:pos + 32] = attribute['name'].encode().ljust(32, b'\0')
            pos += 72
    path.write_bytes(data)


def validate(path, stage, attributes, reflection):
    archive = read_archive(path.read_bytes())
    if len(archive['targets']) != 4 or {(x['language'], x['profile']) for x in archive['targets']} != {(3, 330), (0, 300), (4, 0x80000064), (2, 20000)}:
        raise ValueError('Missing or unexpected GL/GLES/Vulkan/Metal targets')
    uniform_offsets = {}
    uniform_types = {}
    source_types = {'vec4': 0, 'vec3': 1, 'vec2': 2, 'float': 3, 'ivec4': 4, 'ivec3': 5, 'ivec2': 6, 'int': 7, 'mat4': 11, 'mat3': 12}
    for ubo in reflection.get('ubos', []):
        for member in reflection['types'][ubo['type']]['members']:
            uniform_offsets[member['name']] = member['offset']
            count = 1
            for width in member.get('array', []):
                count *= width
            uniform_types[member['name']] = (source_types[member['type']], count)
    declared_textures = {x['name'] for x in reflection.get('textures', [])}
    attribute_names = {x['name']: x for x in attributes}
    summaries = []
    for target in archive['targets']:
        if target['samplers'] or any(texture['samplerRef'] != 0xffff for texture in target['textures']):
            raise ValueError('Legacy textures must use their own dynamic combined samplers')
        if target['stage'] != ('VERT' if stage == 'vert' else 'FRAG'):
            raise ValueError('Wrong shader stage')
        for attribute in target['inputs']:
            expected = attribute_names[attribute['name']]
            if attribute['location'] != expected['location'] or not attribute['semantic']:
                raise ValueError('Attribute identity/location mismatch')
        offsets = {member['name']: member['offset'] for ubo in target['uniformBuffers'] for member in ubo['members']}
        for name, offset in offsets.items():
            if name not in uniform_offsets or uniform_offsets[name] != offset:
                raise ValueError(f'Uniform name/std140 offset changed: {name}={offset}, expected={uniform_offsets.get(name)}')
        for ubo in target['uniformBuffers']:
            for member in ubo['members']:
                if (member['type'], member['arraySize']) != uniform_types[member['name']]:
                    raise ValueError('Uniform type/array width changed: ' + member['name'])
        if target['uniformBuffers'] and offsets != uniform_offsets:
            raise ValueError('Uniform members lost from an active UBO')
        textures = {x['name'] for x in target['textures']}
        if not textures <= declared_textures:
            raise ValueError('Texture names changed')
        summaries.append({k: v for k, v in target.items() if k != 'code'})
    # DXC can eliminate wholly unused attributes/resources. Keep those facts
    # explicit rather than claiming the declarations survived optimization.
    return dict(version=archive['version'], targets=summaries,
                inactiveUniforms=sorted(set(uniform_offsets) - set(offsets)),
                inactiveTextures=sorted(declared_textures - textures),
                inactiveAttributes=sorted(set(attribute_names) - {x['name'] for x in target['inputs']}))


def validate_spirv_interfaces(path, original, spirv_cross, temporary):
    archive = read_archive(path.read_bytes())
    spirv = next(target['code'] for target in archive['targets'] if target['language'] == 4)
    spv_path = temporary / 'final.spv'
    spv_path.write_bytes(spirv)
    final = json.loads(run([spirv_cross, spv_path, '--reflect']))

    def slots(entries):
        result = {}
        for entry in entries:
            kind = entry['type']
            columns = int(kind[3]) if kind.startswith('mat') else 1
            element = 'vec' + str(columns) if columns > 1 else kind
            count = columns
            for width in entry.get('array', []):
                count *= width
            for index in range(count):
                location = entry['location'] + index
                if location in result:
                    raise ValueError('Overlapping SPIR-V interface locations')
                result[location] = element
        return result

    report = {}
    for direction in ('inputs', 'outputs'):
        expected = slots(original.get(direction, []))
        actual = slots(final.get(direction, []))
        for location, kind in actual.items():
            if expected.get(location) != kind:
                raise ValueError(f'SPIR-V {direction} location/type changed: {location}={kind}, expected={expected.get(location)}')
        report[direction] = actual
        report['inactive' + direction.title()] = sorted(set(expected) - set(actual))
    return report
