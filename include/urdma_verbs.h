// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * urdma verbs: RDMA through a NIC the kernel drives
 * =================================================
 *
 * A device opened through libibverbs, reliable-connected queue pairs, and RDMA
 * WRITE between them. The kernel driver creates the objects; the rdma-core
 * provider maps the queues and the doorbell into the process, so posting and
 * polling do not enter the kernel.
 *
 * A queue pair is connected to a peer by the peer's address, struct
 * urdma_verbs_qp_addr, which the two sides exchange by any means. The two may
 * be in one process, as the loopback test connects them.
 *
 * The caller registers memory with ibv_reg_mr() on the device's protection
 * domain, and links libibverbs. Functions return 0 or a negative errno.
 *
 * @file urdma_verbs.h
 */
#ifndef URDMA_VERBS_H
#define URDMA_VERBS_H

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <infiniband/verbs.h>
#include <urdma.h>

/** An opened RDMA device: its context, a protection domain, and the port and GID used */
struct urdma_verbs_dev {
	struct ibv_context *ctx; ///< The device context
	struct ibv_pd *pd;       ///< Protection domain for queue pairs and memory regions
	uint8_t port;            ///< The port used, the first one
	int gid_index;           ///< Index of the port's GID that addresses the queue pairs
	union ibv_gid gid;       ///< The GID at gid_index
	enum ibv_mtu mtu;        ///< The port's active MTU
};

/** A queue pair and the completion queue of both its queues */
struct urdma_verbs_qp {
	struct ibv_cq *cq; ///< Completion queue of the send and the receive queue
	struct ibv_qp *qp; ///< The reliable-connected queue pair
};

/** What a peer needs to connect a queue pair to this one */
struct urdma_verbs_qp_addr {
	uint32_t qpn;      ///< Queue pair number
	uint32_t psn;      ///< First packet sequence number this queue pair sends
	union ibv_gid gid; ///< GID of the port the queue pair is on
};

/**
 * Pick the GID that addresses the port: the first RoCE v2 GID, else the first one set
 *
 * A RoCE v2 GID is routable, so it also reaches a peer on another subnet.
 */
static inline int
urdma_verbs_gid_pick(struct urdma_verbs_dev *dev, const struct ibv_port_attr *pattr)
{
	int fallback = -1;

	for (int i = 0; i < pattr->gid_tbl_len; ++i) {
		struct ibv_gid_entry entry;

		if (ibv_query_gid_ex(dev->ctx, dev->port, (uint32_t)i, &entry, 0)) {
			continue;
		}
		if (!entry.gid.global.subnet_prefix && !entry.gid.global.interface_id) {
			continue;
		}
		if (entry.gid_type == IBV_GID_TYPE_ROCE_V2) {
			dev->gid_index = i;
			dev->gid = entry.gid;
			return 0;
		}
		if (fallback < 0) {
			fallback = i;
		}
	}
	if (fallback < 0) {
		return -ENOENT;
	}
	dev->gid_index = fallback;

	return ibv_query_gid(dev->ctx, dev->port, fallback, &dev->gid) ? -errno : 0;
}

/** Close what urdma_verbs_dev_open() opened; also takes a partly opened device */
static inline void
urdma_verbs_dev_close(struct urdma_verbs_dev *dev)
{
	if (dev->pd) {
		ibv_dealloc_pd(dev->pd);
	}
	if (dev->ctx) {
		ibv_close_device(dev->ctx);
	}
	memset(dev, 0, sizeof(*dev));
}

/** Open device on dev, query its first port, pick a GID and allocate a PD; need_active asks for an
 * active port */
static inline int
urdma_verbs_dev_open_one(struct urdma_verbs_dev *dev, struct ibv_device *device, int need_active)
{
	struct ibv_port_attr pattr;
	int err;

	memset(dev, 0, sizeof(*dev));

	dev->ctx = ibv_open_device(device);
	if (!dev->ctx) {
		return errno ? -errno : -EIO;
	}

	dev->port = 1;
	if (ibv_query_port(dev->ctx, dev->port, &pattr)) {
		err = -errno;
		goto fail;
	}
	if (need_active && pattr.state != IBV_PORT_ACTIVE) {
		err = -ENETDOWN;
		goto fail;
	}
	dev->mtu = pattr.active_mtu;

	err = urdma_verbs_gid_pick(dev, &pattr);
	if (err) {
		goto fail;
	}

	dev->pd = ibv_alloc_pd(dev->ctx);
	if (!dev->pd) {
		err = errno ? -errno : -ENOMEM;
		goto fail;
	}

	return 0;

fail:
	urdma_verbs_dev_close(dev);
	return err;
}

/**
 * Open an RDMA device and allocate a protection domain on it
 *
 * Without a name, the first device whose first port is active and has a GID set
 * is taken; a device that cannot carry RDMA, such as the Ethernet function of a
 * NIC whose port has no GID, is passed over.
 *
 * @param dev Filled on success
 * @param name The device's name, such as "ionic_0", or NULL for the first usable device
 * @return 0, -ENODEV when there is no such device, or another negative errno
 */
static inline int
urdma_verbs_dev_open(struct urdma_verbs_dev *dev, const char *name)
{
	struct ibv_device **list;
	int ndevs = 0, err = -ENODEV;

	memset(dev, 0, sizeof(*dev));

	// NULL also where the kernel has no RDMA support, with ENOSYS, or no
	// /sys/class/infiniband, with ENOENT: either way there is no device
	list = ibv_get_device_list(&ndevs);
	if (!list) {
		return errno == ENOMEM ? -ENOMEM : -ENODEV;
	}
	for (int i = 0; i < ndevs; ++i) {
		if (name) {
			if (!strcmp(ibv_get_device_name(list[i]), name)) {
				err = urdma_verbs_dev_open_one(dev, list[i], 0);
				break;
			}
			continue;
		}
		if (!urdma_verbs_dev_open_one(dev, list[i], 1)) {
			err = 0;
			break;
		}
	}
	ibv_free_device_list(list);

	return err;
}

