// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

// The version macros of urdma.h match the version meson.build gives, passed in as
// URDMA_TEST_VERSION
#include <stdio.h>
#include <string.h>
#include <urdma.h>

#define STR_(x) #x
#define STR(x) STR_(x)

int
main(void)
{
	const char *header =
		STR(URDMA_VERSION_MAJOR) "." STR(URDMA_VERSION_MINOR) "." STR(URDMA_VERSION_PATCH);

	if (strcmp(header, URDMA_TEST_VERSION)) {
		printf("urdma.h has version %s, meson.build %s\n", header, URDMA_TEST_VERSION);
		return 1;
	}
	return 0;
}
