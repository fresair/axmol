// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
package dev.axmol.app;

/** Public names are kept by the template's R8 rules for jsb.reflection. */
public final class JsBridge {
    private JsBridge() {}
    public static String hello(String name) {
        return "Hello, " + (name == null ? "JavaScript" : name);
    }
    public static int add(int left, int right) {
        return left + right;
    }
}
