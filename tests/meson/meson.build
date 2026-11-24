# SPDX-FileCopyrightText: Simon A. F. Lund <os@safl.dk>
#
# SPDX-License-Identifier: BSD-3-Clause

# A consumer of urdma, built by check.sh against an installed urdma and through a wrap of the checkout
project('urdma-consumer', 'c', default_options: ['c_std=gnu11', 'warning_level=3', 'werror=true'])

executable('consumer', 'consumer.c', dependencies: dependency('urdma'))
