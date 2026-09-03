# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NOA modem debug module
#

NOA_MD_DEBUG_PATH := md/mediatek/debugfs

ifeq ($(CONFIG_DEBUG_FS), y)

NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug.o
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug_ctl.o
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug_dpath.o
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug_shmem.o
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug_skb_pool.o
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/noa_md_debug_vpn_tx.o

ifneq ($(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT),)
NOA_MD_DEBUG_OBJ += $(NOA_MD_DEBUG_PATH)/t900/noa_md_debug_t900_cmd.o
endif

noa-m += $(NOA_MD_DEBUG_OBJ)


# Disables automatic SW_INIT from DPMAIF, allowing manual trigger for
# debugging.
# To trigger SW_INIT manually via debugfs:
#   echo "0" > /d/noa_md/t900_cmd
#
# ccflags-y += -DCONFIG_NOA_MD_DEBUG_DISABLE_DPMAIF_SW_INIT

endif  # CONFIG_DEBUG_FS
