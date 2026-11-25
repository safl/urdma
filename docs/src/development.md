<!--
SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>

SPDX-License-Identifier: BSD-3-Clause
-->

# Development

## Building and testing

urdma is header-only. `include/urdma.h` carries the version, and
`include/urdma_verbs.h` drives a NIC the kernel drives through libibverbs,
which a project including it links. The meson build installs both with
`urdma.pc`, and compiles its tests as C and as C++. The tests of
`urdma_verbs.h` are built where libibverbs is found, or always with
`MESON_SETUP_ARGS=-Dverbs=enabled`; on Debian and Ubuntu it comes with
`libibverbs-dev`. The loopback test and the urdma-bench tests run on the RDMA
device that `URDMA_TEST_DEVICE` names, such as `ionic_0`, and are skipped when it
names none:

```bash
make build      # meson setup and compile, in build/
make test       # meson test
make consumer   # build a consumer against an installed urdma and through a wrap
```

A meson project embeds urdma with a wrap such as:

```ini
[wrap-git]
url = https://github.com/safl/urdma.git
revision = v0.1.0
depth = 1

[provide]
dependency_names = urdma
```

The project then takes `dependency('urdma')`. With
`-Durdma:skip-install=true`, nothing of urdma is installed into its prefix.

## Testing in a guest with an RDMA NIC

urdma is also built and tested in a QEMU guest with an emulated AMD Pensando
ionic NIC, so no RDMA hardware is needed. The guest image and the host
container that boots it come from
[systems-devenv](https://github.com/refenv/systems-devenv); the NIC is served
by rocm-ernic, which the host container carries. The rig, `cijoe/rigs/urdma-nic.toml`,
gives the guest one NIC whose loopback backend connects queue pairs within the
guest. There, the loopback test connects two queue pairs and RDMA WRITEs between
them, on the guest's RDMA device, which the guest task names. `make guest-bench`
then runs urdma-bench and perftest with the same workloads in the guest.

```bash
make devenv-verify   # start the host container, the guest, and test urdma in it
make devenv-down     # remove the host container and the guest
```

`make devenv-verify` needs Docker or podman and `/dev/kvm`. With rootless
podman, add `DEVENV_RUN_ARGS="--group-add keep-groups"` so the container keeps
the `kvm` group. The staged guest image, about 20 GB with its download, is
kept in `.devenv/` between runs, or in `DEVENV_HOME` when that is set. CI runs
the same targets in its `guest` job.

The rig `cijoe/rigs/urdma-pair.toml` boots two guests, `urdma` and `peer`, each
with an emulated NIC, the two NICs connected through rocm-ernic's TCP mesh and
their Ethernet on one host bridge, so RDMA WRITEs go from one guest's NIC to the
other's. `make pair` starts both guests and `make pair-bench` builds urdma in
both and runs urdma-bench and perftest with the server in `peer` and the client
in `urdma`:

```bash
make devenv-verify-pair   # start the host container and both guests, run the workloads between them
```

The guests take 4 GB of memory and 4 CPUs each. CI runs the same targets in its
`guest-pair` job.

## RDMA WRITE workloads with urdma-bench

`urdma-bench` runs the RDMA WRITE workloads of perftest's `ib_write_bw` and
`ib_write_lat`, with the same parameters and defaults, between a server and a
client, and verifies every payload. It is built with the verbs path. A run with
no host serves one client; the client's size, iterations and depth apply to
both sides:

```bash
urdma-bench write-bw -d ionic_0 &                 # server
urdma-bench write-bw -d ionic_0 -s 4096 127.0.0.1  # client
urdma-bench write-lat -d ionic_0 &
urdma-bench write-lat -d ionic_0 127.0.0.1
```

`write-bw` keeps up to `--tx-depth` WRITEs in flight into as many slots of the
server's memory, and the server then checks every slot. `write-lat` alternates
one WRITE each way, checking each payload when it arrives, and reports half the
round trip, as `ib_write_lat` does. urdma is tested on an emulated NIC only,
where the bandwidth and latency say nothing about a NIC's performance; there
the workloads verify that RDMA WRITE moves data correctly.

## Format and source checks

The checks are pre-commit hooks: clang-format for C, YAML formatting,
actionlint for the workflows, black and ruff for the documentation's Python,
and REUSE, which requires every file to state its copyright and license.

```bash
make format       # the hooks on staged changes
make format-all   # the hooks on all files, as CI runs them
```

## Documentation

The documentation is Sphinx with MyST Markdown, and the API reference is
rendered from the header by Doxygen and Breathe. `make docs` installs the
tooling with pipx on first use and builds the HTML into `docs/_build/html`,
failing on any warning. CI publishes it to GitHub Pages from main.

## Releasing

A release is a pull request whose last commit is `ver: bump to vX.Y.Z`, made
with `make bump NEW_VERSION=X.Y.Z`, and which adds a `## X.Y.Z` section to
`CHANGELOG.md`. Merged by rebase, the commit releases X.Y.Z through
[gha-project-auto-release](https://github.com/refenv/gha-project-auto-release):
it checks that the pull request's checks passed on the tree main now has, tags
the commit and creates the GitHub Release with the changelog section as its
notes.
