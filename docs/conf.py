# Configuration file for the Sphinx documentation builder.
# Run: cd docs && make html
# Prerequisites: pip install -r docs/requirements.txt && doxygen Doxyfile

import os
import sys

# -- Path setup --------------------------------------------------------------
sys.path.insert(0, os.path.abspath('.'))

# -- Project information -----------------------------------------------------
project = 'aes-stream-drivers'
copyright = '2026, SLAC National Accelerator Laboratory'
author = 'SLAC AES Group'

# Version: CI sets DOCS_VERSION to the git tag (e.g. "v3.2.1")
# Local builds fall back to "dev"
release = os.environ.get('DOCS_VERSION', 'dev').lstrip('v')
version = release

# -- General configuration ---------------------------------------------------
extensions = [
    'breathe',
    'sphinx_rtd_theme',
    'sphinx_copybutton',
]

templates_path = ['_templates']
exclude_patterns = ['_build', '_doxygen', 'Thumbs.db', '.DS_Store']

# -- Breathe configuration ---------------------------------------------------
# Path is relative to conf.py (docs/ directory)
breathe_projects = {
    'aes-stream-drivers': '_doxygen/xml',
}
breathe_default_project = 'aes-stream-drivers'
# Most headers are plain C (no extern "C", no C++ features) and Doxyfile sets
# EXTENSION_MAPPING = h=C / OPTIMIZE_OUTPUT_FOR_C=YES. docs/reference/*.rst
# references them exclusively as :c:func:/:c:type:/:c:macro:, so map .h files
# to the C domain; mapping to cpp would register the entities in the C++ domain
# and silently break every :c:func: cross-reference.
breathe_domain_by_extension = {'h': 'c', 'c': 'c'}
# A few headers carry C++-only features (class declarations, namespaces).
# Override those individually so their classes can be parsed at all — leaving
# them in the C domain trips Breathe with KeyError: 'class'. The pattern is an
# fnmatch glob applied against Doxygen's absolute-path location attribute.
breathe_domain_by_file_pattern = {
    '*/GpuAsyncUser.h': 'cpp',
    '*/GpuAsyncLib.h': 'cpp',
}
breathe_default_members = ('members', 'undoc-members')

# -- sphinx-copybutton configuration -----------------------------------------
copybutton_prompt_text = r'^\$ |^>>> '
copybutton_prompt_is_regexp = True
copybutton_only_copy_prompt_lines = False

# -- HTML output configuration -----------------------------------------------
html_theme = 'sphinx_rtd_theme'

html_theme_options = {
    'navigation_depth': 4,
    'titles_only': False,
    'collapse_navigation': False,
    'sticky_navigation': True,
    'includehidden': True,
    'logo_only': False,
    'prev_next_buttons_location': 'bottom',
}

html_context = {
    'display_github': True,
    'github_user': 'slaclab',
    'github_repo': 'aes-stream-drivers',
    'github_version': 'main',
    'conf_py_path': '/docs/',
    'source_suffix': '.rst',
}

html_static_path = []
