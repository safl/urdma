#!/bin/sh
# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

# Run urdma-bench's server and client against each other on the device
# URDMA_TEST_DEVICE names, over 127.0.0.1; skipped, with exit code 77, when it
# names none. Sizes and depths that differ from the defaults are covered too.
#
# usage: bench_pair.sh URDMA_BENCH WORKLOAD
set -u
bench=$1
workload=$2

if [ -z "${URDMA_TEST_DEVICE:-}" ]; then
	echo "URDMA_TEST_DEVICE names no RDMA device to test on"
	exit 77
fi

port=18515
pair() {
	"$bench" "$workload" -d "$URDMA_TEST_DEVICE" -p "$port" &
	server=$!
	"$bench" "$workload" -d "$URDMA_TEST_DEVICE" -p "$port" "$@" 127.0.0.1
	client=$?
	# A server whose client never connected waits in accept()
	if [ "$client" -ne 0 ]; then
		kill "$server" 2>/dev/null
	fi
	wait "$server"
	server=$?
	port=$((port + 1))
	if [ "$client" -ne 0 ] || [ "$server" -ne 0 ]; then
		echo "$workload $*: client exit $client, server exit $server"
		exit 1
	fi
}

if [ "$workload" = write-bw ]; then
	pair -n 1000
	pair -n 5 -t 128
	pair -n 300 -s 4097 -t 7
	pair -n 50 -s 1048576 -t 4
else
	pair -n 1000
	pair -n 600 -s 1
	pair -n 100 -s 8192
fi
