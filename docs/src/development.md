<!--
SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>

SPDX-License-Identifier: BSD-3-Clause
-->

# Development

## Building and testing

urdma is a single header, `include/urdma.h`. The meson build installs it with
`urdma.pc`, and compiles its tests as C and as C++:

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
guest.

```bash
make devenv-verify   # start the host container, the guest, and test urdma in it
make devenv-down     # remove the host container and the guest
```

`make devenv-verify` needs Docker or podman and `/dev/kvm`. With rootless
podman, add `DEVENV_RUN_ARGS="--group-add keep-groups"` so the container keeps
the `kvm` group. The staged guest image, about 20 GB with its download, is
kept in `.devenv/` between runs, or in `DEVENV_HOME` when that is set. CI runs
the same targets in its `guest` job.

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
