// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

// Built by check.sh against an installed urdma and through a wrap of the checkout
#include <stdio.h>
#include <urdma.h>

int
main(void)
{
	printf("urdma %d.%d.%d\n", URDMA_VERSION_MAJOR, URDMA_VERSION_MINOR, URDMA_VERSION_PATCH);
	return 0;
}
