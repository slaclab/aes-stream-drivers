#!/usr/bin/env python3
# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Helper script to run clang-tidy on files listed in compile_commands.json
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
import re
import argparse

parser = argparse.ArgumentParser(description='Runs clang-tidy on all files listed in compile_commands.json')
parser.add_argument('-e', dest='CMD', default='clang-tidy', type=str, help='Command to run for clang-tidy')

def main():
    args = parser.parse_args()

    db = {}
    with open('compile_commands.json', 'r') as fp:
        db = json.load(fp)
    
    fail = False
    for o in db:
        print(f'Processing {o["file"]}...')
        r = subprocess.run([args.CMD, '-p=.', o['file']])
        if r.returncode != 0:
            fail = True

    exit(1 if fail else 0)

if __name__ == '__main__':
    main()