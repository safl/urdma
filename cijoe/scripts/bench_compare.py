#!/usr/bin/env python3
# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause
"""
Run a workload with urdma-bench and with perftest
=================================================

Runs the workload, write-bw or write-lat, as a server and a client, first with
urdma-bench, which verifies the payloads, then with perftest's ib_write_bw or
ib_write_lat, with the same size, iterations and depth, on the GID index
urdma-bench picked. Fails when either side of either tool fails. On an
emulated NIC the bandwidth and latency say nothing about a NIC's performance.

The client runs on the target. The server runs on the target 'peer', which the
client reaches at 'peer_addr', else on the target itself, reached at
127.0.0.1. The server runs as a transient systemd unit, so it outlives the
SSH session that starts it. Each side uses 'device', else the first in its
/sys/class/infiniband.

Retargetable: true
------------------
"""

import errno
import logging as log
import re
from argparse import ArgumentParser

PERFTEST = {"write-bw": "ib_write_bw", "write-lat": "ib_write_lat"}


def add_args(parser: ArgumentParser):
    parser.add_argument("--workload", choices=sorted(PERFTEST), required=True)
    parser.add_argument("--size", type=int, default=None)
    parser.add_argument("--iters", type=int, default=1000)
    parser.add_argument("--depth", type=int, default=128, help="write-bw only")
    parser.add_argument("--device", type=str, default=None)
    parser.add_argument("--peer", type=str, default=None, help="target the server runs on")
    parser.add_argument("--peer_addr", type=str, default=None, help="address the client reaches the peer at")
    parser.add_argument("--bench", type=str, default="/tmp/urdma/build/tools/urdma-bench")
    parser.add_argument("--port", type=int, default=18515)
    parser.add_argument("--timeout", type=int, default=120)


def device_of(cijoe, device, target):
    """Return 'device', else the first RDMA device on 'target', else None"""

    if device:
        return device
    err, state = cijoe.run("ls /sys/class/infiniband | head -n1", target=target)
    return (state.output().strip() or None) if not err else None


def pair(cijoe, tag, server, client, timeout, peer=None, settle=0):
    """Run server as a systemd unit on 'peer' and client in the foreground; fail if either fails"""

    unit = f"urdma-bench-{tag}"
    run = f"/tmp/{unit}"
    cijoe.run(
        f"systemctl stop {unit} 2>/dev/null; systemctl reset-failed {unit} 2>/dev/null; rm -f {run}.log {run}.rc",
        target=peer,
    )
    err, state = cijoe.run(
        f"systemd-run --collect --unit={unit} sh -c '{server} > {run}.log 2>&1; echo $? > {run}.rc'",
        target=peer,
    )
    if err:
        log.error(f"{tag}: starting the server failed")
        return err, state
    if settle:
        cijoe.run(f"sleep {settle}")

    err, state = cijoe.run(f"timeout {timeout} {client}")
    if err:
        log.error(f"{tag}: the client failed")
        cijoe.run(f"systemctl stop {unit}; cat {run}.log", target=peer)
        return err, state

    err, _ = cijoe.run(
        f"timeout 30 sh -c 'while [ ! -f {run}.rc ]; do sleep 0.1; done'; cat {run}.log; exit $(cat {run}.rc)",
        target=peer,
    )
    if err:
        log.error(f"{tag}: the server failed")

    return err, state


def main(args, cijoe):
    """Run the workload with urdma-bench, then with perftest"""

    if bool(args.peer) != bool(args.peer_addr):
        log.error("'peer' and 'peer_addr' go together")
        return errno.EINVAL
    host = args.peer_addr or "127.0.0.1"

    device = device_of(cijoe, args.device, None)
    peer_device = device_of(cijoe, args.device, args.peer)
    if not device or not peer_device:
        log.error("the client or the server has no RDMA device")
        return errno.ENODEV

    opts = f"-n {args.iters}"
    if args.size:
        opts += f" -s {args.size}"
    if args.workload == "write-bw":
        opts += f" -t {args.depth}"

    port = args.port
    bench = f"{args.bench} {args.workload} -p {port}"
    err, state = pair(
        cijoe,
        f"{args.workload}-urdma",
        f"{bench} -d {peer_device}",
        f"{bench} -d {device} {opts} {host}",
        args.timeout,
        peer=args.peer,
    )
    if err:
        return err

    found = re.search(r"gid_index (\d+),", state.output())
    if not found:
        log.error("urdma-bench did not print the GID index it used")
        return errno.EIO
    gid = found.group(1)

    # perftest's client does not retry while its server starts listening
    perftest = f"{PERFTEST[args.workload]} -x {gid} -p {port + 1}"
    err, _ = pair(
        cijoe,
        f"{args.workload}-perftest",
        f"{perftest} -d {peer_device} {opts}",
        f"{perftest} -d {device} {opts} {host}",
        args.timeout,
        peer=args.peer,
        settle=1,
    )

    return err
