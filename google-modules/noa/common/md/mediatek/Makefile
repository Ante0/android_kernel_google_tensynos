# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NOA Mediatek modem driver
#

# NOA Modem Global Constant
GOOGLE_MODULES_PATH=$(ROOT_DIR)/private/google-modules
NOA_BASE_PATH=$(GOOGLE_MODULES_PATH)/noa
NOAH_LIB_PATH=$(NOA_BASE_PATH)/noah_lib
NANOPB_PATH=$(NOA_BASE_PATH)/nanopb
DPA_PATH=$(GOOGLE_MODULES_PATH)/soc/rdo/drivers/soc/google/dpa
RDO_INCLUDE_PATH=$(GOOGLE_MODULES_PATH)/soc/rdo/include
MTK_TMI3_PATH=$(SRC_DIR)/../../radio/mediatek/t900/wwan/tmi3

# Define the relative path to the Mediatek modem driver directory
MTK_MODEM_PATH := md/mediatek

##### NOA Modem Objects #####
MTK_MODEM_OBJ := \
	$(MTK_MODEM_PATH)/noa_md.o \
	$(MTK_MODEM_PATH)/noa_md_cldma.o \
	$(MTK_MODEM_PATH)/noa_md_data_path_ctrl.o \
	$(MTK_MODEM_PATH)/noa_md_dma_mapper.o \
	$(MTK_MODEM_PATH)/noa_md_dpa.o \
	$(MTK_MODEM_PATH)/noa_md_dpmaif.o \
	$(MTK_MODEM_PATH)/noa_md_pcie.o \
	$(MTK_MODEM_PATH)/noa_md_rpc.o \
	$(MTK_MODEM_PATH)/noa_md_rx_data.o \
	$(MTK_MODEM_PATH)/noa_md_trace.o \
	$(MTK_MODEM_PATH)/noa_md_tx_data.o \
	$(MTK_MODEM_PATH)/noa_md_utility.o \
	$(MTK_MODEM_PATH)/noa_md_vpn_tx_data.o \
	$(MTK_MODEM_PATH)/noa_md_wrapper.o \
	$(MTK_MODEM_PATH)/noa_md_wrapper_dpmaif.o \
	$(MTK_MODEM_PATH)/noa_md_wwan_notifier.o

# NOA Modem Common Defines
ifeq ($(CONFIG_NOA_SIM_SUPPORT), m)
ccflags-y += -DCONFIG_NOA_SIM_SUPPORT
endif

ifeq ($(CONFIG_MTK_DATA_CPU_LOADING_OPTIMIZE), y)
ccflags-y += -DCONFIG_DATA_CPU_LOADING_OPTIMIZE
endif

ifeq ($(CONFIG_MTK_DATA_FEATURE_TEST), y)
ccflags-y += -DCONFIG_MTK_DATA_FEATURE_TEST
endif

# NOA modem MTK Support
ifneq ($(CONFIG_NOA_MD_MEDIATEK_SUPPORT),)
ccflags-y += -DCONFIG_NOA_MD_MEDIATEK_SUPPORT

ifneq ($(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT),)
ccflags-y += -DCONFIG_NOA_MD_MEDIATEK_T900_SUPPORT
ccflags-y += -DCONFIG_NOA_MD_MEDIATEK_T900_UNIFIED_DESC
ccflags-y += -DCONFIG_NOA_MD_MEDIATEK_T900_TCP_SLOW_START

# Check if the file exists to enable CONFIG_ENABLE_T900_NOA_SUPPORT
DPMAIF_DRV_FILE := $(MTK_TMI3_PATH)/pcie/core/mtk_dpmaif_drv.h
DPMAIF_DRV_EXISTS := $(shell if test -f $(DPMAIF_DRV_FILE); then echo YES; else echo NO; fi)

