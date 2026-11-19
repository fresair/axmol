// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
cc.game.onStart = function () {
    cc.view.setDesignResolutionSize(960, 640, cc.ResolutionPolicy.SHOW_ALL);
    cc.director.runScene(new JsSmokeScene());
};
cc.game.run();
