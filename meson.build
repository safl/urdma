# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

project('urdma', 'c',
  version: '0.1.0',
  license: 'BSD-3-Clause',
  meson_version: '>=0.59',
  default_options: [
    'c_std=gnu11',
    'warning_level=3',
    'werror=true',
    'buildtype=release',
  ],
)

inc = include_directories('include')
urdma_dep = declare_dependency(include_directories: inc)
meson.override_dependency('urdma', urdma_dep)

# A parent project that takes urdma as a subproject only to build against it
# sets skip-install=true, so nothing of urdma lands in its prefix
if not get_option('skip-install')
  install_headers('include/urdma.h', 'include/urdma_verbs.h')
  pkg = import('pkgconfig')
  pkg.generate(
    name: 'urdma',
    filebase: 'urdma',
    description: 'RDMA from user space, header-only',
    url: 'https://github.com/safl/urdma',
  )
endif

# The verbs path, the tools and their tests are built where libibverbs is found
ibverbs_dep = dependency('libibverbs', required: get_option('verbs'))

# A project taking urdma through a wrap gets the dependency, not the tools or the tests
if not meson.is_subproject()
  subdir('tools')
  subdir('tests')
endif
