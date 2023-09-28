# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This action is used to produce test archives.
#
# Ideally, the data in this file should be defined in moz.build files.
# It is defined inline because this was easiest to make test archive
# generation faster.

from __future__ import absolute_import, print_function, unicode_literals

import argparse
import itertools
import os
import sys

from mozpack.files import FileFinder
from mozpack.mozjar import JarWriter
import mozpack.path as mozpath

import buildconfig

STAGE = mozpath.join(buildconfig.topobjdir, 'dist', 'test-stage')


ARCHIVE_FILES = {
    'common': [
        {
            'source': STAGE,
            'base': '',
            'pattern': '**',
            'ignore': [
                'cppunittest/**',
                'gtest/**',
                'mochitest/**',
                'reftest/**',
                'talos/**',
                'web-platform/**',
                'xpcshell/**',
            ],
        },
        {
            'source': buildconfig.topobjdir,
            'base': '_tests',
            'pattern': 'modules/**',
        },
        {
            'source': buildconfig.topobjdir,
            'base': '_tests',
            'pattern': 'mozbase/**',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'js/src',
            'pattern': 'jit-test/**',
            'dest': 'jit-test',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'js/src/tests',
            'pattern': 'ecma_6/**',
            'dest': 'jit-test/tests',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'js/src/tests',
            'pattern': 'js1_8_5/**',
            'dest': 'jit-test/tests',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'js/src/tests',
            'pattern': 'lib/**',
            'dest': 'jit-test/tests',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'js/src',
            'pattern': 'jsapi.h',
            'dest': 'jit-test',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'tps/**',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'services/sync/',
            'pattern': 'tps/**',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'services/sync/tests/tps',
            'pattern': '**',
            'dest': 'tps/tests',
        },
    ],
    'cppunittest': [
        {
            'source': STAGE,
            'base': '',
            'pattern': 'cppunittest/**',
        },
        # We don't ship these files if startup cache is disabled, which is
        # rare. But it shouldn't matter for test archives.
        {
            'source': buildconfig.topsrcdir,
            'base': 'startupcache/test',
            'pattern': 'TestStartupCacheTelemetry.*',
            'dest': 'cppunittest',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'runcppunittests.py',
            'dest': 'cppunittest',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'remotecppunittests.py',
            'dest': 'cppunittest',
        },
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'cppunittest.ini',
            'dest': 'cppunittest',
        },
        {
            'source': buildconfig.topobjdir,
            'base': '',
            'pattern': 'mozinfo.json',
            'dest': 'cppunittest',
        },
    ],
    'gtest': [
        {
            'source': STAGE,
            'base': '',
            'pattern': 'gtest/**',
        },
    ],
    'mochitest': [
        {
            'source': buildconfig.topobjdir,
            'base': '_tests/testing',
            'pattern': 'mochitest/**',
        },
        {
            'source': STAGE,
            'base': '',
            'pattern': 'mochitest/**',
        },
    ],
    'mozharness': [
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'mozharness/**',
        },
    ],
    'reftest': [
        {
            'source': STAGE,
            'base': '',
            'pattern': 'reftest/**',
        },
    ],
    'talos': [
        {
            'source': buildconfig.topsrcdir,
            'base': 'testing',
            'pattern': 'talos/**',
        },
    ],
    'web-platform': [
        {
            'source': buildconfig.topobjdir,
            'base': '_tests',
            'pattern': 'web-platform/**',
        },
        {
            'source': buildconfig.topobjdir,
            'base': '',
            'pattern': 'mozinfo.json',
            'dest': 'web-platform',
        },
    ],
    'xpcshell': [
        {
            'source': buildconfig.topobjdir,
            'base': '_tests/xpcshell',
            'pattern': '**',
            'dest': 'xpcshell/tests',
        },
        {
            'source': STAGE,
            'base': '',
            'pattern': 'xpcshell/**',
        },
    ],
}


# "common" is our catch all archive and it ignores things from other archives.
# Verify nothing sneaks into ARCHIVE_FILES without a corresponding exclusion
# rule in the "common" archive.
for k, v in ARCHIVE_FILES.items():
    # Skip mozharness because it isn't staged.
    if k in ('common', 'mozharness'):
        continue

    ignores = set(itertools.chain(*(e.get('ignore', [])
                                  for e in ARCHIVE_FILES['common'])))

    if not any(p.startswith('%s/' % k) for p in ignores):
        raise Exception('"common" ignore list probably should contain %s' % k)


def find_files(archive):
    for entry in ARCHIVE_FILES[archive]:
        source = entry['source']
        base = entry['base']
        pattern = entry['pattern']
        dest = entry.get('dest')
        ignore = list(entry.get('ignore', []))
        ignore.append('**/.mkdir.done')
        ignore.append('**/*.pyc')

        common_kwargs = {
            'find_executables': False,
            'find_dotfiles': True,
            'ignore': ignore,
        }

        finder = FileFinder(os.path.join(source, base), **common_kwargs)

        for p, f in finder.find(pattern):
            if dest:
                p = mozpath.join(dest, p)
            yield p, f


def main(argv):
    parser = argparse.ArgumentParser(
        description='Produce test archives')
    parser.add_argument('archive', help='Which archive to generate')
    parser.add_argument('outputfile', help='File to write output to')

    args = parser.parse_args(argv)

    if not args.outputfile.endswith('.zip'):
        raise Exception('expected zip output file')

    with open(args.outputfile, 'wb') as fh:
        with JarWriter(fileobj=fh, optimize=False) as writer:
            res = find_files(args.archive)
            for p, f in res:
                writer.add(p.encode('utf-8'), f.read(), mode=f.mode)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
