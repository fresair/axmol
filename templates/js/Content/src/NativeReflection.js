// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
function runNativeReflectionSample() {
    var greeting;
    var sum;
    if (cc.sys.os === cc.sys.OS_ANDROID) {
        greeting = jsb.reflection.callStaticMethod("dev/axmol/app/JsBridge", "hello", "(Ljava/lang/String;)Ljava/lang/String;", "JavaScript");
        sum = jsb.reflection.callStaticMethod("dev/axmol/app/JsBridge", "add", "(II)I", 20, 22);
    } else {
        greeting = jsb.reflection.callStaticMethod("AxmolJsBridge", "hello:", "JavaScript");
        sum = jsb.reflection.callStaticMethod("AxmolJsBridge", "add:to:", 20, 22);
    }
    if (sum !== 42 || greeting !== "Hello, JavaScript") {
        throw new Error("Native reflection sample failed");
    }
    cc.log(greeting + "; native sum = " + sum);
    return true;
}
