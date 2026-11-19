/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
package androidx.media3.exoplayer.mediacodec;

import androidx.media3.common.util.UnstableApi;

/**
 * Configuration-only bridge to Media3 1.11.1's package-private full constructor.
 *
 * <p>Byte-buffer decoding must not opt into detached Surface output. Preserve every selected
 * codec field and the original capabilities object; change only detachedSurfaceSupported.
 */
@UnstableApi
public final class AxmolByteBufferCodecInfo {
    private AxmolByteBufferCodecInfo() {}

    public static MediaCodecInfo withoutDetachedSurface(MediaCodecInfo original) {
        return new MediaCodecInfo(
            original.name,
            original.mimeType,
            original.codecMimeType,
            original.capabilities,
            original.hardwareAccelerated,
            original.softwareOnly,
            original.vendor,
            original.adaptive,
            original.tunneling,
            original.secure,
            /* detachedSurfaceSupported= */ false);
    }
}
