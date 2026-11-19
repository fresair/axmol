/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
package dev.axmol.lib;

import java.nio.ByteBuffer;

/** Immutable byte-buffer YUV420 layout; offsets are relative to the source position. */
public final class Yuv420FrameLayout {
    public static final int NV12 = 3;
    public static final int I420 = 4;
    // Bound native staging memory, including malformed decoder dimensions; common 8K fits.
    public static final int MAX_FRAME_BYTES = 128 * 1024 * 1024;

    public final int width, height, stride, sliceHeight;
    public final int cropLeft, cropTop, cropRight, cropBottom, pixelFormat;
    public final int visibleWidth, visibleHeight, yBytes, outputBytes, rawChromaOffset;
    public final int yLastEnd, uLastEnd, vLastEnd;
    private final int yStart, uStart, vStart, chromaStride, chromaRowBytes;

    public Yuv420FrameLayout(int width, int height, int stride, int sliceHeight,
        int cropLeft, int cropTop, int cropRight, int cropBottom, int pixelFormat) {
        require(width > 0 && height > 0 && stride >= width && sliceHeight >= height,
            "invalid dimensions/stride/slice height");
        require(pixelFormat == NV12 || pixelFormat == I420, "unsupported YUV420 pixel format");
        require((width & 1) == 0 && (height & 1) == 0 && (stride & 1) == 0 &&
            (sliceHeight & 1) == 0, "odd raw YUV420 dimensions/stride/slice height");
        require(cropLeft >= 0 && cropTop >= 0 && cropRight >= cropLeft &&
            cropBottom >= cropTop && cropRight < width && cropBottom < height,
            "crop outside raw dimensions");
        int visibleWidth = cropRight - cropLeft + 1;
        int visibleHeight = cropBottom - cropTop + 1;
        require((cropLeft & 1) == 0 && (cropTop & 1) == 0 &&
            (visibleWidth & 1) == 0 && (visibleHeight & 1) == 0,
            "unsupported odd YUV420 crop dimensions/origin");
        this.width = width;
        this.height = height;
        this.stride = stride;
        this.sliceHeight = sliceHeight;
        this.cropLeft = cropLeft;
        this.cropTop = cropTop;
        this.cropRight = cropRight;
        this.cropBottom = cropBottom;
        this.pixelFormat = pixelFormat;
        this.visibleWidth = visibleWidth;
        this.visibleHeight = visibleHeight;
        yBytes = index((long) visibleWidth * visibleHeight);
        outputBytes = index((long) yBytes + yBytes / 2);
        require(outputBytes <= MAX_FRAME_BYTES, "normalized YUV420 frame exceeds staging limit");
        rawChromaOffset = index((long) stride * sliceHeight);
        yStart = index((long) cropTop * stride + cropLeft);
        yLastEnd = lastEnd(yStart, stride, visibleWidth, visibleHeight);
        chromaStride = pixelFormat == NV12 ? stride : stride / 2;
        chromaRowBytes = pixelFormat == NV12 ? visibleWidth : visibleWidth / 2;
        uStart = index((long) rawChromaOffset + (long) (cropTop / 2) * chromaStride +
            (pixelFormat == NV12 ? cropLeft : cropLeft / 2));
        uLastEnd = lastEnd(uStart, chromaStride, chromaRowBytes, visibleHeight / 2);
        vStart = pixelFormat == I420 ? index((long) uStart +
            (long) chromaStride * (sliceHeight / 2)) : 0;
        vLastEnd = pixelFormat == I420 ? lastEnd(vStart, chromaStride,
            chromaRowBytes, visibleHeight / 2) : 0;
    }

    /** Validate every visible row before allocating or changing the destination. */
    public ByteBuffer normalize(ByteBuffer source, ByteBuffer reusable) {
        require(source != null, "missing decoder output buffer");
        int remaining = source.remaining();
        checkRows(remaining, yStart, stride, visibleWidth, visibleHeight);
        checkRows(remaining, uStart, chromaStride, chromaRowBytes, visibleHeight / 2);
        if (pixelFormat == I420)
            checkRows(remaining, vStart, chromaStride, chromaRowBytes, visibleHeight / 2);
        ByteBuffer destination = reusable;
        if (destination == null || !destination.isDirect() || destination.isReadOnly() ||
            destination.capacity() < outputBytes || destination == source)
            destination = ByteBuffer.allocateDirect(outputBytes);
        destination.clear();
        destination.limit(outputBytes);
        ByteBuffer input = source.duplicate();
        copyRows(input, source.position(), source.limit(), destination,
            yStart, stride, visibleWidth, visibleHeight);
        copyRows(input, source.position(), source.limit(), destination,
            uStart, chromaStride, chromaRowBytes, visibleHeight / 2);
        if (pixelFormat == I420)
            copyRows(input, source.position(), source.limit(), destination,
                vStart, chromaStride, chromaRowBytes, visibleHeight / 2);
        destination.flip();
        return destination;
    }

    private static void checkRows(int remaining, int start, int stride, int bytes, int rows) {
        for (int row = 0; row < rows; ++row) {
            long offset = (long) start + (long) row * stride;
            require(offset >= 0 && offset + bytes <= remaining,
                "decoder buffer missing visible YUV420 row bytes");
        }
    }

    private static void copyRows(ByteBuffer input, int base, int limit, ByteBuffer output,
        int start, int stride, int bytes, int rows) {
        for (int row = 0; row < rows; ++row) {
            int offset = (int) ((long) base + start + (long) row * stride);
            input.limit(limit);
            input.position(offset);
            input.limit(offset + bytes);
            output.put(input);
        }
    }

    private static int lastEnd(int start, int stride, int bytes, int rows) {
        return index((long) start + (long) (rows - 1) * stride + bytes);
    }

    private static int index(long value) {
        require(value >= 0 && value <= Integer.MAX_VALUE, "YUV420 byte offset/length overflow");
        return (int) value;
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new IllegalArgumentException(message);
    }

    @Override
    public String toString() {
        return "raw=" + width + "x" + height + " stride=" + stride + " slice=" + sliceHeight
            + " crop=" + cropLeft + "," + cropTop + ".." + cropRight + "," + cropBottom
            + " pf=" + pixelFormat + " rawCbcr=" + rawChromaOffset
            + " visible=" + visibleWidth + "x" + visibleHeight
            + " Yend=" + yLastEnd + " UVorUend=" + uLastEnd + " Vend=" + vLastEnd
            + " outputBytes=" + outputBytes;
    }
}
