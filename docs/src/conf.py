# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

"""Sphinx configuration for the urdma documentation"""

from __future__ import annotations

import re
from pathlib import Path

project = "urdma"
author = "Simon A. F. Lund"
copyright = f"2026, {author}"

# The version is meson.build's
_meson = (Path(__file__).resolve().parents[2] / "meson.build").read_text()
_match = re.search(r"version:\s*'([^']+)'", _meson)
release = _match.group(1) if _match else "0.0.0"
version = release

extensions = [
    "myst_parser",
    "sphinx_copybutton",
    "breathe",
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "linkify",
]

source_suffix = {".md": "markdown"}
master_doc = "index"
exclude_patterns = ["_build"]

# Breathe renders the header from the Doxygen XML under docs/_build/doxy
breathe_projects = {project: str(Path(__file__).resolve().parents[1] / "_build" / "doxy" / "xml")}
breathe_default_project = project
breathe_domain_by_extension = {"h": "c"}

html_theme = "furo"
html_title = "urdma"
