// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

// urdma_verbs.h compiles as C++; opening a device that does not exist needs no RDMA device
#include <cstdio>
#include <urdma_verbs.h>

int
main()
{
	struct urdma_verbs_dev dev;
	int err = urdma_verbs_dev_open(&dev, "urdma-no-such-device");

	if (err != -ENODEV) {
		std::printf("urdma_verbs_dev_open() of no such device returned %d\n", err);
		return 1;
	}
	return 0;
}