/** Destroy what urdma_verbs_qp_create() created; also takes a partly created queue pair */
static inline void
urdma_verbs_qp_destroy(struct urdma_verbs_qp *qp)
{
	if (qp->qp) {
		ibv_destroy_qp(qp->qp);
	}
	if (qp->cq) {
		ibv_destroy_cq(qp->cq);
	}
	memset(qp, 0, sizeof(*qp));
}

/**
 * Create a reliable-connected queue pair and its completion queue, in the RESET state
 *
 * @param dev The device opened with urdma_verbs_dev_open()
 * @param qp Filled with the queue pair and its completion queue
 * @param depth How many work requests each queue holds, and the completion queue twice that
 * @return 0, or a negative errno
 */
static inline int
urdma_verbs_qp_create(struct urdma_verbs_dev *dev, struct urdma_verbs_qp *qp, uint32_t depth)
{
	struct ibv_qp_init_attr attr;
	int err;

	memset(qp, 0, sizeof(*qp));

	qp->cq = ibv_create_cq(dev->ctx, (int)(2 * depth), NULL, NULL, 0);
	if (!qp->cq) {
		return errno ? -errno : -ENOMEM;
	}

	memset(&attr, 0, sizeof(attr));
	attr.send_cq = qp->cq;
	attr.recv_cq = qp->cq;
	attr.qp_type = IBV_QPT_RC;
	attr.sq_sig_all = 0;
	attr.cap.max_send_wr = depth;
	attr.cap.max_recv_wr = depth;
	attr.cap.max_send_sge = 1;
	attr.cap.max_recv_sge = 1;

	qp->qp = ibv_create_qp(dev->pd, &attr);
	if (!qp->qp) {
		err = errno ? -errno : -ENOMEM;
		urdma_verbs_qp_destroy(qp);
		return err;
	}

	return 0;
}

/** The address a peer connects to this queue pair with; it sends from PSN 0 */
static inline void
urdma_verbs_qp_addr_get(const struct urdma_verbs_dev *dev, const struct urdma_verbs_qp *qp,
			struct urdma_verbs_qp_addr *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->qpn = qp->qp->qp_num;
	addr->psn = 0;
	addr->gid = dev->gid;
}

/**
 * Connect a queue pair to a peer: from RESET through INIT and RTR to RTS
 *
 * The peer is addressed by its GID alone, with a global route header, as RoCE requires.
 * Remote peers may write to and read from the memory regions that allow it.
 */
static inline int
urdma_verbs_qp_connect(struct urdma_verbs_dev *dev, struct urdma_verbs_qp *qp,
		       const struct urdma_verbs_qp_addr *remote)
{
	struct ibv_qp_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = 0;
	attr.port_num = dev->port;
	attr.qp_access_flags =
		IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	if (ibv_modify_qp(qp->qp, &attr,
			  IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
		return -errno;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = dev->mtu;
	attr.dest_qp_num = remote->qpn;
	attr.rq_psn = remote->psn;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer = 12;
	attr.ah_attr.is_global = 1;
	attr.ah_attr.grh.dgid = remote->gid;
	attr.ah_attr.grh.sgid_index = (uint8_t)dev->gid_index;
	attr.ah_attr.grh.hop_limit = 64;
	attr.ah_attr.port_num = dev->port;
	if (ibv_modify_qp(qp->qp, &attr,
			  IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
				  IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
				  IBV_QP_MIN_RNR_TIMER)) {
		return -errno;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.retry_cnt = 7;
	attr.rnr_retry = 7;
	attr.sq_psn = 0;
	attr.max_rd_atomic = 1;
	if (ibv_modify_qp(qp->qp, &attr,
			  IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
				  IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
		return -errno;
	}

	return 0;
}

/**
 * Post an RDMA WRITE of nbytes at laddr, in mr, to raddr in the peer's region of rkey
 *
 * The completion is signaled, with wr_id; wait for it with urdma_verbs_wait().
 */
static inline int
urdma_verbs_write(struct urdma_verbs_qp *qp, struct ibv_mr *mr, void *laddr, uint32_t nbytes,
		  uint64_t raddr, uint32_t rkey, uint64_t wr_id)
{
	struct ibv_sge sge;
	struct ibv_send_wr wr, *bad = NULL;

	sge.addr = (uint64_t)(uintptr_t)laddr;
	sge.length = nbytes;
	sge.lkey = mr->lkey;

	memset(&wr, 0, sizeof(wr));
	wr.wr_id = wr_id;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.remote_addr = raddr;
	wr.wr.rdma.rkey = rkey;

	return -ibv_post_send(qp->qp, &wr, &bad);
}

/**
 * Poll the completion queue until one completion arrives, or timeout_ms passes
 *
 * @param qp The queue pair whose completion queue is polled
 * @param wc Filled with the completion
 * @param timeout_ms How long to poll before returning -ETIMEDOUT
 * @return 0, -ETIMEDOUT, or -EIO when the work request failed; wc->status says how
 */
static inline int
urdma_verbs_wait(struct urdma_verbs_qp *qp, struct ibv_wc *wc, int timeout_ms)
{
	struct timespec start, now;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		int n = ibv_poll_cq(qp->cq, 1, wc);

		if (n < 0) {
			return -EIO;
		}
		if (n) {
			return wc->status == IBV_WC_SUCCESS ? 0 : -EIO;
		}

		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 >=
		    timeout_ms) {
			return -ETIMEDOUT;
		}
	}
}

#endif /* URDMA_VERBS_H */
