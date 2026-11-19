#!/usr/bin/env python3
# Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
"""Generate a test-only protected Director phase from the actual production CPP.

The target links the complete matching SDK, without candidate objects. It runs
no beginFrame(), swapchain acquire, Scene draw or presentation and therefore
does not stand in for a platform/GPU renderFrame regression.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = (ROOT / 'axmol/base/Director.cpp').read_text()
    start = source.index('    // tick before glClear: issue #533\n')
    end = source.index('\n    if (canRender)', start)
    block = source[start:end]
    assert 'scriptEngine->updateFrame(scriptDelta);' in block
    assert block.index('_scheduler->getTimeScale()') < block.index('_scheduler->update(_deltaTime)')
    assert block.index('getScriptEngineIfExists()') > block.index('_scheduler->update(_deltaTime)')
    adapter = '''// Generated verbatim from the matching production Director update block.
#pragma once
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/CustomEvent.h"
#include "axmol/base/ScriptSupport.h"
#include <stdexcept>
namespace ax {
class HeadlessFrameDirector final : public Director {
public:
    static HeadlessFrameDirector* create() {
        if (s_SharedDirector) throw std::runtime_error("Existing Director in isolated frame fixture");
        auto* result = new HeadlessFrameDirector();
        s_SharedDirector = result;
        if (!result->init()) throw std::runtime_error("Native Director init failed");
        return result;
    }
    void advanceFrame(float delta) {
        _deltaTime = delta;
''' + block + '''
    }
};
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != adapter:
        args.output.write_text(adapter)


if __name__ == '__main__':
    main()
