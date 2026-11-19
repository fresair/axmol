/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

https://axmol.dev/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
 ****************************************************************************/
package dev.axmol.lib;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.text.BoringLayout;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.TextUtils;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class BitmapHelper {
    // ===========================================================
    // Constants
    // ===========================================================

    /* The values are the same as axmol/platform/Image.h. */
    private static final int HORIZONTAL_ALIGN_LEFT = 1;
    private static final int HORIZONTAL_ALIGN_RIGHT = 2;
    private static final int HORIZONTAL_ALIGN_CENTER = 3;
    private static final int VERTICAL_ALIGN_TOP = 1;
    private static final int VERTICAL_ALIGN_BOTTOM = 2;
    private static final int VERTICAL_ALIGN_CENTER = 3;

    // ===========================================================
    // Methods for/from SuperClass/Interfaces
    // ===========================================================

    // ===========================================================
    // Methods
    // ===========================================================

    private static native void nativeInitBitmapDC(final int width,
            final int height, final byte[] pixels);

    //http://egoco.de/post/19077604048/calculating-the-height-of-text-in-android
    public static int getTextHeight(String text, int maxWidth, float textSize, Typeface typeface) {
        TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setTextSize(textSize);
        paint.setTypeface(typeface);

        int lineCount = 0;

        int index = 0;
        int length = text.length();

        while(index < length) {
            final int charsToAdvance = paint.breakText(text, index, length, true, maxWidth, null);
            if(charsToAdvance == 0) {
                index++;
            }
            else {
                index += charsToAdvance;
                lineCount++;
            }
        }

        float actualHeight = (Math.abs(paint.ascent()) + Math.abs(paint.descent()));

        return (int)Math.floor(lineCount * actualHeight);
    }

    public static Typeface calculateShrinkTypeFace(String text, int width, int height, Layout.Alignment hAlignment, float textSize, TextPaint paint, boolean enableWrap)
    {
        if (width == 0 || height == 0) {
            return  paint.getTypeface();
        }
        float actualWidth = width + 1;
        float actualHeight = height + 1;
        float fontSize = textSize + 1;

        if (!enableWrap) {
            while (actualWidth > width || actualHeight > height) {
                fontSize = fontSize - 1;

                actualWidth = (int)Math.ceil( StaticLayout.getDesiredWidth(text, paint));
                actualHeight = getTextHeight(text, (int)actualWidth, fontSize, paint.getTypeface());

                paint.setTextSize(fontSize);
                if (fontSize <= 0) {
                    paint.setTextSize(textSize);
                    break;
                }
            }
        } else {
            while (actualHeight > height || actualWidth > width) {
                fontSize = fontSize - 1;

                Layout layout = new StaticLayout(text, paint, (int) width, hAlignment,1.0f,0.0f,false);
                actualWidth = layout.getWidth();
                actualHeight = layout.getLineTop(layout.getLineCount());

                paint.setTextSize(fontSize);

                if (fontSize <= 0) {
                    paint.setTextSize(textSize);
                    break;
                }
            }

        }
        return paint.getTypeface();
    }

    public static boolean createTextBitmapWithStroke(byte[] bytes,  final String fontName, int fontSize,
                                                    int fontTintR, int fontTintG, int fontTintB, int fontTintA,
                                                    int alignment, int width, int height,
                                                    boolean stroke, int strokeR, int strokeG, int strokeB, int strokeA, float strokeSize, boolean enableWrap, int overflow) {
        String string;
        if (bytes == null || bytes.length == 0) {
          return false;
        } else {
          string = new String(bytes);
        }

        TextPaint paint = BitmapHelper.newPaint(fontName, fontSize);
        if (stroke) {
            paint.setStyle(TextPaint.Style.STROKE);
            paint.setStrokeWidth(strokeSize);
        }
        TextBitmapLayout geometry=createTextBitmapLayout(string,paint,fontSize,alignment,width,height,enableWrap,overflow);
        if(geometry==null)return false;
        Layout layout=geometry.layout;
        int bitmapWidth=geometry.bitmapWidth, bitmapHeight=geometry.bitmapHeight;
        int offsetX=geometry.offsetX, offsetY=geometry.offsetY;


        Bitmap bitmap = Bitmap.createBitmap(bitmapWidth, bitmapHeight, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        canvas.translate(offsetX, offsetY);
        if ( stroke )
        {
            paint.setARGB(strokeA, strokeR, strokeG, strokeB);
            layout.draw(canvas);
        }
        paint.setStyle(TextPaint.Style.FILL);
        paint.setARGB(fontTintA, fontTintR, fontTintG, fontTintB);
        layout.draw(canvas);

        BitmapHelper.initNativeObject(bitmap);
        return true;
    }


    private static final class TextBitmapLayout {
        final Layout layout;
        final int bitmapWidth, bitmapHeight, offsetX, offsetY;
        TextBitmapLayout(Layout value, int width, int height, int x, int y) {
            layout=value; bitmapWidth=width; bitmapHeight=height; offsetX=x; offsetY=y;
        }
    }

    private static TextBitmapLayout createTextBitmapLayout(String string, TextPaint paint,
            int fontSize, int alignment, int width, int height, boolean enableWrap, int overflow) {
        Layout.Alignment hAlignment = Layout.Alignment.ALIGN_NORMAL;
        int horizontalAlignment = alignment & 0x0F;
        switch (horizontalAlignment) {
            case HORIZONTAL_ALIGN_CENTER:
                hAlignment = Layout.Alignment.ALIGN_CENTER;
                break;
            case HORIZONTAL_ALIGN_RIGHT:
                hAlignment = Layout.Alignment.ALIGN_OPPOSITE;
                break;
            case HORIZONTAL_ALIGN_LEFT:
                break;
            default:
                break;
        }

        int maxWidth = width;

        if (maxWidth <= 0) {
            maxWidth = (int)Math.ceil( StaticLayout.getDesiredWidth(string, paint));
        }

        Layout layout = null;
        int layoutWidth = 0;
        int layoutHeight = 0;


        if (overflow == 1 && !enableWrap){
            int widthBoundary = (int)Math.ceil( StaticLayout.getDesiredWidth(string, paint));
            layout = new StaticLayout(string, paint, widthBoundary , hAlignment,1.0f,0.0f,false);
        }else {
            if (overflow == 2) {
                calculateShrinkTypeFace(string, width, height, hAlignment, fontSize, paint, enableWrap);
            }
            layout = new StaticLayout(string, paint, maxWidth , hAlignment,1.0f,0.0f,false);
        }

        layoutWidth = layout.getWidth();
        layoutHeight = layout.getLineTop(layout.getLineCount());

        int bitmapWidth = Math.max(layoutWidth, width);
        int bitmapHeight = layoutHeight;
        if (height > 0) {
            bitmapHeight = height;
        }

        if (overflow == 1 && !enableWrap) {
            if (width > 0) {
                bitmapWidth = width;
            }
        }

        if (bitmapWidth == 0 || bitmapHeight == 0) {
            return null;
        }

        int offsetX = 0;
        if (horizontalAlignment == HORIZONTAL_ALIGN_CENTER) {
            offsetX = (bitmapWidth - layoutWidth) / 2;
        }
        else if (horizontalAlignment == HORIZONTAL_ALIGN_RIGHT) {
            offsetX = bitmapWidth - layoutWidth;
        }

        int offsetY = 0;
        int verticalAlignment   = (alignment >> 4) & 0x0F;
        switch (verticalAlignment)
        {
            case VERTICAL_ALIGN_CENTER:
                offsetY = (bitmapHeight - layoutHeight) / 2;
                break;
            case VERTICAL_ALIGN_BOTTOM:
                offsetY = bitmapHeight - layoutHeight;
                break;
        }

        return new TextBitmapLayout(layout, bitmapWidth, bitmapHeight, offsetX, offsetY);
    }

    // Internal JNI geometry query. It uses the identical actual StaticLayout
    // as bitmap rendering, not per-character synthetic measurements.
    // Packed floats: schema, codepoints, bitmap width/height, line count,
    // then each line's start/end codepoint, left/right, top/bottom, offset count
    // followed by one x offset for every codepoint boundary in that line.
    public static float[] getInputFieldTextLayout(byte[] bytes, String fontName, int fontSize,
            int alignment, int width, int height, boolean enableWrap, int overflow) {
        if(bytes==null || bytes.length==0)return null;
        String string=new String(bytes);
        TextBitmapLayout bitmap=createTextBitmapLayout(string,newPaint(fontName,fontSize),
                fontSize,alignment,width,height,enableWrap,overflow);
        if(bitmap==null)return null;
        Layout layout=bitmap.layout;
        int lines=layout.getLineCount(), chars=string.codePointCount(0,string.length());
        int count=5;
        for(int line=0;line<lines;line++) {
            // Existing cursor search assumes monotonic left-to-right x offsets.
            // Do not pretend the fallback path repairs bidirectional layout.
            if(layout.getParagraphDirection(line)!=Layout.DIR_LEFT_TO_RIGHT)return null;
            // A left-to-right paragraph may still contain RTL runs whose
            // primary insertion positions happen to be nondecreasing. The
            // native single-x-per-codepoint model cannot represent their
            // secondary positions or discontiguous selection geometry.
            for(int offset=layout.getLineStart(line);offset<layout.getLineEnd(line);offset++)
                if(layout.isRtlCharAt(offset))return null;
            count+=7+string.codePointCount(layout.getLineStart(line),layout.getLineEnd(line))+1;
        }
        float[] result=new float[count];
        int out=0;
        result[out++]=1;result[out++]=chars;result[out++]=bitmap.bitmapWidth;
        result[out++]=bitmap.bitmapHeight;result[out++]=lines;
        for(int line=0;line<lines;line++) {
            int begin=layout.getLineStart(line),end=layout.getLineEnd(line);
            int visibleEnd=layout.getLineVisibleEnd(line), n=string.codePointCount(begin,end);
            float left=layout.getLineLeft(line)+bitmap.offsetX;
            float right=layout.getLineRight(line)+bitmap.offsetX;
            result[out++]=string.codePointCount(0,begin);
            result[out++]=string.codePointCount(0,end);
            result[out++]=left;result[out++]=right;
            result[out++]=layout.getLineTop(line)+bitmap.offsetY;
            result[out++]=layout.getLineBottom(line)+bitmap.offsetY;
            result[out++]=n+1;
            int offset=begin;
            for(int ch=0;ch<=n;ch++) {
                // At a soft line break getPrimaryHorizontal(end) belongs to
                // the next line. Clamp trailing whitespace to this line's real
                // drawn edge instead of borrowing the next line's x position.
                float x=offset>=visibleEnd ? right : layout.getPrimaryHorizontal(offset)+bitmap.offsetX;
                result[out++]=x-left;
                if(ch<n)offset+=Character.charCount(string.codePointAt(offset));
            }
        }
        return result;
    }

    private static TextPaint newPaint(final String fontName, final int fontSize) {
        final TextPaint paint = new TextPaint();
        paint.setTextSize(fontSize);
        paint.setAntiAlias(true);

        if (fontName != null && fontName.endsWith(".ttf")) {
            try {
                paint.setTypeface(fontName.startsWith("/")
                        ? Typeface.createFromFile(fontName)
                        : Typeface.createFromAsset(AxmolEngine.getAssetManager(), fontName));
                return paint;
            } catch (RuntimeException exception) {
                // Missing or invalid files retain the system-family fallback.
                // No extra cache: later calls can retry a file that appears.
            }
        }
        paint.setTypeface(Typeface.create(fontName, Typeface.NORMAL));

        return paint;
    }

    private static void initNativeObject(final Bitmap bitmap) {
        final byte[] pixels = BitmapHelper.getPixels(bitmap);
        if (pixels == null) {
            return;
        }

        BitmapHelper.nativeInitBitmapDC(bitmap.getWidth(),
                bitmap.getHeight(), pixels);
    }

    private static byte[] getPixels(final Bitmap bitmap) {
        if (bitmap != null) {
            final byte[] pixels = new byte[bitmap.getWidth()
                    * bitmap.getHeight() * 4];
            final ByteBuffer buf = ByteBuffer.wrap(pixels);
            buf.order(ByteOrder.nativeOrder());
            bitmap.copyPixelsToBuffer(buf);
            return pixels;
        }

        return null;
    }

    public static int getFontSizeAccordingHeight(int height) {
        TextPaint paint = new TextPaint();
        Rect bounds = new Rect();

        paint.setTypeface(Typeface.DEFAULT);
        int text_size = 1;
        boolean found_desired_size = false;

        while (!found_desired_size) {
            paint.setTextSize(text_size);
            String text = "SghMNy";
            paint.getTextBounds(text, 0, text.length(), bounds);

            text_size++;

            if (height - bounds.height() <= 2) {
                found_desired_size = true;
            }
        }
        return text_size;
    }

    private static String getStringWithEllipsis(String string, float width,
                                                float fontSize) {
        if (TextUtils.isEmpty(string)) {
            return "";
        }

        TextPaint paint = new TextPaint();
        paint.setTypeface(Typeface.DEFAULT);
        paint.setTextSize(fontSize);

        return TextUtils.ellipsize(string, paint, width,
                TextUtils.TruncateAt.END).toString();
    }
}
