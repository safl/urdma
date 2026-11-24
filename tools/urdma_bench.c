// SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
//
// SPDX-License-Identifier: BSD-3-Clause

// urdma-bench: RDMA WRITE workloads between two processes, with the payloads verified
//
// The workloads and parameters are those of perftest's ib_write_bw and ib_write_lat,
// so the two tools run the same workload on a rig. Run without a host it is the
// server; with one, the client connects to it over TCP, sends the parameters,
// and the two exchange queue pair addresses and memory regions. The client's
// parameters apply to both.
//
// write-bw: the client keeps up to depth WRITEs in flight, into min(iters, depth)
// slots of the server's region. Each slot is written with the pattern of the last
// iteration that targets it, so after the run the server checks every slot.
//
// write-lat: client and server take turns, each WRITE answered by one from the
// other side. The receiver polls the last byte of its region, which carries a
// marker that differs between consecutive iterations, then checks the whole
// payload. The client reports half the round trip, as ib_write_lat does.
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <urdma_verbs.h>

#define MAGIC 0x55524442u // "URDB"
#define PROTO_VERSION 1
#define MSG_LEN 64
#define TIMEOUT_MS 5000
#define CONNECT_RETRY_MS 10000

enum mode { MODE_WRITE_BW = 1, MODE_WRITE_LAT = 2 };

struct args {
	enum mode mode;
	const char *device;
	const char *host;
	const char *port;
	uint32_t size;
	uint32_t iters;
	uint32_t depth;
};

// What each side sends the other once: the workload, its queue pair and its memory region
struct msg {
	uint32_t mode, size, iters, depth;
	struct urdma_verbs_qp_addr qp;
	uint64_t addr;
	uint32_t rkey;
};

static uint64_t
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
put32(uint8_t *p, uint32_t v)
{
	v = htonl(v);
	memcpy(p, &v, 4);
}

static uint32_t
get32(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return ntohl(v);
}

static void
msg_pack(const struct msg *m, uint8_t *buf)
{
	memset(buf, 0, MSG_LEN);
	put32(buf + 0, MAGIC);
	put32(buf + 4, PROTO_VERSION);
	put32(buf + 8, m->mode);
	put32(buf + 12, m->size);
	put32(buf + 16, m->iters);
	put32(buf + 20, m->depth);
	put32(buf + 24, m->qp.qpn);
	put32(buf + 28, m->qp.psn);
	memcpy(buf + 32, m->qp.gid.raw, 16);
	put32(buf + 48, (uint32_t)(m->addr >> 32));
	put32(buf + 52, (uint32_t)m->addr);
	put32(buf + 56, m->rkey);
}

static int
msg_unpack(struct msg *m, const uint8_t *buf)
{
	if (get32(buf) != MAGIC || get32(buf + 4) != PROTO_VERSION) {
		return -EPROTO;
	}
	memset(m, 0, sizeof(*m));
	m->mode = get32(buf + 8);
	m->size = get32(buf + 12);
	m->iters = get32(buf + 16);
	m->depth = get32(buf + 20);
	m->qp.qpn = get32(buf + 24);
	m->qp.psn = get32(buf + 28);
	memcpy(m->qp.gid.raw, buf + 32, 16);
	m->addr = (uint64_t)get32(buf + 48) << 32 | get32(buf + 52);
	m->rkey = get32(buf + 56);

	return 0;
}

