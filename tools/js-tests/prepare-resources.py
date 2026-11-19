#!/usr/bin/env python3
# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
"""Prepare checksum-pinned v3 JS test assets in the engine's shared cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def verify(root, files):
    for row in files:
        path = root / PurePosixPath(row['path']).relative_to('res')
        if not path.is_file() or path.stat().st_size != row['size'] or digest(path) != row['sha256']:
            raise RuntimeError('Missing or changed pinned resource: ' + str(path))


def fetch_archive(cache, repository, revision):
    target = cache / (repository.replace('/', '-') + '-' + revision + '.tar.gz')
    if not target.is_file():
        partial = target.with_suffix(target.suffix + '.partial')
        with urllib.request.urlopen('https://codeload.github.com/' + repository + '/tar.gz/' + revision) as response:
            with partial.open('wb') as stream:
                shutil.copyfileobj(response, stream, 1024 * 1024)
        partial.replace(target)
    return target


def extract_pinned(archive, selected, destination):
    found = set()
    # Extract only manifest-listed regular files, never arbitrary archive paths.
    with tarfile.open(archive, 'r|gz') as tar:
        for member in tar:
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2:
                continue
            key = '/'.join(parts[1:])
            row = selected.get(key)
            if row is None:
                continue
            if not member.isfile():
                raise RuntimeError('Pinned asset is not a regular archive entry: ' + key)
            relative = PurePosixPath(row['path']).relative_to('res')
            assert '..' not in relative.parts
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as source, target.open('wb') as stream:
                shutil.copyfileobj(source, stream)
            if target.stat().st_size != row['size'] or digest(target) != row['sha256']:
                raise RuntimeError('Downloaded resource SHA mismatch: ' + key)
            found.add(key)
    missing = set(selected) - found
    if missing:
        raise RuntimeError('Pinned archive lacks resources: ' + ', '.join(sorted(missing)[:5]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', type=Path, default=ROOT / 'tests/js-tests')
    parser.add_argument('--cache-root', type=Path)
    parser.add_argument('--adopt-local', action='store_true', help='Move a verified existing resource directory to cache')
    parser.add_argument('--offline', action='store_true', help='Never download missing resources')
    args = parser.parse_args()
    suite = args.app_root.resolve() / 'Content/legacy/js-tests'
    manifest = json.loads((suite / 'provenance.json').read_text())
    files = [row for row in manifest['files'] if row['path'].startswith('res/')]
    assert files and len(manifest['revision']) == 40
    cache = (args.cache_root or ROOT / 'cache/js-tests' / ('v3-' + manifest['revision'])).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    resources = cache / 'res'
    local = suite / 'res'
    if args.adopt_local and local.is_dir() and not local.is_symlink():
        assert not resources.exists(), 'Do not replace existing cached resources'
        verify(local, files)
        local.rename(resources)
    if not resources.is_dir():
        if args.offline:
            raise RuntimeError('Resources are not cached; run this command without --offline to fetch pinned assets')
        staging = cache / 'res.partial'
        assert not staging.exists(), 'Inspect previous incomplete download before retrying: ' + str(staging)
        staging.mkdir()
        cocos = {row['source'].removeprefix('cocos2d-x/'): row for row in files
                 if row['source'].startswith('cocos2d-x/')}
        ccs = {row['source'].split('/', 1)[1]: row for row in files if row['source'].startswith('ccs-res@')}
        archives = []
        if cocos:
            archive = fetch_archive(cache, 'cocos2d/cocos2d-x', manifest['revision'])
            archives.append(archive)
            extract_pinned(archive, cocos, staging)
        if ccs:
            archive = fetch_archive(cache, 'dumganhar/ccs-res', manifest['ccs_res']['revision'])
            archives.append(archive)
            extract_pinned(archive, ccs, staging)
        verify(staging, files)
        staging.rename(resources)
        # Download containers are reproducible; the verified resource cache is sufficient.
        for archive in archives:
            archive.unlink()
    verify(resources, files)
    if local.is_symlink():
        if local.resolve() != resources:
            raise RuntimeError('Resource link points at an unexpected directory: ' + str(local))
    elif local.exists():
        # Platforms without symlink privileges may have a materialized copy.
        verify(local, files)
    else:
        try:
            local.symlink_to(os.path.relpath(resources, local.parent), target_is_directory=True)
        except OSError:
            shutil.copytree(resources, local)
    print(json.dumps({'status': 'PASS_PINNED_JS_TEST_RESOURCES', 'files': len(files),
                      'resource_bytes': sum(row['size'] for row in files), 'cache': str(resources),
                      'app_link': str(local), 'symlink': local.is_symlink()}))


if __name__ == '__main__':
    main()
