#!/bin/sh
# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Checks meson.build the two ways a Meson project takes urdma: installed, found
# through urdma.pc, and fetched by a wrap-git of this checkout's HEAD. Each
# builds tests/meson/consumer.c. The wrapped build must not build urdma's own
# tests, must install the header and urdma.pc by default, and must install
# nothing with skip-install=true. Run from the repository's top.
set -eu

top=$PWD
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

consumer() {
	mkdir -p "$tmp/$1"
	cp tests/meson/meson.build tests/meson/consumer.c "$tmp/$1/"
}

meson setup --prefix="$tmp/usr" "$tmp/b" > /dev/null
meson install -C "$tmp/b" > /dev/null
pc=$(find "$tmp/usr" -name urdma.pc)
test -n "$pc"
test -f "$tmp/usr/include/urdma.h"
if grep -q '^Libs:.*-l' "$pc"; then echo "urdma.pc links a library" >&2; exit 1; fi
consumer installed
PKG_CONFIG_PATH=$(dirname "$pc") meson setup --wrap-mode=nofallback "$tmp/c1" "$tmp/installed" > /dev/null
ninja -C "$tmp/c1" > /dev/null
test -x "$tmp/c1/consumer"

consumer wrapped
mkdir -p "$tmp/wrapped/subprojects"
cat > "$tmp/wrapped/subprojects/urdma.wrap" << WRAP
[wrap-git]
url = $top
revision = $(git rev-parse HEAD)
depth = 1

[provide]
dependency_names = urdma
WRAP
PKG_CONFIG_PATH= meson setup --wrap-mode=forcefallback "$tmp/c2" "$tmp/wrapped" > /dev/null
ninja -C "$tmp/c2" > /dev/null
test -x "$tmp/c2/consumer"
if [ -n "$(find "$tmp/c2/subprojects" -name 'test_version*')" ]; then
	echo "a wrapped urdma built its own tests" >&2
	exit 1
fi
DESTDIR="$tmp/c2-dest" meson install -C "$tmp/c2" > /dev/null
for f in urdma.h urdma.pc; do
	if [ -z "$(find "$tmp/c2-dest" -name "$f")" ]; then
		echo "a wrapped urdma did not install $f" >&2
		exit 1
	fi
done

meson configure -Durdma:skip-install=true "$tmp/c2" > /dev/null
DESTDIR="$tmp/c2-skip" meson install -C "$tmp/c2" > /dev/null
if [ -n "$(find "$tmp/c2-skip" -name 'urdma*' 2> /dev/null)" ]; then
	echo "a wrapped urdma installed with skip-install=true" >&2
	exit 1
fi
echo "meson: installed and wrapped consumers built"
