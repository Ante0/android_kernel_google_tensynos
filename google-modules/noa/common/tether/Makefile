# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NOA Controller driver
#

obj-$(CONFIG_NOA_TETHERING_OFFLOAD_SUPPORT) += noa_cont.o

noa_cont-$(CONFIG_NOA_TETHERING_OFFLOAD_SUPPORT) += noa_tethering_manager.o
noa_cont-$(CONFIG_NOA_TETHERING_OFFLOAD_SUPPORT) += noa_netengine_manager.o

EXTRA_CFLAGS += -g
EXTRA_CFLAGS += -I$(srctree)/drivers/soc/google/noa
EXTRA_CFLAGS += -I$(srctree)/drivers/soc/google/noa/sim
EXTRA_CFLAGS += -I$(srctree)/drivers/soc/google/noa/sim/common
EXTRA_CFLAGS += -I$(srctree)/drivers/soc/google/noa/tether
EXTRA_CFLAGS += -I$(srctree)/drivers/soc/google/noa/tether/vpn/public

ccflags-y := $(EXTRA_CFLAGS)
