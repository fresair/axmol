#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Prepare the default pinned runtime package during ordinary CMake configure.

Only absent or empty outputs are automatically built. A nonempty package is
checked and reused, or rejected without modifying its contents. Explicit custom
packages are validated by CMake itself, without invoking this preparation path.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

import build
from cache_lock import cache_lock, output_lock


def bootstrap(argv: list[str]) -> int:
    spec = importlib.util.spec_from_file_location("axmol_spidermonkey_python2", build.HERE / "bootstrap-python2.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.main(argv)


def check_existing(output: Path, args: argparse.Namespace) -> bool:
    """Return false only for absent/empty paths, never repair an existing package."""
    if not output.exists():
        return False
    if not output.is_dir():
        raise RuntimeError(f"runtime output is not a directory: {output}")
    if not any(output.iterdir()):
        return False
    try:
        manifest = json.loads((output / "manifest.json").read_text())
        expected = {
            "schema_version": 1, "runtime": "spidermonkey", "version": "33.1.1",
            "source_commit": build.SOURCE_COMMIT, "source_archive_sha256": build.SOURCE_SHA256,
            "platform": args.platform, "architecture": args.arch,
            "build_standard": "c++11", "consumer_standard": "c++23",
            "library": "lib/libjs_static.a",
            "gc": {"exact_rooting": True, "generational": True, "incremental": True},
            "jit": args.platform in ("macos", "android") and args.arch == "x86_64",
            "cxx_abi": "c++_shared" if args.platform == "android" else "libc++",
            "compile_definitions": build.PUBLIC_DEFINES + (["ANDROID=1"] if args.platform == "android" else ["IPHONEOS=1"] if args.platform.startswith("ios") else []),
        }
        if args.platform == "android":
            expected.update(ndk="27.3.13750724", minimum_system_version="23")
        import hashlib
        patches = hashlib.sha256()
        for patch in sorted(build.PATCHES.glob("*.patch")):
            patches.update(patch.name.encode())
            patches.update(patch.read_bytes())
        expected["patch_sha256"] = patches.hexdigest()
        for key, value in expected.items():
            if manifest[key] != value:
                raise ValueError(f"manifest field {key!r} does not match the requested runtime")
        if args.platform != "android":
            requested_minimum = args.deployment_target or ("11.0" if args.platform == "macos" else "15.0")
            def version(value: str) -> tuple[int, ...]:
                if not isinstance(value, str) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,3}", value):
                    raise ValueError(f"invalid minimum system version: {value!r}")
                parts = tuple(int(part) for part in value.split("."))
                return parts + (0,) * (4 - len(parts))
            if version(manifest["minimum_system_version"]) > version(requested_minimum):
                raise ValueError(f"package minimum {manifest['minimum_system_version']} exceeds requested deployment target {requested_minimum}")
        for key, relative in (("library_sha256", "lib/libjs_static.a"), ("config_header_sha256", "include/js-config.h")):
            if build.digest(output / relative) != manifest[key]:
                raise ValueError(f"checksum mismatch for {relative}")
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise RuntimeError(f"existing runtime package is incomplete or invalid: {output}: {error}; refusing to overwrite it. Select another cache/output or repair it explicitly.") from error
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("macos", "ios", "ios-simulator", "android"))
    parser.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    parser.add_argument("--cache-dir", type=Path, default=build.DEFAULT_CACHE)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--deployment-target")
    parser.add_argument("--sysroot", help="actual Apple SDK directory selected by CMake/Xcode")
    parser.add_argument("--ndk")
    parser.add_argument("--jobs", type=int, default=min(build.os.cpu_count() or 2, 8))
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least one")
    if args.platform == "ios" and args.arch != "arm64":
        parser.error("iOS devices require arm64")
    cache = args.cache_dir.resolve()
    output = (args.output or cache / "packages" / f"{args.platform}-{args.arch}").resolve()
    try:
        with cache_lock(cache), output_lock(output):
            if check_existing(output, args):
                print(f"AX_JS_SPIDERMONKEY_ROOT={output}")
                return 0
            # An existing isolated interpreter with a wrong version or a broken
            # executable is an error, not permission to replace that tool.
            if not (cache / "tools/python2/bin/python2.7").exists():
                try:
                    build.python2_path(None, cache)
                except RuntimeError:
                    options = ["--cache-dir", str(cache), "--jobs", str(args.jobs)]
                    if args.offline:
                        options.append("--offline")
                    if bootstrap(options):
                        return 1
            options = ["--platform", args.platform, "--arch", args.arch, "--cache-dir", str(cache),
                       "--output", str(output), "--jobs", str(args.jobs)]
            for name in ("deployment_target", "sysroot", "ndk"):
                if getattr(args, name):
                    options += ["--" + name.replace("_", "-"), getattr(args, name)]
            if args.offline:
                options.append("--offline")
            return build.main(options)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"SpiderMonkey preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
