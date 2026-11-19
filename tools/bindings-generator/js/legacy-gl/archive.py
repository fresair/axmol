# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Strict reader for the Axmol axslc-spec.h 4.x archive wire format."""
import re
import struct


def read_archive(data, major=4):
    def unpack(fmt, pos, limit=None):
        size = struct.calcsize(fmt)
        if pos < 0 or pos + size > (len(data) if limit is None else min(len(data), limit)):
            raise ValueError('Truncated shader archive')
        return struct.unpack_from(fmt, data, pos)

    def name(pos):
        raw = data[pos:pos + 32]
        if len(raw) != 32 or b'\0' not in raw:
            raise ValueError('Invalid reflection name')
        return raw.split(b'\0', 1)[0].decode('utf-8')

    magic, size, found, minor, patch, count = unpack('<4sI4H', 0)
    if magic != b'AXSA' or size != len(data) or found != major or not count:
        raise ValueError(f'Invalid AXSA {major} archive header: {magic!r}, size={size}, version={found}.{minor}.{patch}, targets={count}')
    table_end = 16 + 12 * count
    targets = []
    next_start = table_end
    for index in range(count):
        language, profile, start = unpack('<III', 16 + 12 * index, table_end)
        if start != next_start:
            raise ValueError('Non-contiguous archive stage table')
        chunk, stage_size, stage, code_chunk, code_size = unpack('<4sI4s4sI', start)
        if chunk != b'STAG' or code_chunk != b'CODE':
            raise ValueError('Invalid stage/code chunk')
        end = start + 8 + stage_size
        reflection = start + 20 + code_size
        ref_chunk, ref_size = unpack('<4sI', reflection, end)
        if ref_chunk != b'REFL' or reflection + 8 + ref_size != end:
            raise ValueError('Invalid reflection chunk')
        header = reflection + 8
        inputs, textures, samplers, ubos, images, buffers = unpack('<6I', header + 32, end)
        pos = header + 66
        target = dict(language=language, profile=profile, stage=stage.decode(), inputs=[], uniformBuffers=[], textures=[], samplers=[])
        for _ in range(inputs):
            location, semantic_index, kind = unpack('<iHH', pos + 64, end)
            target['inputs'].append(dict(name=name(pos), semantic=name(pos + 32), location=location, semanticIndex=semantic_index, type=kind))
            pos += 72
        for _ in range(ubos):
            binding, byte_size, array_size, members = unpack('<iIHH', pos + 32, end)
            ubo = dict(name=name(pos), binding=binding, size=byte_size, arraySize=array_size, members=[])
            pos += 44
            for _ in range(members):
                offset, member_size, array_size, kind = unpack('<iIHH', pos + 32, end)
                ubo['members'].append(dict(name=name(pos), offset=offset, size=member_size, arraySize=array_size, type=kind))
                pos += 44
            target['uniformBuffers'].append(ubo)
        for _ in range(textures):
            binding, descriptor_set, dim, flags, array_size, sampler_ref = unpack('<iHBBHH', pos + 32, end)
            target['textures'].append(dict(name=name(pos), binding=binding, descriptorSet=descriptor_set, dimension=dim, flags=flags, arraySize=array_size, samplerRef=sampler_ref))
            pos += 44
        for _ in range(samplers):
            binding, space, array_size, preset, flags, reserved = unpack('<iHHhBB', pos + 32, end)
            target['samplers'].append(dict(name=name(pos), binding=binding, space=space, arraySize=array_size, preset=preset, flags=flags))
            if reserved:
                raise ValueError('Nonzero reserved sampler field')
            pos += 44
        pos += images * 44 + buffers * 48
        if pos != end:
            raise ValueError(f'Reflection size mismatch: parsed={pos}, end={end}')
        code = data[start + 20:reflection]
        target['code'] = code
        if language == 2:
            msl = code.decode().rstrip('\0')
            if not re.search(r'\b(?:vertex|fragment)\s+\w+\s+main0\s*\(', msl):
                raise ValueError('Metal entry must be main0')
            target['metalAttributes'] = sorted(map(int, re.findall(r'\[\[attribute\((\d+)\)\]\]', msl)))
            if stage == b'VERT' and target['metalAttributes'] != sorted(x['location'] for x in target['inputs']):
                raise ValueError('Metal attributes disagree with reflection')
        targets.append(target)
        next_start = end
    if next_start != len(data):
        raise ValueError('Trailing archive bytes')
    return dict(version=[found, minor, patch], targets=targets)
