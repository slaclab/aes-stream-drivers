#!/usr/bin/env python3
# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Python script used to filter unsupported arguments out of a generated
#     compile_commands.json file.
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------
import json
import subprocess
import argparse
import os
import re

parser = argparse.ArgumentParser(description='A super simple Python script to strip unsupported arguments out of a compile_commands.json')
parser.add_argument('-f', type=str, default='compile_commands.json', help='Input file')
parser.add_argument('--clang', type=str, default='clang', help='Clang executable to query')
parser.add_argument('-w', dest='WRITE', action='store_true', help='Write in place instead of dumping to stdout')
parser.add_argument('--strip', action='append', help='Arguments to strip (only arg names, not the whole values)')
args = parser.parse_args()

to_strip = [
    '-mrecord-mcount'
]

if not os.path.exists(args.f):
    print(f'Input file {args.f} does not exist')

cdb = {}
with open(args.f, 'r') as fp:
    cdb = json.load(fp)

allargs = subprocess.run([args.clang, '--help'], capture_output=True).stdout.decode()
supportedargs = [x[0].strip() for x in re.findall(r'\n\s+(-[-A-z0-9]+)(\s?)', allargs)]


def is_special_arg(x: str) -> bool:
    """
    Returns true if x is a preserved argument
    """
    return x.startswith('-I') or x.startswith('-D') or x.startswith('-U')


def is_stripped(x: str) -> bool:
    """
    Returns true if x should be stripped from the argument list
    """
    if args.strip is None:
        args.strip = {}

    ag = x.split()[0]
    return ag in args.strip or ag in to_strip

for command in cdb:
    command['arguments'] = [x for x in command['arguments'] if (not x.startswith('-') or x.split('=')[0] in supportedargs or is_special_arg(x)) and not is_stripped(x)]

if not args.WRITE:
    print(json.dumps(cdb, indent=2))
else:
    with open(args.f, 'w') as fp:
        json.dump(cdb, fp, indent=2)
