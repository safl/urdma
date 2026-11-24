# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

BUILD_DIR ?= build
# Further 'meson setup' options, e.g. -Dverbs=enabled where libibverbs must be found
MESON_SETUP_ARGS ?=
# Where source-archive writes urdma-src.tar.gz; tasks/test.yaml takes it from
# /tmp/artifacts, where devenv-host mounts this
ARTIFACTS ?= /tmp/artifacts
# The rig the guest boots with, from cijoe/rigs/
RIG ?= urdma-nic
CIJOE_OUTPUT ?= cijoe-output
# The guest is started by systems-devenv's rig scripts, which its host container
# carries along with QEMU, cijoe and the device servers; devenv-* run there
DEVENV_IMAGE ?= ghcr.io/refenv/systems-devenv/host:v0.19.0
DEVENV_ENGINE ?= docker
DEVENV_NAME ?= urdma-devenv-$(notdir $(CURDIR))
DEVENV_HOME ?= $(CURDIR)/.devenv
# Further 'run' options; rootless podman needs '--group-add keep-groups' to
# keep the kvm group that gives access to /dev/kvm
DEVENV_RUN_ARGS ?=

.DEFAULT_GOAL := help
.PHONY: help build test consumer install format format-all docs docs-serve bump clean \
	source-archive guest guest-test guest-bench devenv-host devenv-verify devenv-down

help:
	@echo "urdma:"
	@echo "  build       meson setup and compile, in $(BUILD_DIR)/"
	@echo "  test        run the tests"
	@echo "  consumer    build a consumer against an installed urdma and through a wrap"
	@echo "  guest       start the QEMU guest with RIG=$(RIG), in systems-devenv's host container"
	@echo "  guest-test  build and test urdma in the running guest"
	@echo "  guest-bench run urdma-bench and perftest in the guest, after guest-test"
	@echo "  devenv-verify  source-archive, guest, guest-test and guest-bench, in the host container"
	@echo "  devenv-down    remove the host container and the guest in it"
	@echo "  install     install the headers, urdma.pc and urdma-bench"
	@echo "  format      run the pre-commit hooks on staged changes"
	@echo "  format-all  run the pre-commit hooks on all files"
	@echo "  docs        build the HTML documentation into docs/_build/html"
	@echo "  docs-serve  serve the documentation, rebuilding on change"
	@echo "  bump        set the version, e.g. make bump NEW_VERSION=0.2.0"
	@echo "  clean       remove $(BUILD_DIR)/ and docs/_build/"

build:
	meson setup $(MESON_SETUP_ARGS) $(BUILD_DIR) || meson setup --reconfigure $(MESON_SETUP_ARGS) $(BUILD_DIR)
	meson compile -C $(BUILD_DIR)

test: build
	meson test -C $(BUILD_DIR) --print-errorlogs

consumer:
	tests/meson/check.sh

# The tracked files as they are in the working tree, uncommitted changes included
source-archive:
	mkdir -p $(ARTIFACTS)
	git ls-files -z | tar --null -T - -czf $(ARTIFACTS)/urdma-src.tar.gz

guest:
	cd cijoe && cijoe tasks/guest.yaml --monitor --config configs/guest.toml \
		--config rigs/$(RIG).toml --output $(CIJOE_OUTPUT)-guest

guest-test:
	cd cijoe && cijoe tasks/test.yaml --monitor --config configs/guest.toml \
		--output $(CIJOE_OUTPUT)-test

guest-bench:
	cd cijoe && cijoe tasks/bench.yaml --monitor --config configs/guest.toml \
		--output $(CIJOE_OUTPUT)-bench

# DEVENV_HOME keeps the staged guest image between runs
devenv-host:
	mkdir -p $(DEVENV_HOME) $(ARTIFACTS)
	$(DEVENV_ENGINE) inspect $(DEVENV_NAME) >/dev/null 2>&1 || \
		$(DEVENV_ENGINE) run -d --name $(DEVENV_NAME) --privileged --ulimit memlock=-1 $(DEVENV_RUN_ARGS) \
			-v $(DEVENV_HOME):/root -v $(CURDIR):/root/git/urdma -v $(ARTIFACTS):/tmp/artifacts \
			-w /root/git/urdma $(DEVENV_IMAGE) sleep infinity

devenv-verify: source-archive devenv-host
	$(DEVENV_ENGINE) exec $(DEVENV_NAME) make guest guest-test guest-bench RIG=$(RIG)

devenv-down:
	$(DEVENV_ENGINE) rm -f $(DEVENV_NAME)

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
