// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// v3 jsval_to_ccpoint/jsval_to_ccsize coerced components with JS::ToNumber, so
// original sources can build points from numeric strings (Number.toFixed and
// similar). Malformed or non-finite components must still be rejected.
(function () {
    function check(ok, message) { if (!ok) throw Error('Vec2 coercion: ' + message); }

    var widget = new ccui.Widget();
    widget.setPositionPercent(cc.p('0.42', '0.73'));
    var percent = widget.getPositionPercent();
    check(Math.abs(percent.x - 0.42) < 1e-6 && Math.abs(percent.y - 0.73) < 1e-6,
        'position percent from numeric strings');

    var size = new ccui.Widget();
    size.setSizePercent(cc.p('0.25', '0.5'));
    var sizePercent = size.getSizePercent();
    check(Math.abs(sizePercent.x - 0.25) < 1e-6 && Math.abs(sizePercent.y - 0.5) < 1e-6,
        'size percent from numeric strings');

    var node = new cc.Node();
    node.setPosition(cc.p('12', '34'));
    check(node.getPositionX() === 12 && node.getPositionY() === 34,
        'node position from numeric strings');

    // The legacy cc.p maps falsy components to 0 in JavaScript, so the native
    // boundary only sees strings here. Raw objects can still carry NaN or a
    // non-numeric string, which must be rejected instead of becoming geometry.
    var accepted = [];
    ['abc', {}].forEach(function (bad) {
        try {
            node.setPosition({x:bad, y:1});
            accepted.push(String(bad));
        } catch (error) {
        }
    });
    [NaN, Infinity, -Infinity].forEach(function (bad) {
        try {
            node.setPosition({x:bad, y:1});
            accepted.push(String(bad));
        } catch (error) {
        }
    });
    check(accepted.length === 0, 'invalid point components must be rejected, accepted ' + accepted.join(','));
    check(isNaN(cc.p(undefined, 1).x) === false && cc.p(undefined, 1).x === 0,
        'legacy cc.p keeps its falsy-to-zero contract');
    var coercedPoint = cc.p('0.5', '2');
    node.setPosition(coercedPoint);
    check(node.getPositionX() === 0.5 && node.getPositionY() === 2,
        'raw string components coerce at the native boundary');

    var sized = new cc.Node();
    sized.setContentSize(cc.size('10', '20'));
    var sizedOut = sized.getContentSize();
    check(sizedOut.width === 10 && sizedOut.height === 20,
        'content size from numeric strings');
    return 0;
})();
