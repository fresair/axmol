// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#pragma once

#include "axmol/axmol.h"

class AppDelegate : private ax::Application
{
public:
    AppDelegate() = default;
    ~AppDelegate() override;
    void applicationWillLaunch() override;
    bool applicationDidFinishLaunching() override;
    void applicationDidEnterBackground() override;
    void applicationWillEnterForeground() override;
    void applicationWillQuit() override;
};
