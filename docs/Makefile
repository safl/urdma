# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

.DEFAULT_GOAL := help

.PHONY: help deps html serve clean

help:
	@echo "urdma docs:"
	@echo "  deps    pipx install ./tooling  (provides urdma-docs-{serve,build-html})"
	@echo "  html    urdma-docs-build-html   -> _build/html/"
	@echo "  serve   urdma-docs-serve        (live rebuild on http://localhost:8000)"
	@echo "  clean   remove _build/"

deps:
	pipx install --force ./tooling

html:
	urdma-docs-build-html

serve:
	urdma-docs-serve

clean:
	rm -rf _build
