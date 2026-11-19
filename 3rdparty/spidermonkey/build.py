#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Build the pinned SpiderMonkey 33.1 runtime without changing global tools.

Example: python3 3rdparty/spidermonkey/build.py --platform macos --arch arm64
The returned package directory is AX_JS_SPIDERMONKEY_ROOT, not the source tree.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import sys
import tarfile
from cache_lock import cache_lock, output_lock

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFAULT_CACHE = REPO / "cache/js-runtime"
PATCHES = HERE / "patches"
SOURCE_COMMIT = "1d8c1d7eb188db095b5a7ce838ba0a933a0d414b"
SOURCE_URL = f"https://codeload.github.com/cocos2d/Spidermonkey/tar.gz/{SOURCE_COMMIT}"
SOURCE_SHA256 = "7d0d87f5cafe4a9d547eaac2a03b9f01c34bd5c7d93416110719b3b330465764"
PUBLIC_DEFINES = ["STATIC_JS_API=1", "JSGC_INCREMENTAL=1", "JSGC_GENERATIONAL=1", "JSGC_USE_EXACT_ROOTING=1"]


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(command: list[str], *, cwd: Path | None = None, env: dict | None = None, log: Path | None = None) -> str:
    print("+ " + shlex.join(list(map(str, command))), flush=True)
    if log:
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            print("\n".join(log.read_text(errors="replace").splitlines()[-60:]), file=sys.stderr)
            raise RuntimeError(f"command failed ({result.returncode}); full output: {log}")
        return ""
    return subprocess.check_output(command, cwd=cwd, env=env, text=True).strip()


def python2_path(requested: str | None, cache: Path) -> str:
    isolated = cache / "tools/python2/bin/python2.7"
    candidates = [requested] if requested else [str(isolated) if isolated.is_file() else None, shutil.which("python2.7"), shutil.which("python2")]
    for candidate in filter(None, candidates):
        try:
            version = run([str(candidate), "-c", "import sys; print('%d.%d.%d' % sys.version_info[:3])"])
            if version == "2.7.18":
                return run([str(candidate), "-c", "import sys; print(sys.executable)"])
        except (OSError, subprocess.CalledProcessError):
            pass
    raise RuntimeError("SpiderMonkey's pinned build system requires Python 2.7.18. Supply an existing isolated interpreter with --python2 PATH; or run python3 3rdparty/spidermonkey/bootstrap-python2.py to build it into the repository cache; no global installation is performed.")


def prepare_source(cache: Path, offline: bool) -> tuple[Path, str]:
    archive = cache / "spidermonkey33.tar.gz"
    if not archive.is_file():
        if offline:
            raise RuntimeError(f"offline archive is missing: {archive}")
        temporary = archive.with_suffix(".download")
        # Respect the caller's network setup. --no-proxy is available for local
        # shells whose configured proxy is inaccessible inside a sandbox.
        command = ["curl", "--fail", "--location", "--retry", "3", "--connect-timeout", "30"]
        if os.environ.get("AX_JS_DOWNLOAD_NO_PROXY") == "1":
            command += ["--noproxy", "*"]
        run(command + [SOURCE_URL, "--output", str(temporary)])
        if digest(temporary) != SOURCE_SHA256:
            raise RuntimeError("SpiderMonkey source archive checksum mismatch")
        temporary.replace(archive)
    if digest(archive) != SOURCE_SHA256:
        raise RuntimeError(f"SpiderMonkey source archive checksum mismatch: {archive}")
    patch_hash = hashlib.sha256()
    patch_files = sorted(PATCHES.glob("*.patch"))
    for patch in patch_files:
        patch_hash.update(patch.name.encode())
        patch_hash.update(patch.read_bytes())
    patch_id = patch_hash.hexdigest()
    source = cache / "tmp" / ("source-" + patch_id[:12])
    source.parent.mkdir(parents=True, exist_ok=True)
    stamp = source / ".axmol-source.json"
    expected = {"source_commit": SOURCE_COMMIT, "patch_sha256": patch_id}
    if stamp.exists() and json.loads(stamp.read_text()) == expected:
        return source, patch_id
    if source.exists():
        raise RuntimeError(f"incomplete source extraction: remove only this cache directory and retry: {source}")
    source.mkdir()
    with tarfile.open(archive) as package:
        prefix = package.getnames()[0] + "/"
        for member in package:
            if not member.name.startswith(prefix):
                continue
            relative = member.name[len(prefix):]
            if not relative:
                continue
            if ".." in Path(relative).parts or Path(relative).is_absolute():
                raise RuntimeError("unsafe source archive path")
            # This upstream archive uses no symlinks outside its own tree.
            if member.issym() or member.islnk():
                if Path(member.linkname).is_absolute() or ".." in Path(member.linkname).parts:
                    raise RuntimeError("unsafe source archive link")
            member.name = relative
            if sys.version_info >= (3, 12):
                package.extract(member, source, filter="fully_trusted")
            else:
                package.extract(member, source)
    for patch in patch_files:
        run(["patch", "-p1", "--batch", "--forward", "-i", str(patch)], cwd=source)
    stamp.write_text(json.dumps(expected, indent=2) + "\n")
    return source, patch_id


