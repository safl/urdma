// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

// Two queue pairs on one device, connected to each other, and an RDMA WRITE
// from one's memory to the other's, on the device URDMA_TEST_DEVICE names.
// Skipped, with meson's exit code 77, when it names none: a machine may have an
// RDMA device that cannot connect RC queue pairs, as the MANA NIC of some
// GitHub-hosted runners cannot, so the device is named where it is known to work.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <urdma_verbs.h>

#define NBYTES (64 * 1024)
#define TIMEOUT_MS 5000

int
main(void)
{
	const char *name = getenv("URDMA_TEST_DEVICE");
	struct urdma_verbs_dev dev;
	struct urdma_verbs_qp a = {0}, b = {0};
	struct urdma_verbs_qp_addr a_addr, b_addr;
	struct ibv_mr *src_mr = NULL, *dst_mr = NULL;
	struct ibv_wc wc;
	char *src = NULL, *dst = NULL;
	int err, ret = 1;

	if (!name || !*name) {
		printf("URDMA_TEST_DEVICE names no RDMA device to test on\n");
		return 77;
	}
	err = urdma_verbs_dev_open(&dev, name);
	if (err) {
		printf("urdma_verbs_dev_open(%s): %s\n", name, strerror(-err));
		return 1;
	}
	printf("%s port %u gid_index %d mtu %d\n", ibv_get_device_name(dev.ctx->device),
	       (unsigned)dev.port, dev.gid_index, (int)dev.mtu);

	src = aligned_alloc(4096, NBYTES);
	dst = aligned_alloc(4096, NBYTES);
	if (!src || !dst) {
		printf("aligned_alloc() failed\n");
		goto out;
	}
	for (int i = 0; i < NBYTES; ++i) {
		src[i] = (char)(i * 7 + 1);
	}
	memset(dst, 0, NBYTES);

	src_mr = ibv_reg_mr(dev.pd, src, NBYTES, IBV_ACCESS_LOCAL_WRITE);
	dst_mr = ibv_reg_mr(dev.pd, dst, NBYTES, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!src_mr || !dst_mr) {
		printf("ibv_reg_mr(): %s\n", strerror(errno));
		goto out;
	}

	err = urdma_verbs_qp_create(&dev, &a, 16);
	if (!err) {
		err = urdma_verbs_qp_create(&dev, &b, 16);
	}
	if (err) {
		printf("urdma_verbs_qp_create(): %s\n", strerror(-err));
		goto out;
	}
	urdma_verbs_qp_addr_get(&dev, &a, &a_addr);
	urdma_verbs_qp_addr_get(&dev, &b, &b_addr);

	err = urdma_verbs_qp_connect(&dev, &a, &b_addr);
	if (!err) {
		err = urdma_verbs_qp_connect(&dev, &b, &a_addr);
	}
	if (err) {
		printf("urdma_verbs_qp_connect(): %s\n", strerror(-err));
		goto out;
	}

	err = urdma_verbs_write(&a, src_mr, src, NBYTES, (uint64_t)(uintptr_t)dst, dst_mr->rkey,
				1);
	if (err) {
		printf("urdma_verbs_write(): %s\n", strerror(-err));
		goto out;
	}
	err = urdma_verbs_wait(&a, &wc, TIMEOUT_MS);
	if (err) {
		printf("urdma_verbs_wait(): %s; status %s\n", strerror(-err),
		       err == -EIO ? ibv_wc_status_str(wc.status) : "none");
		goto out;
	}
	if (wc.wr_id != 1 || wc.opcode != IBV_WC_RDMA_WRITE) {
		printf("completion of wr_id %llu opcode %d, expected 1 and %d\n",
		       (unsigned long long)wc.wr_id, (int)wc.opcode, (int)IBV_WC_RDMA_WRITE);
		goto out;
	}
	if (memcmp(src, dst, NBYTES)) {
		for (int i = 0; i < NBYTES; ++i) {
			if (src[i] != dst[i]) {
				printf("dst differs from src first at byte %d\n", i);
				break;
			}
		}
		goto out;
	}
	printf("RDMA WRITE of %d bytes: ok\n", NBYTES);
	ret = 0;

out:
	urdma_verbs_qp_destroy(&b);
	urdma_verbs_qp_destroy(&a);
	if (dst_mr) {
		ibv_dereg_mr(dst_mr);
	}
	if (src_mr) {
		ibv_dereg_mr(src_mr);
	}
	free(dst);
	free(src);
	urdma_verbs_dev_close(&dev);

	return ret;
}
