// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include <doctest.h>
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"

int main(int argc,char** argv)
{
    ax::AutoreleasePool pool("existing C++ headless regressions");
    ax::Director::getInstance();
    doctest::Context context;
    context.applyCommandLine(argc,argv);
    context.setOption("no-breaks",true);
    return context.run();
}
