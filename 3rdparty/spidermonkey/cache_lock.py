# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Serialize all runtime cache mutations across processes and nested entry points."""
from __future__ import annotations

from contextlib import contextmanager
import fcntl
import os
from pathlib import Path
import threading

_states: dict[tuple[int, Path], dict] = {}
_states_guard = threading.Lock()


@contextmanager
def cache_lock(cache: Path):
    """Hold one flock per cache, reentrant for builders composed in one thread.

    Keep the lock file permanently: unlinking it would let a new caller lock a
    different inode while an existing waiter still owns the old one.
    """
    cache = cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    with _file_lock(cache / ".axmol-runtime.lock"):
        yield


@contextmanager
def output_lock(output: Path):
    """Serialize package checks/publication even when callers use different caches.

    Always acquire the cache lock first, then this output lock. Both are
    reentrant so prepare may invoke build while holding the same pair.
    """
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with _file_lock(output.parent / ("." + output.name + ".axmol-publication.lock")):
        yield


@contextmanager
def _file_lock(path: Path):
    with _states_guard:
        state = _states.setdefault((os.getpid(), path), {"mutex": threading.RLock(), "depth": 0})
    with state["mutex"]:
        if state["depth"] == 0:
            stream = path.open("a+")
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
            except BaseException:
                stream.close()
                raise
            state["stream"] = stream
        state["depth"] += 1
        try:
            yield
        finally:
            state["depth"] -= 1
            if state["depth"] == 0:
                stream = state.pop("stream")
                try:
                    fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
                finally:
                    stream.close()
