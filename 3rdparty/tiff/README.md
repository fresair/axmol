# Optional native TIFF decoding

`AX_WITH_TIFF=ON` builds LibTIFF 4.7.2 as the static C99 target `tiff`, with the
aliases `TIFF::TIFF` and `TIFF::tiff`. Axmol and its bindings remain C++23.
The default is OFF; the JS template and original JS test project explicitly
enable it. Enabling TIFF does not select or link another C++ runtime.

The original Cocos2d-x v3 particle resources include gzip/base64 TIFF textures.
`Image::initWithImageData` decompresses those same bytes and uses an in-memory
LibTIFF stream. The decoder returns RGBA8 with associated alpha, records
`hasPremultipliedAlpha=true`, and normalizes all eight TIFF orientations to a
top-left image, including transposition of the width/height for orientations
5–8. It preserves all existing `Image::Format` numeric values; the optional
TIFF value is appended after UNKNOWN. Decode failures do not publish partial
pixels. Concurrent loads use independent stream state and per-handle diagnostic
handlers rather than changing LibTIFF's process-wide handlers.

LibTIFF supports Classic TIFF and BigTIFF in either byte order, strip/tile
storage, and its standard RGBA conversion. PackBits, LZW, CCITT, and the other
built-in codecs come from the pinned source. Deflate and PixarLog use Axmol's
existing `ZLIB::ZLIB`. JPEG and old JPEG are enabled when `AX_WITH_JPEG=ON` and
use Axmol's existing `jpeg-turbo`. The CMake wrapper deliberately disables
optional external JBIG, LERC, LZMA, Zstd, WebP and libdeflate codecs; it never
silently picks up a second JPEG/zlib or a user-installed optional codec.
Unsupported compression produces a decode failure. Image decoding reads the
first TIFF directory; TIFF authoring, multipage animation and arbitrary TIFF
metadata are outside the Image API.

Allocation limits protect the texture loader from malformed or excessive
dimensions: one decoded RGBA image is at most 256 MiB, and LibTIFF's internal
single/cumulative allocations are limited to 256/512 MiB. All stream reads and
seeks stay within the supplied buffer. These bounds are independent of the
graphics backend's texture size limit.

## Source and license

The unmodified source subset comes from the official
[LibTIFF 4.7.2 release](https://libtiff.gitlab.io/libtiff/releases/v4.7.2.html)
and [release archive](https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz).
The archive SHA256 is
`672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a`.
[manifest.json](manifest.json) records the pin, every vendored file digest,
and the fact that no upstream patches are applied. Library/CMake/port sources
are retained; upstream executables, test images and documentation are omitted.
The subset also omits Autotools-only `Makefile.am`, `Makefile.in` and `*.h.in`
inputs, the unused Automake port dummy, and Sphinx/manpage generators. CMake's
`*.h.cmake.in` templates, `configure.ac` (read by the CMake version modules),
`build/CMakeLists.txt`, optional platform sources and license notices remain.
Re-import removes only explicitly excluded paths whose contents match the
pinned official archive. It rejects symbolic links in destination paths before
writing or deleting files. `--verify` checks both the complete file set and each
retained digest.
Copyright and permissive license notices are preserved in the source files and
[upstream/LICENSE.md](upstream/LICENSE.md).

```sh
curl --noproxy '*' -o tiff-4.7.2.tar.gz \
  https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz
python3 3rdparty/tiff/import-source.py tiff-4.7.2.tar.gz
python3 3rdparty/tiff/import-source.py --verify
```

## Actual decoder coverage

`js-tiff-tests` compares every output byte against an independent oracle for
all 13 original embedded TIFF particle plists, through both their gzip and raw
TIFF inputs. The fixture helper uses only Python's standard library, directly
reading the original uncompressed RGBA strip samples; it does not use LibTIFF
or an image decoder to create the oracle. It records the original plist, packed
input, TIFF and expected-pixel SHA256 values in its generated manifest.
