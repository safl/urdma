# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

if ibverbs_dep.found()
  urdma_bench = executable('urdma-bench', 'urdma_bench.c',
    dependencies: [urdma_dep, ibverbs_dep, meson.get_compiler('c').find_library('m')],
    install: not get_option('skip-install'),
  )
endif