def environment(args: argparse.Namespace) -> tuple[dict, list[str], dict]:
    env = os.environ.copy()
    for name in ("CPPFLAGS", "CFLAGS", "CXXFLAGS", "LDFLAGS", "LIBS", "CC", "CXX", "CPP", "CXXCPP", "AR", "RANLIB", "AS", "LD", "STRIP", "OBJCOPY"):
        env.pop(name, None)
    env.update(PYTHON=python2_path(args.python2, args.cache_dir.resolve()), CPPFLAGS="", LDFLAGS="",
               HOST_CC="clang", HOST_CXX="clang++", HOST_CFLAGS="-Wno-implicit-int", HOST_CXXFLAGS="-std=c++11", HOST_LDFLAGS=" ")
    cflags = ["-fPIC", "-Wno-implicit-int"]
    cxxflags = ["-std=c++11", "-fPIC", "-Wno-register", "-Wno-deprecated-builtins"]
    host_arch = "aarch64" if platform.machine() in ("arm64", "aarch64") else "x86_64"
    host = f"{host_arch}-apple-darwin" if sys.platform == "darwin" else f"{host_arch}-unknown-linux-gnu"
    cpu = "aarch64" if args.arch == "arm64" else "x86_64"
    target = f"{cpu}-apple-darwin"
    env["CC"] = shutil.which("clang") or "clang"
    env["CXX"] = shutil.which("clang++") or "clang++"
    metadata = {"platform": args.platform, "architecture": args.arch, "jit": args.platform in ("macos", "android") and args.arch == "x86_64"}
    if args.platform in ("macos", "ios", "ios-simulator"):
        if sys.platform != "darwin":
            raise RuntimeError("Apple packages must be built with Xcode on macOS")
        sdk = {"macos": "macosx", "ios": "iphoneos", "ios-simulator": "iphonesimulator"}[args.platform]
        sdk_path = str(Path(args.sysroot).resolve()) if args.sysroot else run(["xcrun", "--sdk", sdk, "--show-sdk-path"])
        if not Path(sdk_path).is_dir():
            raise RuntimeError(f"Apple SDK directory is missing: {sdk_path}")
        for key, tool in (("AR", "ar"), ("RANLIB", "ranlib"), ("STRIP", "strip")):
            env[key] = run(["xcrun", "--sdk", sdk_path, "--find", tool])
        minimum = args.deployment_target or ("11.0" if args.platform == "macos" else "15.0")
        triple = f"{args.arch}-apple-macos{minimum}" if args.platform == "macos" else f"{args.arch}-apple-ios{minimum}" + ("-simulator" if args.platform == "ios-simulator" else "")
        flags = ["-target", triple, "-isysroot", sdk_path]
        cflags += flags
        cxxflags += flags
        env["LDFLAGS"] = shlex.join(flags)
        if args.platform != "macos":
            cflags += ["-DIPHONEOS=1"]
            cxxflags += ["-DIPHONEOS=1"]
            env["CROSS_COMPILE"] = "1"
            metadata["compile_definitions"] = ["IPHONEOS=1"]
        metadata.update(minimum_system_version=minimum, sdk=run(["xcrun", "--sdk", sdk_path, "--show-sdk-version"]), sysroot=sdk_path, cxx_abi="libc++")
    else:
        if not args.ndk:
            raise RuntimeError("Android needs --ndk pointing at NDK r27d (27.3.13750724)")
        ndk = Path(args.ndk).resolve()
        properties = ndk / "source.properties"
        if not properties.is_file() or "27.3.13750724" not in properties.read_text():
            raise RuntimeError("Android runtime must use NDK r27d (27.3.13750724) to match Axmol")
        host_tag = "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64"
        toolchain = ndk / "toolchains/llvm/prebuilt" / host_tag
        target = f"{cpu}-linux-android"
        compiler_prefix = toolchain / "bin" / f"{target}23"
        env.update(CC=str(compiler_prefix) + "-clang", CXX=str(compiler_prefix) + "-clang++",
                   AR=str(toolchain / "bin/llvm-ar"), RANLIB=str(toolchain / "bin/llvm-ranlib"),
                   STRIP=str(toolchain / "bin/llvm-strip"), OBJCOPY=str(toolchain / "bin/llvm-objcopy"),
                   LD=str(toolchain / "bin/ld.lld"), AX_SM_ANDROID_NDK=str(ndk),
                   AX_SM_ANDROID_TOOLCHAIN=str(toolchain), AX_SM_ANDROID_ARCH="arm64-v8a" if args.arch == "arm64" else "x86_64", CROSS_COMPILE="1")
        env["AS"] = env["CC"]
        cflags += ["-DANDROID=1"]
        cxxflags += ["-DANDROID=1", "-stdlib=libc++"]
        env["LDFLAGS"] = "-Wl,-z,max-page-size=16384 -lc++_shared"
        metadata.update(minimum_system_version="23", ndk="27.3.13750724", cxx_abi="c++_shared", compile_definitions=["ANDROID=1"])
    env["CFLAGS"] = shlex.join(cflags)
    env["CXXFLAGS"] = shlex.join(cxxflags)
    # Autoconf and make consume these values as shell command fragments. Keep
    # path arguments quoted there, and decode them for direct subprocess calls.
    for key in ("CC", "CXX", "AR", "RANLIB", "STRIP", "OBJCOPY", "LD", "AS"):
        if key in env:
            env[key] = shlex.join([env[key]])
    configure = [f"--host={host}", f"--target={target}", "--disable-tests", "--disable-shared-js",
                 "--enable-posix-nspr-emulation", "--without-intl-api", "--disable-debug", "--disable-debug-symbols", "--enable-optimize"]
    if not metadata["jit"]:
        configure += ["--disable-ion"]
    return env, configure, metadata


