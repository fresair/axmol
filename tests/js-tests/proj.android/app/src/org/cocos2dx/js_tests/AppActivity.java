/****************************************************************************
Copyright (c) 2015-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2026 Axmol contributors.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

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
package org.cocos2dx.js_tests;

import android.app.AlertDialog;
import android.content.Context;
import java.util.concurrent.atomic.AtomicInteger;
import dev.axmol.lib.AxmolActivity;

// Retains the exact reflection class name and signatures in the unchanged
// v3 ReflectionTest.js. The actual Activity remains dev.axmol.app.AppActivity.
public final class AppActivity {
    private static final AtomicInteger utfCalls = new AtomicInteger();
    private static native long nativeGeneration();
    private static native void nativeEvalString(long generation, String source);

    private static AxmolActivity activity() {
        Context context = AxmolActivity.getContext();
        if (!(context instanceof AxmolActivity))
            throw new IllegalStateException("The native reflection test requires a running AxmolActivity");
        return (AxmolActivity) context;
    }

    private static void show(final String title, final String message, final String button) {
        final AxmolActivity app = activity();
        final long generation = nativeGeneration();
        app.runOnUiThread(() -> new AlertDialog.Builder(app)
            .setTitle(title)
            .setMessage(message)
            .setIcon(app.getApplicationInfo().icon)
            .setCancelable(true)
            .setPositiveButton(button, (dialog, which) -> app.runOnAxmolThread(() ->
                nativeEvalString(generation, "cc.log(\"Javascript Java bridge!\")")))
            .show());
    }

    public static void showAlertDialog(final String title, final String message) {
        show(title, message, "OK");
    }

    public static void showAlertDialog(final String title, final String message, final boolean logicSwitch) {
        show(title, message, logicSwitch ? "it's true" : "it's false");
    }

    public static String getUtfStr() {
        final String utf8Str = "you will see emotion:💝";
        final AxmolActivity app = activity();
        final long generation = nativeGeneration();
        utfCalls.incrementAndGet();
        app.runOnAxmolThread(() -> nativeEvalString(generation, "cc.log(\"" + utf8Str + "\")"));
        return utf8Str;
    }

    // An external assertion can verify the original 1,000-call constructor
    // stress without modifying its source or treating a rendered menu as pass.
    public static int getUtfStrCallCount() { return utfCalls.get(); }
}
