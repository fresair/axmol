#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Build the legacy runtime's Python 2.7.18 tool into the repository cache only."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
from build import DEFAULT_CACHE, HERE, digest, run
from cache_lock import cache_lock

URL = "https://www.python.org/ftp/python/2.7.18/Python-2.7.18.tar.xz"
SHA256 = "b62c0e7937551d0cc02b8fd5cb0f544f9405bafc9a54d3808ed4594812edef43"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE, help="build/download cache (default: %(default)s)")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least one")
    cache = args.cache_dir.resolve()
    prefix = cache / "tools/python2"
    executable = prefix / "bin/python2.7"
    try:
        cache.mkdir(parents=True, exist_ok=True)
        with cache_lock(cache):
            if executable.exists():
                if run([str(executable), "-c", "import sys; print(sys.version.split()[0])"]) != "2.7.18":
                    raise RuntimeError("existing cache interpreter is not Python 2.7.18")
                print(executable)
                return 0
            archive = cache / "Python-2.7.18.tar.xz"
            if not archive.exists():
                if args.offline:
                    raise RuntimeError(f"offline archive missing: {archive}")
                temporary = archive.with_suffix(".download")
                command = ["curl", "--fail", "--location", "--retry", "3", "--connect-timeout", "30"]
                if os.environ.get("AX_JS_DOWNLOAD_NO_PROXY") == "1":
                    command += ["--noproxy", "*"]
                run(command + [URL, "--output", str(temporary)])
                if digest(temporary) != SHA256:
                    raise RuntimeError("Python 2.7.18 archive checksum mismatch")
                temporary.replace(archive)
            if digest(archive) != SHA256:
                raise RuntimeError("Python 2.7.18 archive checksum mismatch")
            source = cache / "tmp/Python-2.7.18"
            source.parent.mkdir(parents=True, exist_ok=True)
            if not source.exists():
                with tarfile.open(archive) as package:
                    for member in package:
                        if Path(member.name).is_absolute() or ".." in Path(member.name).parts:
                            raise RuntimeError("unsafe Python archive path")
                        if sys.version_info >= (3, 12):
                            package.extract(member, source.parent, filter="data")
                        else:
                            package.extract(member, source.parent)
            patch_stamp = source / ".axmol-toolchain-patched"
            if not patch_stamp.exists():
                for patch in sorted((HERE / "python-patches").glob("*.patch")):
                    run(["patch", "-p1", "--batch", "--forward", "-i", str(patch)], cwd=source)
                patch_stamp.touch()
            work = cache / "tmp/python2-build"
            work.mkdir(exist_ok=True)
            env = os.environ.copy()
            for key in ("CPPFLAGS", "LDFLAGS", "LIBS", "CC", "CXX"):
                env.pop(key, None)
            env.update(CC="clang", CFLAGS="-Wno-implicit-int -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-deprecated-declarations")
            # Optional SSL/readline extensions are unnecessary for mozbuild. No pip
            # packages or system/user Python installations are created or modified.
            run([str(source / "configure"), "--prefix=" + str(prefix), "--without-ensurepip", "--enable-unicode=ucs4"], cwd=work, env=env, log=work / "configure.log")
            run(["make", "-j", str(args.jobs)], cwd=work, env=env, log=work / "build.log")
            run(["make", "install"], cwd=work, env=env, log=work / "install.log")
            version = subprocess.check_output([str(executable), "-c", "import sys; print(sys.version.split()[0])"], text=True).strip()
            if version != "2.7.18":
                raise RuntimeError(f"unexpected built interpreter version: {version}")
            (prefix / "axmol-toolchain.json").write_text(json.dumps({"version": version, "source_url": URL, "source_sha256": SHA256}, indent=2) + "\n")
            print(executable)
            return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Isolated Python 2 build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
