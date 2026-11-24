<!--
SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>

SPDX-License-Identifier: BSD-3-Clause
-->

# urdma: RDMA from user space, header-only

**This is an exploration.** There is no RDMA code yet, and nothing here is
settled. The repository records what is being explored and why, and has the
build, checks, documentation and releases in place for the code to come.

urdma is meant to become a header-only C library for moving data with an RDMA
NIC, in the style of [uPCIe](https://github.com/safl/upcie),
[ufuse](https://github.com/safl/ufuse) and
[uTrace](https://github.com/safl/utrace). A project embeds it, for example
[xNVMe](https://github.com/xnvme/xnvme) through a meson wrap. It is not meant
to be packaged.

## What is explored

The data paths of interest run between an RDMA NIC, GPU memory and NVMe: an
RDMA WRITE into VRAM that an NVMe controller then writes to storage, and the
reverse. They build on uPCIe's work with dma-buf, iommufd and vfio-pci.

Two ways to drive the NIC are explored.

The first uses a NIC that the kernel drives, through verbs. The kernel
driver creates the objects, and the data path already runs in user space: the
rdma-core provider maps the queues and the doorbell into the process. GPU
memory is registered as a dma-buf with `ibv_reg_dmabuf_mr()`. This works with
any vendor's NIC, but the NIC addresses memory through the kernel's DMA
mappings, apart from the I/O address space uPCIe sets up for an NVMe
controller.

The second is a user-space driver for a NIC bound to vfio-pci, as uPCIe's
NVMe driver is for an NVMe controller. The NIC then shares one iommufd I/O
address space with the NVMe controller and with GPU memory, and the driver
decides where the queues live. RDMA has no standard hardware interface, so
such a driver is specific to one NIC family. The first candidate is AMD
Pensando's ionic, which [rocm-ernic](https://github.com/safl/rocm-ernic)
emulates and [systems-devenv](https://github.com/refenv/systems-devenv) puts in a
QEMU guest next to an emulated GPU and an NVMe controller.

GPU-initiated transfers are also explored: the queues placed in GPU memory,
and the GPU writing work requests and ringing the NIC's doorbell, as uPCIe
does for NVMe.

## Development

Building, testing, the source checks, the documentation and releasing are
described in the [documentation](https://safl.github.io/urdma/development.html).

## Licensing

BSD-3-Clause, see `LICENSE`.
