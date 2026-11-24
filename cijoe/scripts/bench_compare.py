#!/usr/bin/env python3
# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause
"""
Run a workload with urdma-bench and with perftest
=================================================

Runs the workload, write-bw or write-lat, as a server and a client on the
target over 127.0.0.1, first with urdma-bench, which verifies the payloads,
then with perftest's ib_write_bw or ib_write_lat, with the same size,
iterations and depth, on the GID index urdma-bench picked. Fails when either
side of either tool fails. On an emulated NIC the bandwidth and latency say
nothing about a NIC's performance.

The device is 'device', else the first in /sys/class/infiniband.

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
    parser.add_argument("--bench", type=str, default="/tmp/urdma/build/tools/urdma-bench")
    parser.add_argument("--port", type=int, default=18515)
    parser.add_argument("--timeout", type=int, default=120)


def pair(cijoe, tag, server, client, timeout, settle=0):
    """Run server in the background and client in the foreground; fail if either fails"""

    run = f"/tmp/urdma-bench-{tag}"
    cijoe.run(f"rm -f {run}.log {run}.rc {run}.pid")
    # setsid detaches the server from the SSH session, so this command returns
    err, state = cijoe.run(
        f"setsid sh -c '{server} > {run}.log 2>&1; echo $? > {run}.rc'"
        f" < /dev/null > /dev/null 2>&1 & echo $! > {run}.pid"
    )
    if err:
        log.error(f"{tag}: starting the server failed")
        return err, state
    if settle:
        cijoe.run(f"sleep {settle}")

    err, state = cijoe.run(f"timeout {timeout} {client}")
    if err:
        log.error(f"{tag}: the client failed")
        cijoe.run(f"kill -- -$(cat {run}.pid); cat {run}.log")
        return err, state

    err, _ = cijoe.run(
        f"timeout 30 sh -c 'while [ ! -f {run}.rc ]; do sleep 0.1; done';" f" cat {run}.log; exit $(cat {run}.rc)"
    )
    if err:
        log.error(f"{tag}: the server failed")

    return err, state


def main(args, cijoe):
    """Run the workload with urdma-bench, then with perftest"""

    device = args.device
    if not device:
        err, state = cijoe.run("ls /sys/class/infiniband | head -n1")
        device = state.output().strip() if not err else ""
        if not device:
            log.error("the target has no RDMA device")
            return errno.ENODEV

    opts = f"-n {args.iters}"
    if args.size:
        opts += f" -s {args.size}"
    if args.workload == "write-bw":
        opts += f" -t {args.depth}"

    port = args.port
    bench = f"{args.bench} {args.workload} -d {device} -p {port}"
    err, state = pair(
        cijoe,
        f"{args.workload}-urdma",
        bench,
        f"{bench} {opts} 127.0.0.1",
        args.timeout,
    )
    if err:
        return err

    found = re.search(r"gid_index (\d+),", state.output())
    if not found:
        log.error("urdma-bench did not print the GID index it used")
        return errno.EIO
    gid = found.group(1)

    # perftest's client does not retry while its server starts listening
    perftest = f"{PERFTEST[args.workload]} -d {device} -x {gid} -p {port + 1}"
    err, _ = pair(
        cijoe,
        f"{args.workload}-perftest",
        f"{perftest} {opts}",
        f"{perftest} {opts} 127.0.0.1",
        args.timeout,
        settle=1,
    )

    return err
