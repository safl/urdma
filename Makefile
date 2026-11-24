# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

BUILD_DIR ?= build

.DEFAULT_GOAL := help
.PHONY: help build test consumer install format format-all docs docs-serve bump clean

help:
	@echo "urdma:"
	@echo "  build       meson setup and compile, in $(BUILD_DIR)/"
	@echo "  test        run the tests"
	@echo "  consumer    build a consumer against an installed urdma and through a wrap"
	@echo "  install     install the header and urdma.pc"
	@echo "  format      run the pre-commit hooks on staged changes"
	@echo "  format-all  run the pre-commit hooks on all files"
	@echo "  docs        build the HTML documentation into docs/_build/html"
	@echo "  docs-serve  serve the documentation, rebuilding on change"
	@echo "  bump        set the version, e.g. make bump NEW_VERSION=0.2.0"
	@echo "  clean       remove $(BUILD_DIR)/ and docs/_build/"

build:
	meson setup $(BUILD_DIR) || meson setup --reconfigure $(BUILD_DIR)
	meson compile -C $(BUILD_DIR)

test: build
	meson test -C $(BUILD_DIR) --print-errorlogs

consumer:
	tests/meson/check.sh

install: build
	meson install -C $(BUILD_DIR)

format:
	pre-commit run

format-all:
	pre-commit run --all-files

# Installs the docs tooling with pipx on first use
docs:
	@command -v urdma-docs-build-html >/dev/null 2>&1 || $(MAKE) -C docs deps
	$(MAKE) -C docs html

docs-serve:
	@command -v urdma-docs-serve >/dev/null 2>&1 || $(MAKE) -C docs deps
	$(MAKE) -C docs serve

# The version is in meson.build and in the header's version macros and @version
bump:
ifndef NEW_VERSION
	$(error Usage: make bump NEW_VERSION=x.y.z)
endif
	@echo "$(NEW_VERSION)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$' || { echo "NEW_VERSION must be x.y.z"; exit 1; }
	sed -i "s/^\(  version: '\)[^']*'/\1$(NEW_VERSION)'/" meson.build
	sed -i "s/^\( \* @version \).*/\1$(NEW_VERSION)/" include/urdma.h
	sed -i "s/^\(#define URDMA_VERSION_MAJOR \).*/\1$(word 1,$(subst ., ,$(NEW_VERSION)))/" include/urdma.h
	sed -i "s/^\(#define URDMA_VERSION_MINOR \).*/\1$(word 2,$(subst ., ,$(NEW_VERSION)))/" include/urdma.h
	sed -i "s/^\(#define URDMA_VERSION_PATCH \).*/\1$(word 3,$(subst ., ,$(NEW_VERSION)))/" include/urdma.h
	@grep -n "version: '\|@version\|URDMA_VERSION_" meson.build include/urdma.h

clean:
	rm -rf $(BUILD_DIR) docs/_build