# Detect if the T900 driver source has NOA features.
# To do this, we search for the 'NOA_FREE_POOL' keyword in the DPMAIF header.
# This is necessary to support different source branches (with or without NOA,
# e.g., 'android16-gs-pixel-6.12-noa25-dev' vs 'android16-gs-pixel-6.12').
# If the keyword is found, we enable full T900 support for NOA.
ifeq ($(DPMAIF_DRV_EXISTS), YES)
    $(info File $(DPMAIF_DRV_FILE) exists)
    NOA_FREE_POOL_IN_DPMAIF := $(shell grep -q 'NOA_FREE_POOL' $(DPMAIF_DRV_FILE) && echo FOUND)

    ifeq ($(NOA_FREE_POOL_IN_DPMAIF), FOUND)
        $(info Found NOA in $(DPMAIF_DRV_FILE), enabling T900 NOA support)
        ccflags-y += -DCONFIG_ENABLE_T900_NOA_SUPPORT
    else
        $(info NOA not found in $(DPMAIF_DRV_FILE))
    endif
else
    $(info File $(DPMAIF_DRV_FILE) does not exist.)
endif  # DPMAIF_DRV_EXISTS

endif

# T900 Headers
MTK_TMI3_COMMON_HDRS := $(MTK_TMI3_PATH)/common
MTK_TMI3_PCIE_CORE_HDRS := $(MTK_TMI3_PATH)/pcie/core

subdir-ccflags-y += \
	-I$(MTK_TMI3_COMMON_HDRS) \
	-I$(MTK_TMI3_PCIE_CORE_HDRS)

# NOA T900 Modem Objects
MTK_MODEM_OBJ += $(MTK_MODEM_PATH)/noa_md_wrapper_t900.o
endif # CONFIG_NOA_MD_MEDIATEK_SUPPORT

# NOA modem FullSoC Support
ifneq ($(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT),)
ccflags-y += -DCONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT
ccflags-y += -DCONFIG_DATA_CPU_LOADING_OPTIMIZE

# RPC Support
# Add include paths for RPC support
RPC_CFLAGS := \
	-I$(NOAH_LIB_PATH)/pw_rpc_c/public \
	-I$(NOAH_LIB_PATH)/gen/pw_rpc_services \
	-I$(NOAH_LIB_PATH)/pw_rpc_service_client/public \
	-I$(NANOPB_PATH)

# NANOPB system header for Linux
RPC_CFLAGS += -DPB_SYSTEM_HEADER=\"common/pb_linux.h\"

# TODO: b/373772456 - Enable this to get RPC NCP client
# RPC_CFLAGS += \
#	-I$(RDO_INCLUDE_PATH) \
#	-I$(DPA_PATH)

RPC_PATHS := \
	$(NOAH_LIB_PATH) \
	$(NOAH_LIB_PATH)/pw_rpc_c/public \
	$(NOAH_LIB_PATH)/gen/pw_rpc_services \
	$(NOAH_LIB_PATH)/pw_rpc_service_client/public \
	$(NANOPB_PATH) \
	$(DPA_PATH)

check_path = $(if $(shell test -d "$1" && echo yes),yes,)
all_paths_exist := $(foreach path,$(RPC_PATHS),$(call check_path,$(path)))

# Check if all RPC paths exist
ifeq ($(filter-out yes,$(all_paths_exist)),)
$(info Enable NOA MD RPC support)
ccflags-y += $(RPC_CFLAGS)

# TODO: b/373772456 - Enable this for RPC commands
# PW_RPC_PATH := ../noah_lib/pw_rpc_c
# MTK_MODEM_OBJ += \
#	../noah_lib/gen/pw_rpc_services/pw_rpc/echo.pb.o \
#	$(PW_RPC_PATH)/packet.pb.o \
#	$(PW_RPC_PATH)/pw_rpc_call_list.o \
#	$(PW_RPC_PATH)/pw_rpc_client.o \
#	$(PW_RPC_PATH)/pw_rpc_packet.o \
#	$(PW_RPC_PATH)/linux_export.o

endif # RPC paths exist

endif # CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT

noa-m += $(MTK_MODEM_OBJ)

# Include the NOA modem debug module
ifeq ($(CONFIG_DEBUG_FS), y)
	include $(NOA_BASE_PATH)/common/$(MTK_MODEM_PATH)/debug/Makefile
endif
