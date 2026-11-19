// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
// The legacy project itself is copied without edits. Its native scene provides
// the result of loading, inheritance, properties, label and sprite rendering.
function jsLegacyHelloResult() {
    var scene = cc.director.getRunningScene();
    if (!scene || !scene.isRunning()) { return 20; }
    var layers = scene.getChildren();
    cc.log("Legacy native scene: " + layers.length + " child(ren); running=" +
        layers.map(function (node) { return node.isRunning(); }).join(","));
    // Both the original Cocos Scene and Axmol include their default Camera in
    // getChildren(). Locate the unchanged application's one HelloWorld layer.
    var applicationLayers = layers.filter(function (node) { return node instanceof HelloWorldLayer; });
    if (applicationLayers.length !== 1 || !applicationLayers[0].isRunning()) { return 21; }
    var children = applicationLayers[0].getChildren();
    if (children.length !== 2) { return 22; }
    var hasLabel = false;
    var hasSprite = false;
    for (var i = 0; i < children.length; ++i) {
        var node = children[i];
        if (typeof node.getString === "function" && node.getString() === "Hello World") {
            hasLabel = true;
        }
        if (typeof node.getTexture === "function" && node.getTexture()) {
            hasSprite = true;
        }
        if (node.getPosition().x !== cc.winSize.width / 2) { return 23; }
    }
    if (!hasLabel || !hasSprite) { return 24; }
    cc.log("Unmodified Cocos2d-x v3 HelloWorld passed");
    return 0;
}
