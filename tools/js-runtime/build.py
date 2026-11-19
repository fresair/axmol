#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Compatibility launcher and import facade for the third-party runtime builder."""
from pathlib import Path
import importlib.util
import sys

_COMPONENT = Path(__file__).resolve().parents[2] / "3rdparty/spidermonkey"
sys.path.insert(0, str(_COMPONENT))
_SPEC = importlib.util.spec_from_file_location("axmol_spidermonkey_build", _COMPONENT / "build.py")
_MODULE = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _MODULE
_SPEC.loader.exec_module(_MODULE)
globals().update({name: value for name, value in vars(_MODULE).items() if not name.startswith("__")})

if __name__ == "__main__":
    raise SystemExit(_MODULE.main())
