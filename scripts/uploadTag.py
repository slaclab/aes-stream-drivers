# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Python code that called during Github Actions to upload tag
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

import sys
import os
import argparse
import github  # Importing PyGithub for GitHub API interaction

# Initialize the argument parser with a description
parser = argparse.ArgumentParser(description='Download a release from GitHub')

# Add arguments for the GitHub repository, tag of the release, and file to upload
parser.add_argument(
    "--repo",
    type=str,
    required=True,
    help="GitHub repository in the format 'username/repository'"
)

parser.add_argument(
    "--tag",
    type=str,
    required=True,
    help="Tag of the release to download"
)

parser.add_argument(
    "--file",
    type=str,
    required=True,
    help="Path to the file to upload"
)

# Parse the arguments from the command line
args = parser.parse_args()

print("\nLogging into GitHub...\n")

# Retrieve GitHub token from environment variables
token = os.environ.get('GH_REPO_TOKEN')

# Exit if the GitHub token is not found
if token is None:
    sys.exit("Failed to get GitHub token from GH_REPO_TOKEN environment variable")

# Authenticate with GitHub using the provided token
gh = github.Github(token)

# Retrieve the specified repository
try:
    repo = gh.get_repo(args.repo)
    release = repo.get_release(args.tag)
    release.upload_asset(args.file)
    print(f"Successfully uploaded {args.file} to release {args.tag} in repository {args.repo}.")
except Exception as e:
    sys.exit(f"Failed to find the release '{args.tag}' or upload the file '{args.file}': {e}")