static int
sock_send(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len) {
		ssize_t n = send(fd, p, len, MSG_NOSIGNAL);

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -errno;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int
sock_recv(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;

	while (len) {
		ssize_t n = recv(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -errno;
		}
		if (!n) {
			return -ECONNRESET;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

// Accept one client on port, over IPv6 and IPv4 where the host has IPv6, else over IPv4
static int
sock_serve(const char *port)
{
	static const int families[] = {AF_INET6, AF_INET};
	int lfd = -1, fd, one = 1, zero = 0, err = -EAFNOSUPPORT;

	for (size_t i = 0; i < sizeof(families) / sizeof(families[0]) && lfd < 0; ++i) {
		struct addrinfo hints = {0}, *res;
		int gai;

		hints.ai_family = families[i];
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		gai = getaddrinfo(NULL, port, &hints, &res);
		if (gai) {
			fprintf(stderr, "getaddrinfo(port %s): %s\n", port, gai_strerror(gai));
			return -EINVAL;
		}
		lfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (lfd >= 0) {
			setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
			if (families[i] == AF_INET6) {
				setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
			}
			if (bind(lfd, res->ai_addr, res->ai_addrlen) || listen(lfd, 1)) {
				err = -errno;
				close(lfd);
				lfd = -1;
			}
		} else {
			err = -errno;
		}
		freeaddrinfo(res);
	}
	if (lfd < 0) {
		return err;
	}

	fd = accept(lfd, NULL, NULL);
	err = fd < 0 ? -errno : fd;
	close(lfd);

	return err;
}

// Connect to host:port, retrying for CONNECT_RETRY_MS so the server may start after the client
static int
sock_connect(const char *host, const char *port)
{
	uint64_t deadline = now_ns() + CONNECT_RETRY_MS * 1000000ull;
	struct addrinfo hints = {0}, *res, *ai;
	int err;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(host, port, &hints, &res);
	if (err) {
		fprintf(stderr, "getaddrinfo(%s, %s): %s\n", host, port, gai_strerror(err));
		return -EINVAL;
	}
	for (;;) {
		for (ai = res; ai; ai = ai->ai_next) {
			int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

			if (fd < 0) {
				continue;
			}
			if (!connect(fd, ai->ai_addr, ai->ai_addrlen)) {
				freeaddrinfo(res);
				return fd;
			}
			err = -errno;
			close(fd);
		}
		if (now_ns() > deadline) {
			freeaddrinfo(res);
			return err;
		}
		usleep(100000);
	}
}

// The byte at offset j of a WRITE-bw slot: a hash of the slot and the offset, so a WRITE into
// the wrong slot or at the wrong offset is seen
static uint8_t
bw_pattern(uint32_t slot, uint32_t j)
{
	uint32_t h = (slot + 1) * 2654435761u ^ j * 40503u;

	return (uint8_t)(h ^ h >> 8 ^ h >> 16 ^ h >> 24);
}

// The marker in the last byte of a WRITE-lat payload: never 0, and never equal for two
// consecutive iterations
static uint8_t
lat_marker(uint32_t iter)
{
	return (uint8_t)(iter % 255 + 1);
}

static uint8_t
lat_pattern(uint32_t iter, uint32_t j, uint32_t size)
{
	return j == size - 1 ? lat_marker(iter) : (uint8_t)(iter * 31u + j * 7u + 1u);
}

static void
lat_fill(uint8_t *buf, uint32_t iter, uint32_t size)
{
	for (uint32_t j = 0; j < size; ++j) {
		buf[j] = lat_pattern(iter, j, size);
	}
}

// Wait for the marker of iter in the last byte of buf, then check the payload
static int
lat_await(volatile uint8_t *buf, uint32_t iter, uint32_t size)
{
	uint64_t deadline = now_ns() + TIMEOUT_MS * 1000000ull;

	while (buf[size - 1] != lat_marker(iter)) {
		if (now_ns() > deadline) {
			fprintf(stderr, "iteration %u: no WRITE arrived within %d ms\n", iter,
				TIMEOUT_MS);
			return -ETIMEDOUT;
		}
	}
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	for (uint32_t j = 0; j < size; ++j) {
		if (buf[j] != lat_pattern(iter, j, size)) {
			fprintf(stderr, "iteration %u: byte %u is 0x%02x, expected 0x%02x\n", iter,
				j, buf[j], lat_pattern(iter, j, size));
			return -EIO;
		}
	}
	return 0;
}

static int
wait_one(struct urdma_verbs_qp *qp, uint64_t wr_id)
{
	struct ibv_wc wc;
	int err = urdma_verbs_wait(qp, &wc, TIMEOUT_MS);

	if (err) {
		fprintf(stderr, "wr_id %llu: %s; status %s\n", (unsigned long long)wr_id,
			strerror(-err), err == -EIO ? ibv_wc_status_str(wc.status) : "none");
		return err;
	}
	// Completions of an RC send queue arrive in the order the work requests were posted
	if (wc.wr_id != wr_id || wc.opcode != IBV_WC_RDMA_WRITE) {
		fprintf(stderr, "completion of wr_id %llu opcode %d, expected %llu and %d\n",
			(unsigned long long)wc.wr_id, (int)wc.opcode, (unsigned long long)wr_id,
			(int)IBV_WC_RDMA_WRITE);
		return -EIO;
	}
	return 0;
}

static int
cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

static void
print_bw(const struct args *a, uint64_t ns)
{
	double secs = (double)ns / 1e9;
	double bytes = (double)a->size * a->iters;

	printf(" %-10s %-14s %-22s %s\n", "#bytes", "#iterations", "BW average[MiB/sec]",
	       "MsgRate[Mpps]");
	printf(" %-10u %-14u %-22.2f %.6f\n", a->size, a->iters, bytes / secs / (1 << 20),
	       a->iters / secs / 1e6);
}

static void
print_lat(const struct args *a, uint64_t *half_rtt_ns)
{
	uint32_t n = a->iters;
	double sum = 0, var = 0, avg;

	qsort(half_rtt_ns, n, sizeof(*half_rtt_ns), cmp_u64);
	for (uint32_t i = 0; i < n; ++i) {
		sum += (double)half_rtt_ns[i];
	}
	avg = sum / n;
	for (uint32_t i = 0; i < n; ++i) {
		var += ((double)half_rtt_ns[i] - avg) * ((double)half_rtt_ns[i] - avg);
	}

	printf(" %-8s %-12s %-12s %-12s %-16s %-12s %-14s %-22s %s\n", "#bytes", "#iterations",
	       "t_min[usec]", "t_max[usec]", "t_typical[usec]", "t_avg[usec]", "t_stdev[usec]",
	       "99% percentile[usec]", "99.9% percentile[usec]");
	printf(" %-8u %-12u %-12.2f %-12.2f %-16.2f %-12.2f %-14.2f %-22.2f %.2f\n", a->size, n,
	       half_rtt_ns[0] / 1e3, half_rtt_ns[n - 1] / 1e3, half_rtt_ns[n / 2] / 1e3, avg / 1e3,
	       sqrt(var / n) / 1e3, half_rtt_ns[(uint32_t)(n * 0.99)] / 1e3,
	       half_rtt_ns[(uint32_t)(n * 0.999)] / 1e3);
}

// The client's side of write-bw: WRITE every iteration, then ask the server to check the slots
static int
client_bw(struct args *a, struct urdma_verbs_qp *qp, struct ibv_mr *mr, const struct msg *peer,
	  int fd)
{
	uint32_t slots = a->iters < a->depth ? a->iters : a->depth;
	uint8_t *buf = mr->addr, verdict = 1;
	uint32_t posted = 0, completed = 0;
	uint64_t start;
	int err;

	for (uint32_t s = 0; s < slots; ++s) {
		for (uint32_t j = 0; j < a->size; ++j) {
			buf[(size_t)s * a->size + j] = bw_pattern(s, j);
		}
	}

	start = now_ns();
	while (completed < a->iters) {
		while (posted < a->iters && posted - completed < a->depth) {
			size_t off = (size_t)(posted % slots) * a->size;

			err = urdma_verbs_write(qp, mr, buf + off, a->size, peer->addr + off,
						peer->rkey, posted);
			if (err) {
				fprintf(stderr, "urdma_verbs_write(%u): %s\n", posted,
					strerror(-err));
				return err;
			}
			++posted;
		}
		err = wait_one(qp, completed);
		if (err) {
			return err;
		}
		++completed;
	}
	print_bw(a, now_ns() - start);

	err = sock_send(fd, &verdict, 1);
	if (!err) {
		err = sock_recv(fd, &verdict, 1);
	}
	if (err) {
		fprintf(stderr, "server verdict: %s\n", strerror(-err));
		return err;
	}
	if (verdict) {
		fprintf(stderr, "server: the payload of the WRITEs differs from what was sent\n");
		return -EIO;
	}
	printf("payload: verified, %u slots of %u bytes\n", slots, a->size);

	return 0;
}

// The server's side of write-bw: when the client is done, check every slot
static int
server_bw(struct args *a, struct ibv_mr *mr, int fd)
{
	uint32_t slots = a->iters < a->depth ? a->iters : a->depth;
	const uint8_t *buf = mr->addr;
	uint8_t verdict = 0, done;
	int err;

	err = sock_recv(fd, &done, 1);
	if (err) {
		fprintf(stderr, "client: %s\n", strerror(-err));
		return err;
	}
	for (uint32_t s = 0; s < slots && !verdict; ++s) {
		for (uint32_t j = 0; j < a->size; ++j) {
			uint8_t want = bw_pattern(s, j);

			if (buf[(size_t)s * a->size + j] != want) {
				fprintf(stderr, "slot %u byte %u is 0x%02x, expected 0x%02x\n", s,
					j, buf[(size_t)s * a->size + j], want);
				verdict = 1;
				break;
			}
		}
	}
	err = sock_send(fd, &verdict, 1);
	if (err) {
		return err;
	}
	if (verdict) {
		return -EIO;
	}
	printf("payload: verified, %u slots of %u bytes\n", slots, a->size);

	return 0;
}

// write-lat, both sides: the region holds what the peer writes, then what this side sends
static int
run_lat(struct args *a, int client, struct urdma_verbs_qp *qp, struct ibv_mr *mr,
	const struct msg *peer)
{
	uint8_t *rx = mr->addr, *tx = rx + a->size;
	uint64_t *half_rtt_ns = NULL;
	int err = 0;

	if (client) {
		half_rtt_ns = calloc(a->iters, sizeof(*half_rtt_ns));
		if (!half_rtt_ns) {
			return -ENOMEM;
		}
	}
	for (uint32_t i = 0; i < a->iters; ++i) {
		uint64_t start = now_ns();

		if (!client) {
			err = lat_await(rx, i, a->size);
			if (err) {
				break;
			}
		}
		lat_fill(tx, i, a->size);
		err = urdma_verbs_write(qp, mr, tx, a->size, peer->addr, peer->rkey, i);
		if (!err) {
			err = wait_one(qp, i);
		}
		if (err) {
			break;
		}
		if (client) {
			err = lat_await(rx, i, a->size);
			if (err) {
				break;
			}
			half_rtt_ns[i] = (now_ns() - start) / 2;
		}
	}
	if (!err) {
		if (client) {
			print_lat(a, half_rtt_ns);
		}
		printf("payload: verified, %u WRITEs of %u bytes each way\n", a->iters, a->size);
	}
	free(half_rtt_ns);

	return err;
}

static int
run(struct args *a)
{
	struct urdma_verbs_dev dev = {0};
	struct urdma_verbs_qp qp = {0};
	struct ibv_mr *mr = NULL;
	uint8_t raw[MSG_LEN];
	struct msg self = {0}, peer;
	size_t len;
	void *buf = NULL;
	int fd, err;

	fd = a->host ? sock_connect(a->host, a->port) : sock_serve(a->port);
	if (fd < 0) {
		fprintf(stderr, "%s %s: %s\n", a->host ? "connect to" : "listen on",
			a->host ? a->host : a->port, strerror(-fd));
		return fd;
	}

	// The client states the workload; the server takes it, and answers with its addresses
	if (a->host) {
		self.mode = a->mode;
		self.size = a->size;
		self.iters = a->iters;
		self.depth = a->depth;
		msg_pack(&self, raw);
		err = sock_send(fd, raw, MSG_LEN);
		if (err) {
			fprintf(stderr, "server: %s\n", strerror(-err));
			goto out;
		}
	} else {
		err = sock_recv(fd, raw, MSG_LEN);
		if (!err) {
			err = msg_unpack(&peer, raw);
		}
		if (err) {
			fprintf(stderr, "client: %s\n", strerror(-err));
			goto out;
		}
		a->mode = peer.mode;
		a->size = peer.size;
		a->iters = peer.iters;
		a->depth = peer.depth;
		if ((a->mode != MODE_WRITE_BW && a->mode != MODE_WRITE_LAT) || !a->size ||
		    !a->iters || !a->depth) {
			fprintf(stderr, "client asked for mode %u, size %u, iters %u, depth %u\n",
				a->mode, a->size, a->iters, a->depth);
			err = -EINVAL;
			goto out;
		}
	}

	err = urdma_verbs_dev_open(&dev, a->device);
	if (err) {
		fprintf(stderr, "urdma_verbs_dev_open(%s): %s\n", a->device ? a->device : "any",
			strerror(-err));
		goto out;
	}
	if (a->mode == MODE_WRITE_BW) {
		len = (size_t)(a->iters < a->depth ? a->iters : a->depth) * a->size;
	} else {
		len = 2 * (size_t)a->size;
	}
	buf = aligned_alloc(4096, (len + 4095) & ~(size_t)4095);
	if (!buf) {
		err = -ENOMEM;
		goto out;
	}
	memset(buf, 0, len);
	mr = ibv_reg_mr(dev.pd, buf, len, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!mr) {
		err = -errno;
		fprintf(stderr, "ibv_reg_mr(%zu bytes): %s\n", len, strerror(errno));
		goto out;
	}
	err = urdma_verbs_qp_create(&dev, &qp, a->mode == MODE_WRITE_BW ? a->depth : 1);
	if (err) {
		fprintf(stderr, "urdma_verbs_qp_create(): %s\n", strerror(-err));
		goto out;
	}

	urdma_verbs_qp_addr_get(&dev, &qp, &self.qp);
	self.mode = a->mode;
	self.size = a->size;
	self.iters = a->iters;
	self.depth = a->depth;
	self.addr = (uint64_t)(uintptr_t)buf;
	self.rkey = mr->rkey;

	// The server's queue pair is ready to receive before it answers, so the client may
	// WRITE as soon as it has connected its own
	if (a->host) {
		uint8_t req[MSG_LEN];

		msg_pack(&self, req);
		err = sock_send(fd, req, MSG_LEN);
		if (!err) {
			err = sock_recv(fd, raw, MSG_LEN);
		}
		if (!err) {
			err = msg_unpack(&peer, raw);
		}
		if (!err) {
			err = urdma_verbs_qp_connect(&dev, &qp, &peer.qp);
		}
	} else {
		err = sock_recv(fd, raw, MSG_LEN);
		if (!err) {
			err = msg_unpack(&peer, raw);
		}
		if (!err) {
			err = urdma_verbs_qp_connect(&dev, &qp, &peer.qp);
		}
		if (!err) {
			msg_pack(&self, raw);
			err = sock_send(fd, raw, MSG_LEN);
		}
	}
	if (err) {
		fprintf(stderr, "connecting the queue pairs: %s\n", strerror(-err));
		goto out;
	}
	printf("%s %s: %s port %u gid_index %d, peer qpn %u\n", a->host ? "client" : "server",
	       a->mode == MODE_WRITE_BW ? "write-bw" : "write-lat",
	       ibv_get_device_name(dev.ctx->device), (unsigned)dev.port, dev.gid_index,
	       peer.qp.qpn);

	if (a->mode == MODE_WRITE_BW) {
		err = a->host ? client_bw(a, &qp, mr, &peer, fd) : server_bw(a, mr, fd);
	} else {
		err = run_lat(a, a->host != NULL, &qp, mr, &peer);
	}

out:
	urdma_verbs_qp_destroy(&qp);
	if (mr) {
		ibv_dereg_mr(mr);
	}
	free(buf);
	urdma_verbs_dev_close(&dev);
	close(fd);

	return err;
}

static void
usage(FILE *out)
{
	fprintf(out,
		"usage: urdma-bench {write-bw|write-lat} [options] [host]\n"
		"\n"
		"Without a host, serve one client; with one, connect to its server. The\n"
		"client's size, iterations and depth apply to both sides.\n"
		"\n"
		"  -d, --ib-dev NAME   RDMA device, such as ionic_0 (default: the first usable)\n"
		"  -p, --port PORT     TCP port of the exchange (default: 18515)\n"
		"  -s, --size BYTES    bytes per WRITE (default: 65536 for write-bw, 2 for "
		"write-lat)\n"
		"  -n, --iters N       WRITEs (default: 1000)\n"
		"  -t, --tx-depth N    WRITEs in flight, write-bw only (default: 128)\n"
		"  -h, --help\n");
}

static int
parse_u32(const char *s, uint32_t *v)
{
	char *end;
	unsigned long long n;

	errno = 0;
	n = strtoull(s, &end, 0);
	if (errno || *end || !*s || !n || n > UINT32_MAX) {
		return -EINVAL;
	}
	*v = (uint32_t)n;
	return 0;
}

int
main(int argc, char **argv)
{
	static const struct option opts[] = {
		{"ib-dev", required_argument, NULL, 'd'},
		{"port", required_argument, NULL, 'p'},
		{"size", required_argument, NULL, 's'},
		{"iters", required_argument, NULL, 'n'},
		{"tx-depth", required_argument, NULL, 't'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0},
	};
	struct args a = {.port = "18515", .iters = 1000, .depth = 128};
	int c;

	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	if (!strcmp(argv[1], "write-bw")) {
		a.mode = MODE_WRITE_BW;
		a.size = 65536;
	} else if (!strcmp(argv[1], "write-lat")) {
		a.mode = MODE_WRITE_LAT;
		a.size = 2;
	} else {
		fprintf(stderr, "unknown workload '%s'\n", argv[1]);
		usage(stderr);
		return 2;
	}
	optind = 2;
	while ((c = getopt_long(argc, argv, "d:p:s:n:t:h", opts, NULL)) != -1) {
		int err = 0;

		switch (c) {
		case 'd':
			a.device = optarg;
			break;
		case 'p':
			a.port = optarg;
			break;
		case 's':
			err = parse_u32(optarg, &a.size);
			break;
		case 'n':
			err = parse_u32(optarg, &a.iters);
			break;
		case 't':
			err = parse_u32(optarg, &a.depth);
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
		if (err) {
			fprintf(stderr, "-%c %s: expected a number from 1 to %u\n", c, optarg,
				UINT32_MAX);
			return 2;
		}
	}
	if (optind < argc) {
		a.host = argv[optind++];
	}
	if (optind < argc) {
		usage(stderr);
		return 2;
	}

	return run(&a) ? 1 : 0;
}
