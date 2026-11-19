/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
(function () {
    function triangular(n) {
        var total = 0;
        for (var i = 1; i <= n; ++i) total += i;
        return total;
    }
    function closure(base) { return function (value) { return base + value; }; }
    var values = [1, 3, 5].map(closure(7));
    this.legacyBytecodeResult = triangular(20) + values.reduce(function (a, b) { return a + b; }, 0);
    this.legacyBytecodeText = '旧字节码 / SpiderMonkey 33.1';
})();
function legacyBytecodeValue() { return legacyBytecodeResult; }
