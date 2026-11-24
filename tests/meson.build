# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

# The same test compiled as C and as C++, which is also what shows the header works from both
version_arg = '-DURDMA_TEST_VERSION="@0@"'.format(meson.project_version())
test('version-c', executable('test_version_c', 'test_version.c', dependencies: urdma_dep,
                             c_args: version_arg))
# Skipped unless URDMA_TEST_DEVICE names an RDMA device
if ibverbs_dep.found()
  test('verbs-loopback', executable('test_verbs_loopback', 'test_verbs_loopback.c',
                                    dependencies: [urdma_dep, ibverbs_dep]),
       timeout: 60)
  foreach workload : ['write-bw', 'write-lat']
    test('bench-' + workload, find_program('bench_pair.sh'),
         args: [urdma_bench, workload], is_parallel: false, timeout: 120)
  endforeach
endif
if add_languages('cpp', required: false, native: false)
  test('version-cxx', executable('test_version_cxx', 'test_version_cxx.cpp', dependencies: urdma_dep,
                                 cpp_args: version_arg, override_options: ['cpp_std=c++17']))
  if ibverbs_dep.found()
    test('verbs-cxx', executable('test_verbs_cxx', 'test_verbs_cxx.cpp',
                                 dependencies: [urdma_dep, ibverbs_dep],
                                 override_options: ['cpp_std=c++17']))
  endif
endif
