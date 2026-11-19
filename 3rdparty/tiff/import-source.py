#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Import the pinned, unmodified LibTIFF library and CMake sources."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile

VERSION = "4.7.2"
SHA256 = "672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a"
URL = f"https://download.osgeo.org/libtiff/tiff-{VERSION}.tar.gz"
ROOT_FILES = {
    "CMakeLists.txt", "configure.ac", "VERSION", "RELEASE-DATE", "LICENSE.md",
    "README.md", "placeholder.h", "libtiff-4.pc.in",
}
DIRECTORIES = {"libtiff", "cmake", "port", "build"}
# Upstream's CMake build reads configure.ac for its version and still includes
# build/CMakeLists.txt. Keep those inputs, but omit the independent Autotools
# build, documentation generators (tiff-docs is forced OFF), and Automake's
# unused port dummy. Keep *.h.cmake.in: they generate the actual CMake headers.
EXCLUDED_FILES = {
    "build/README", "port/dummy.c",
    "cmake/Sphinx.cmake", "cmake/SphinxCheckInternalLinks.cmake",
    "cmake/list-manpages.py", "cmake/list-manpage-dependencies.py",
    "build/Makefile.am", "build/Makefile.in",
    "libtiff/Makefile.am", "libtiff/Makefile.in",
    "port/Makefile.am", "port/Makefile.in",
    "libtiff/tif_config.h.in", "libtiff/tiffconf.h.in",
    "libtiff/tiffvers.h.in", "port/libport_config.h.in",
}
SELECTION = (
    "Unmodified library, CMake modules, portability sources, and CMake build "
    "metadata; no upstream tools/tests/docs, Autotools-only Makefiles/header "
    "templates, documentation generators, or unused Automake port dummy."
)


def selected(relative):
    return (
        (relative.parts[0] in DIRECTORIES or str(relative) in ROOT_FILES)
        and str(relative) not in EXCLUDED_FILES
    )


def checked_path(root, relative):
    """Reject links at every destination component before reading or mutating."""
    relative = PurePosixPath(relative)
    if not relative.parts or relative.is_absolute() or ".." in relative.parts:
        raise SystemExit(f"Unsafe vendored path: {relative}")
    path = root
    for part in ("", *relative.parts):
        path = path / part
        if path.is_symlink():
            raise SystemExit(f"Symbolic link in vendored path: {path}")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, nargs="?")
    parser.add_argument("--verify", action="store_true", help="Verify vendored files against the pinned manifest")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    manifest_path = checked_path(directory, "manifest.json")
    if args.verify:
        manifest = json.loads(manifest_path.read_text())
        if manifest["archive_sha256"] != SHA256 or manifest["version"] != VERSION:
            raise SystemExit("Unexpected LibTIFF pin in manifest")
        actual = {str(path.relative_to(directory / "upstream"))
                  for path in (directory / "upstream").rglob("*") if path.is_file()}
        if actual != set(manifest["files"]):
            raise SystemExit("Vendored LibTIFF file set differs from manifest")
        for relative, expected in manifest["files"].items():
            if not selected(PurePosixPath(relative)):
                raise SystemExit(f"Excluded upstream source in manifest: {relative}")
            if hashlib.sha256(checked_path(directory, "upstream/" + relative).read_bytes()).hexdigest() != expected:
                raise SystemExit(f"Modified upstream source: {relative}")
        print(f"Verified {len(manifest['files'])} unmodified LibTIFF {VERSION} files")
        return
    if args.archive is None:
        parser.error("archive is required unless --verify is used")
    archive = args.archive.read_bytes()
    if hashlib.sha256(archive).hexdigest() != SHA256:
        raise SystemExit("LibTIFF archive SHA256 mismatch")
    files, contents, excluded = {}, {}, {}
    with tarfile.open(args.archive, "r:gz") as source:
        for member in source.getmembers():
            parts = PurePosixPath(member.name).parts
            if not parts or parts[0] != f"tiff-{VERSION}":
                raise SystemExit("Unexpected archive root")
            relative = PurePosixPath(*parts[1:])
            if ".." in relative.parts or relative.is_absolute():
                raise SystemExit("Unsafe archive member")
            if not member.isfile() or not relative.parts:
                continue
            if not selected(relative) and str(relative) not in EXCLUDED_FILES:
                continue
            data = source.extractfile(member).read()
            if str(relative) in EXCLUDED_FILES:
                excluded[str(relative)] = hashlib.sha256(data).hexdigest()
            else:
                contents[str(relative)] = data
                files[str(relative)] = hashlib.sha256(data).hexdigest()
    # Never use a previous manifest as a deletion list. Only the narrow list
    # above, with bytes authenticated by the fixed official archive, may be
    # removed. Preflight all destinations before any write or deletion.
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        if previous["archive_sha256"] != SHA256 or previous["version"] != VERSION:
            raise SystemExit("Unexpected LibTIFF pin in previous manifest")
        if previous["files"].keys() - (files.keys() | excluded.keys()):
            raise SystemExit("Unexpected path in previous LibTIFF manifest")
    destinations = {relative: checked_path(directory, "upstream/" + relative)
                    for relative in files.keys() | excluded.keys()}
    stale_files = []
    for relative, expected in excluded.items():
        stale = destinations[relative]
        if stale.exists():
            if not stale.is_file() or hashlib.sha256(stale.read_bytes()).hexdigest() != expected:
                raise SystemExit(f"Modified stale upstream source: {relative}")
            stale_files.append(stale)
    for relative, data in contents.items():
        destination = destinations[relative]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    for stale in stale_files:
        stale.unlink()
    manifest = {
        "name": "LibTIFF", "version": VERSION, "source_url": URL,
        "archive_sha256": SHA256, "license": "LibTIFF",
        "license_file": "upstream/LICENSE.md", "patches": [],
        "selection": SELECTION,
        "files": dict(sorted(files.items())),
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Imported {len(files)} original files from LibTIFF {VERSION}")


if __name__ == "__main__":
    main()