def smoke(package: Path, env: dict, metadata: dict, work: Path, run_cross: bool) -> dict:
    compiler = shlex.split(env["CXX"])
    flags = [x for x in shlex.split(env["CXXFLAGS"]) if x != "-std=c++11"]
    command = compiler + ["-std=c++23"] + flags + ["-D" + value for value in metadata["compile_definitions"]]
    executable = work / "runtime-smoke"
    command += ["-DAX_SM_JIT=" + str(int(metadata["jit"])), "-I" + str(package / "include"), str(HERE / "smoke/runtime.cpp"), str(package / "lib/libjs_static.a"), "-lz", "-pthread"]
    if metadata["platform"] == "android":
        command += ["-lc++_shared", "-llog", "-Wl,-z,max-page-size=16384"]
    command += ["-o", str(executable)]
    run(command, env=env, log=work / "smoke-build.log")
    if metadata["platform"] == "macos":
        (package / "bin").mkdir()
        tool = compiler + ["-std=c++23"] + flags + ["-D" + value for value in metadata["compile_definitions"]]
        tool += ["-I" + str(package / "include"), str(HERE / "bytecode.cpp"), str(package / "lib/libjs_static.a"), "-lz", "-lpthread", "-o", str(package / "bin/axmol-jscompile")]
        run(tool, env=env, log=work / "bytecode-build.log")
    native = metadata["platform"] == "macos" and metadata["architecture"] == ("arm64" if platform.machine() in ("arm64", "aarch64") else "x86_64")
    if native or (metadata["platform"] == "macos" and run_cross):
        output = run([str(executable)])
        print(output)
        sign = ["codesign", "--force", "--sign", "-", "--options", "runtime"]
        if metadata["jit"]:
            sign += ["--entitlements", str(REPO / "3rdparty/spidermonkey/macos-jit.entitlements.plist")]
        run(sign + [str(executable)])
        run(["codesign", "--verify", "--strict", str(executable)])
        signed_output = run([str(executable)])
        print(signed_output)
        return {"cpp23_compile": "passed", "native_runtime": "passed" if native else "passed-rosetta", "hardened_adhoc_runtime": "passed", "output": output}
    return {"cpp23_compile": "passed", "native_runtime": "not-run-cross-compiled"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("macos", "ios", "ios-simulator", "android"))
    parser.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    parser.add_argument("--python2", help="existing Python 2.7.18 executable")
    parser.add_argument("--ndk", help="NDK r27d directory; Android only")
    parser.add_argument("--deployment-target", help="Apple deployment target, default macOS 11.0 / iOS 15.0")
    parser.add_argument("--sysroot", help="actual Apple SDK directory selected by CMake/Xcode")
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE, help="build/download cache (default: %(default)s)")
    parser.add_argument("--output", type=Path, help="package directory (default <cache-dir>/packages/<platform>-<arch>)")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument("--run-cross-smoke", action="store_true", help="run macOS x86_64 smoke via installed Rosetta, including hardened signing")
    parser.add_argument("--reconfigure", action="store_true", help="rerun configure when an existing build cache changes toolchain settings")
    parser.add_argument("--offline", action="store_true", help="require a cached checksum-verified source archive")
    args = parser.parse_args(argv)
    if args.platform == "ios" and args.arch != "arm64":
        parser.error("iOS devices require arm64; x86_64 is an ios-simulator package")
    if args.jobs < 1:
        parser.error("--jobs must be at least one")
    try:
        cache = args.cache_dir.resolve()
        cache.mkdir(parents=True, exist_ok=True)
        output = (args.output or cache / "packages" / (args.platform + "-" + args.arch)).resolve()
        with cache_lock(cache), output_lock(output):
            env, configure, metadata = environment(args)
            source, patch_id = prepare_source(cache, args.offline)
            settings = {"configure": configure, "CFLAGS": env["CFLAGS"], "CXXFLAGS": env["CXXFLAGS"], "LDFLAGS": env["LDFLAGS"], "CC": env["CC"], "CXX": env["CXX"], "PYTHON": env["PYTHON"], "AR": env.get("AR"), "RANLIB": env.get("RANLIB")}
            settings_id = hashlib.sha256(json.dumps(settings, sort_keys=True).encode()).hexdigest()[:16]
            work = cache / "tmp" / ("build-" + args.platform + "-" + args.arch + "-" + patch_id[:12] + "-" + settings_id)
            work.mkdir(parents=True, exist_ok=True)
            settings_file = work / ".axmol-settings.json"
            changed = settings_file.exists() and json.loads(settings_file.read_text()) != settings
            if changed and not args.reconfigure:
                raise RuntimeError(f"build settings changed; rerun with --reconfigure or use a separate --cache-dir: {work}")
            if changed or args.reconfigure or not settings_file.exists():
                (work / "config.cache").unlink(missing_ok=True)
                run([str(source / "js/src/configure")] + configure, cwd=work, env=env, log=work / "configure.log")
                settings_file.write_text(json.dumps(settings, indent=2) + "\n")
            run(["make", "-j", str(args.jobs)], cwd=work, env=env, log=work / "build.log")
            confdefs = (work / "js/src/js-confdefs.h").read_text()
            actual_jit = "#define JS_ION 1" in confdefs
            if actual_jit != metadata["jit"]:
                raise RuntimeError("configured JIT does not match platform policy")
            if args.arch == "arm64" and "#define JS_CODEGEN_ARM 1" in confdefs:
                raise RuntimeError("ARM64 was misclassified as the 32-bit ARM JIT backend")
            output.parent.mkdir(parents=True, exist_ok=True)
            package = output.parent / ("." + output.name + ".staging-" + str(os.getpid()))
            package.mkdir()
            # Copy dereferenced build symlinks, never ship the object directory.
            shutil.copytree(work / "dist/include", package / "include", dirs_exist_ok=True, symlinks=False)
            (package / "lib").mkdir(exist_ok=True)
            shutil.copy2(work / "dist/lib/libjs_static.a", package / "lib/libjs_static.a")
            shutil.copy2(source / "LICENSE", package / "LICENSE")
            metadata["compile_definitions"] = PUBLIC_DEFINES + metadata.get("compile_definitions", [])
            verification = smoke(package, env, metadata, work, args.run_cross_smoke)
            manifest = {"schema_version": 1, "runtime": "spidermonkey", "version": "33.1.1", "source_commit": SOURCE_COMMIT,
                        "source_archive_sha256": SOURCE_SHA256, "patch_sha256": patch_id,
                        "library": "lib/libjs_static.a", "library_sha256": digest(package / "lib/libjs_static.a"),
                        "config_header_sha256": digest(package / "include/js-config.h"), "build_standard": "c++11", "consumer_standard": "c++23",
                        "gc": {"exact_rooting": True, "generational": True, "incremental": True}, "verification": verification,
                        "compiler": run(shlex.split(env["CXX"]) + ["--version"]).splitlines()[0], "python": "2.7.18", **metadata}
            (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
            previous = output.parent / ("." + output.name + ".previous-" + str(os.getpid()))
            if output.exists():
                output.rename(previous)
            try:
                package.rename(output)
            except OSError:
                if previous.exists():
                    previous.rename(output)
                raise
            if previous.exists():
                shutil.rmtree(previous)
            print(f"AX_JS_SPIDERMONKEY_ROOT={output}")
            return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"SpiderMonkey build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
