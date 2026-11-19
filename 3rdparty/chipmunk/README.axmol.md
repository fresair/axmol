# Legacy JavaScript Chipmunk backend

This target builds the real Chipmunk 7.0.1 C solver for the Cocos2d-x `cp`
JavaScript compatibility API. It is linked only by the JS bindings target.
Axmol's current Physics2D APIs continue to use Box2D, and Physics3D uses Jolt.

Source: [slembcke/Chipmunk2D commit e98a7cedb366daecf388f0faa0acee996ee7d0f3](https://github.com/slembcke/Chipmunk2D/tree/e98a7cedb366daecf388f0faa0acee996ee7d0f3)
(tag `Chipmunk-7.0.1`). All vendored files in `upstream/` retain their original
contents except the documented HastySpace CPU-count portability patch.
The complete upstream MIT license is preserved in `upstream/LICENSE.txt`.
`manifest.json` records each file's SHA-256 and the downloaded archive digest.

The version is verified against the actual historical dependency header from
`cocos2d-x-3rd-party-libs-bin` commit
`a85b979dcd6b5cb8aa2bf97e52b73a2d827b3cfb`: its `chipmunk.h` defines
`CP_VERSION_MAJOR=7`, `CP_VERSION_MINOR=0`, `CP_VERSION_RELEASE=1`. The
historical header and types header were checked against their original Git
blob IDs; their source paths and hashes are recorded in the manifest.

Configuration is C99, `CP_USE_DOUBLES=1`, `CP_USE_CGTYPES=0`, with a static
position-independent library `ax_chipmunk` (`axmol::chipmunk`). ARM64 defines
the spelling `__arm64` recognized by the unchanged upstream HastySpace NEON
solver, including Android Clang's `__aarch64__` target. HastySpace uses the
original `sysctlbyname` CPU query on Apple and POSIX `sysconf` on Android/Linux,
whose current SDKs do not provide Apple's `sys/sysctl.h`. The patch and its
original/patched hashes are recorded in the manifest.
