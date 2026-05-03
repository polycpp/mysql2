# Sphinx configuration for the mysql2 companion library.
#
# This file is intentionally static - no CMake templating - so the docs
# build independently of the C++ toolchain. The version is read from
# ../../VERSION.txt at the repo root so CI doesn't need to know CMake.

import os, sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE / '_ext'))

project = 'mysql2'
copyright = '2026, polycpp contributors'
author = 'polycpp contributors'

_version_file = _HERE.parent.parent / 'VERSION.txt'
version = _version_file.read_text().strip() if _version_file.exists() else '0.1.0'
release = version

extensions = [
    'breathe',
    'cpp_toc_overloads',
    'sphinx_copybutton',
    'sphinx_design',
    'sphinx_sitemap',
]

# Doxygen XML lives next to this conf.py in build/doxygen/xml.
# build.py creates it before invoking sphinx-build.
breathe_projects = {
    project: str(_HERE.parent / 'build' / 'doxygen' / 'xml'),
}
breathe_default_project = project
breathe_domain_by_extension = {'hpp': 'cpp', 'h': 'cpp'}

# Duplicate declarations happen when we split a class into @name groups.
suppress_warnings = ['duplicate_declaration.cpp', 'docutils']

primary_domain = 'cpp'
highlight_language = 'cpp'
default_role = 'any'

templates_path = []
exclude_patterns = ['_build']

# Theme
html_theme = 'furo'
html_theme_options = {
    'light_css_variables': {
        'color-brand-primary': '#2962FF',
        'color-brand-content': '#2962FF',
    },
    'dark_css_variables': {
        'color-brand-primary': '#82B1FF',
        'color-brand-content': '#82B1FF',
    },
    'sidebar_hide_name': False,
    'navigation_with_keys': True,
    'source_repository': 'https://github.com/polycpp/mysql2',
    'source_branch': 'main',
    'source_directory': 'docs/sphinx/',
}
html_static_path = ['_static']
html_css_files = ['custom.css']
html_title = f"{project} {version}"
html_copy_source = False
html_baseurl = 'https://polycpp.github.io/mysql2/'

# Intersphinx to the base polycpp docs. Optional and commented by default
# until polycpp publishes an objects.inv; swap the URL once it is live.
# intersphinx_mapping = {
#     'polycpp': ('https://enricohuang.github.io/polycpp/', None),
# }
