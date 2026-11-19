# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Lower the legacy per-texture sampler contract into valid combined archives.

Modern named HLSL samplers require SamplerRegistry registration. Legacy GLSL
sampler2D/samplerCube parameters instead belong to each texture. RHI already
supports that contract through combined-image archives with no named samplers.
"""
import json
import re
import struct
from archive import read_archive
from bridge import run


def lower(path, input_path, original, glslc, spirv_cross):
    raw = path.read_bytes()
    archive = read_archive(raw)
    textures = original.get('textures', [])
    if not textures:
        if any(target['samplers'] for target in archive['targets']):
            raise ValueError('Named sampler without a legacy texture')
        return []
    # Compile the original normalized GLSL, not HLSL's separate texture/sampler
    # model, for Vulkan. This emits real OpTypeSampledImage descriptors in set1.
    combined_path = input_path.with_suffix('.combined.spv')
    run([glslc, '--target-env=vulkan1.0', '-O', '-fshader-stage=' + ('vert' if input_path.suffix == '.vert' else 'frag'),
         '-o', combined_path, input_path])
    combined_code = combined_path.read_bytes()
    reflected = json.loads(run([spirv_cross, combined_path, '--reflect']))
    if reflected.get('separate_images') or reflected.get('separate_samplers'):
        raise ValueError('Vulkan lowering did not produce combined sampled images')
    expected = {(x['set'], x['binding']): (x['type'], x.get('array', [])) for x in textures}
    actual = {(x['set'], x['binding']): (x['type'], x.get('array', [])) for x in reflected.get('textures', [])}
    if actual != expected or any(descriptor_set != 1 for descriptor_set, _ in actual):
        raise ValueError('Combined Vulkan texture binding/type/array identity changed')
    # Optimization may remove member names, but bytecode storage layout must
    # still match every source member of each active uniform buffer.
    def ubos(reflection):
        return {block['binding']: [(m['offset'], m['type'], m.get('array', []), m.get('array_stride'), m.get('matrix_stride'))
                for m in reflection['types'][block['type']]['members']] for block in reflection.get('ubos', [])}
    if ubos(reflected) != ubos(original):
        raise ValueError('Combined Vulkan std140 block layout changed')
    stages = []
    association = []
    for index, target in enumerate(archive['targets']):
        if len(target['samplers']) != len(target['textures']):
            raise ValueError('Legacy texture/sampler association is not one to one')
        by_name = {sampler['name']: sampler for sampler in target['samplers']}
        for texture in target['textures']:
            sampler_name = '_' + texture['name'] + '_sampler'
            sampler = by_name.get(sampler_name)
            if sampler is None or sampler['preset'] != -1 or sampler['space'] != 2 or sampler['arraySize'] != texture['arraySize']:
                raise ValueError('Unexpected static/shared legacy sampler association')
            expected_binding = texture['binding'] + (1024 if target['language'] == 4 else 0)
            if sampler['binding'] != expected_binding:
                raise ValueError('Legacy sampler is not associated with its own texture slot')
            if target['language'] == 2:
                code = target['code'].decode()
                if not re.search(r'\b' + re.escape(texture['name']) + r'\s*\[\[texture\(' + str(texture['binding']) + r'\)\]\]', code):
                    raise ValueError('Metal texture code/reflection binding mismatch')
                if not re.search(r'\b' + re.escape(sampler_name) + r'\s*\[\[sampler\(' + str(texture['binding']) + r'\)\]\]', code):
                    raise ValueError('Metal texture-owned sampler code/reflection binding mismatch')
                calls = re.findall(r'\b(\w+)\.(?:sample(?:_compare)?|gather(?:_compare)?)\s*\(\s*(\w+)', code)
                if not any(name == texture['name'] for name, _ in calls):
                    raise ValueError('Metal texture has no validated sampling operation')
                if any(name == texture['name'] and state != sampler_name for name, state in calls):
                    raise ValueError('Metal texture samples with another texture sampler')
                association.append(dict(texture=texture['name'], binding=texture['binding'], dimension=texture['dimension'], arraySize=texture['arraySize']))
            if target['language'] in (0, 3):
                if texture['samplerRef'] != (0x8000 | sampler['binding']):
                    raise ValueError('GL combined texture references the wrong generated sampler')
                if not re.search(r'\buniform\s+(?:(?:lowp|mediump|highp)\s+)?sampler\w+\s+' + re.escape(texture['name']) + r'\s*[;\[]', target['code'].decode()):
                    raise ValueError('GL code does not expose the original combined texture uniform')
        start = struct.unpack_from('<I', raw, 16 + index * 12 + 8)[0]
        code_size = struct.unpack_from('<I', raw, start + 16)[0]
        ref_start = start + 20 + code_size
        ref_size = struct.unpack_from('<I', raw, ref_start + 4)[0]
        reflection = bytearray(raw[ref_start + 8:ref_start + 8 + ref_size])
        inputs, texture_count, sampler_count, blocks, images, buffers = struct.unpack_from('<6I', reflection, 32)
        if images or buffers:
            raise ValueError('Storage resources are outside the legacy combined shader contract')
        position = 66 + inputs * 72
        for _ in range(blocks):
            members = struct.unpack_from('<H', reflection, position + 42)[0]
            position += 44 + 44 * members
        for _ in range(texture_count):
            struct.pack_into('<H', reflection, position + 42, 0xffff)
            position += 44
        if position + sampler_count * 44 != len(reflection):
            raise ValueError('Unexpected data after named sampler reflection')
        del reflection[position:]
        struct.pack_into('<I', reflection, 40, 0)
        code = combined_code if target['language'] == 4 else target['code']
        body = target['stage'].encode() + b'CODE' + struct.pack('<I', len(code)) + code + b'REFL' + struct.pack('<I', len(reflection)) + reflection
        stages.append((target['language'], target['profile'], b'STAG' + struct.pack('<I', len(body)) + body))
    result = bytearray(struct.pack('<4sI4H', b'AXSA', 0, *archive['version'], len(stages)))
    offset = 16 + 12 * len(stages)
    for language, profile, stage in stages:
        result += struct.pack('<III', language, profile, offset)
        offset += len(stage)
    for _, _, stage in stages:
        result += stage
    struct.pack_into('<I', result, 4, len(result))
    read_archive(result)
    path.write_bytes(result)
    return association
