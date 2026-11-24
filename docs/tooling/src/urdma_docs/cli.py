# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

"""Console-script entry points for the urdma docs tooling"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def _docs_root() -> Path:
    """The directory holding src/conf.py: the working directory, or its parent from docs/tooling"""
    cwd = Path.cwd()
    for candidate in (cwd, cwd.parent):
        if (candidate / "src" / "conf.py").exists():
            return candidate
    sys.exit("urdma-docs: could not find src/conf.py; run from the docs directory")


def _run_doxygen(root: Path) -> None:
    """Generate the Doxygen XML that Breathe renders into the API reference"""
    # Doxygen does not create a nested OUTPUT_DIRECTORY, and _build is absent on a clean checkout
    (root / "_build" / "doxy").mkdir(parents=True, exist_ok=True)
    subprocess.run(["doxygen", "doxy.cfg"], cwd=root, check=True)


def build_html() -> None:
    """Build the HTML documentation into _build/html, failing on any warning"""
    root = _docs_root()
    _run_doxygen(root)
    subprocess.run(
        [sys.executable, "-m", "sphinx", "-W", "-b", "html", str(root / "src"), str(root / "_build" / "html")],
        check=True,
    )


def serve() -> None:
    """Serve the HTML documentation on http://localhost:8000, rebuilding it on change"""
    root = _docs_root()
    _run_doxygen(root)
    subprocess.run(
        [sys.executable, "-m", "sphinx_autobuild", "--port", "8000", str(root / "src"), str(root / "_build" / "html")],
        check=True,
    )
