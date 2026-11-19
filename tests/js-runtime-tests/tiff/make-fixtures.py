#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Extract original embedded TIFFs and make an independent raw-pixel oracle.

No TIFF decoder is used here: the original particle TIFFs contain uncompressed
8-bit RGBA strips. LibTIFF and native Image are tested against those stored
samples, with the TIFF associated-alpha rounding applied explicitly.
"""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import plistlib
import struct


def digest(data):
    return hashlib.sha256(data).hexdigest()


def original_pixels(data):
    endian = {b"II": "<", b"MM": ">"}[data[:2]]
    unpack = lambda code, offset: struct.unpack_from(endian + code, data, offset)[0]
    if unpack("H", 2) != 42:
        raise ValueError("The raw oracle expects classic TIFF")
    directory = unpack("I", 4)
    tags = {}
    for i in range(unpack("H", directory)):
        entry = directory + 2 + 12 * i
        tag, kind, count = struct.unpack_from(endian + "HHI", data, entry)
        if kind not in (3, 4):
            continue
        code, size = ("H", 2) if kind == 3 else ("I", 4)
        offset = entry + 8 if count * size <= 4 else unpack("I", entry + 8)
        tags[tag] = [unpack(code, offset + size * j) for j in range(count)]
    width, height = tags[256][0], tags[257][0]
    if (tags[258] != [8, 8, 8, 8] or tags[259] != [1] or tags[262] != [2]
            or tags[277] != [4] or tags.get(274, [1]) != [1]
            or tags.get(284, [1]) != [1] or tags[338] != [2]):
        raise ValueError("Original TIFF no longer has the expected uncompressed RGBA layout")
    if len(tags[273]) != len(tags[279]):
        raise ValueError("TIFF strip offsets and lengths do not match")
    pixels = bytearray(b"".join(data[offset:offset + count]
                               for offset, count in zip(tags[273], tags[279])))
    if len(pixels) != width * height * 4:
        raise ValueError("Unexpected original TIFF strip size")
    for i in range(0, len(pixels), 4):
        alpha = pixels[i + 3]
        for component in range(3):
            pixels[i + component] = (pixels[i + component] * alpha + 127) // 255
    return width, height, bytes(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resources", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    resources = args.resources
    args.output.mkdir(parents=True, exist_ok=True)
    manifest, rows = [], []
    for path in sorted(resources.rglob("*.plist")):
        source = path.read_bytes()
        try:
            encoded = plistlib.loads(source).get("textureImageData")
        except (ValueError, TypeError, plistlib.InvalidFileException):
            continue
        if not encoded:
            continue
        packed = base64.b64decode(encoded)
        data = gzip.decompress(packed) if packed.startswith(b"\x1f\x8b") else packed
        if data[:4] not in (b"MM\x00*", b"II*\x00"):
            continue
        width, height, pixels = original_pixels(data)
        stem = f"original-{len(manifest):02d}"
        for suffix, contents in ((".input", packed), (".tiff", data), (".rgba", pixels)):
            (args.output / (stem + suffix)).write_bytes(contents)
        relative = path.relative_to(resources).as_posix()
        rows.append(f"{relative}\t{width}\t{height}\t{stem}\n")
        manifest.append({"original_resource": relative, "original_plist_sha256": digest(source),
                         "packed_sha256": digest(packed), "tiff_sha256": digest(data),
                         "expected_rgba_sha256": digest(pixels), "width": width, "height": height})
    if len(manifest) != 13:
        raise SystemExit(f"Expected all 13 original embedded TIFF plists, found {len(manifest)}")
    (args.output / "originals.tsv").write_text("".join(rows))
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared {len(manifest)} original TIFF particle resources and independent RGBA oracles")


if __name__ == "__main__":
    main()
