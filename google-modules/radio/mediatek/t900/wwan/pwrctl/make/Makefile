# SPDX-License-Identifier: GPL-2.0 only.

# Copyright (c) 2023 MediaTek Inc.

# for debug Link error
ifeq ($(CONFIG_MTK_PCIE_LINK_ERR_DEBUG_SUPPORT), y)
ccflags-y += -DCONFIG_MTK_PCIE_LINK_ERR_DEBUG_SUPPORT
endif
# for test
ifeq ($(CONFIG_MTK_THINMD_SLT_ENABLE), y)
ccflags-y += -DCONFIG_MTK_THINMD_SLT_ENABLE
endif

CUR_DIR := $(abspath $(lastword $(MAKEFILE_LIST)))
CUR_DIR := $(patsubst %/,%,$(dir $(CUR_DIR)))

ifneq ($(KERNEL_SRC),)
##################################################################################################1
# find device and interface folder
PWRCTL_DEV_FOLDER :=$(shell find -L $(CUR_DIR)/device/ -maxdepth 1 -mindepth 1 -type d)
PWRCTL_INTERFACE_FOLDER :=$(shell find $(CUR_DIR)/interface/ -maxdepth 1 -mindepth 1 -type d)
PWRCTL_DEV_FOLDER := $(foreach folderd,$(PWRCTL_DEV_FOLDER),$(notdir $(folderd)))
PWRCTL_INTERFACE_FOLDER := $(foreach folderi,$(PWRCTL_INTERFACE_FOLDER),$(notdir $(folderi)))

ccflags-y += -I$(KERNEL_SRC)/$(DEVICE_MODULES_REL_DIR)/drivers/pci/controller/
ccflags-y += -I$(CUR_DIR)/../../gpio_sap_ctrl/
ccflags-y += -I$(srctree)/include/
ccflags-y += -I$(CUR_DIR)/

GOOGLE_CUSTOM_BASE := private/google-modules/radio/mediatek/t900/wwan/tmi3/pcie/customize/google
ccflags-y += -I$(ROOT_DIR)/$(GOOGLE_CUSTOM_BASE)/

ccflags-y += -mcmodel=large
ccflags-y += -Werror
ccflags-y += -fsanitize=integer-divide-by-zero

#module
MODULE_NAME := wwan_gpio_pwrctl
obj-m += ${MODULE_NAME}.o

MTK_COMMON_OBJS := $(notdir $(wildcard $(CUR_DIR)/common/*.c))
MTK_COMMON_OBJS := $(addprefix common/,$(MTK_COMMON_OBJS:.c=.o))
${MODULE_NAME}-objs = $(MTK_COMMON_OBJS)
ccflags-y += -I$(CUR_DIR)/common/

# interface
MTK_INTERFACE_OBJS := $(notdir $(wildcard $(CUR_DIR)/interface/$(PWRCTL_INTERFACE_FOLDER)/*.c))
MTK_INTERFACE_OBJS := $(addprefix interface/$(PWRCTL_INTERFACE_FOLDER)/,$(MTK_INTERFACE_OBJS:.c=.o))
${MODULE_NAME}-objs += $(MTK_INTERFACE_OBJS)
ccflags-y += -I$(CUR_DIR)/interface/$(PWRCTL_INTERFACE_FOLDER)/

#device
MTK_DEV_OBJS := $(notdir $(wildcard $(CUR_DIR)/device/$(PWRCTL_DEV_FOLDER)/*.c))
MTK_DEV_OBJS := $(addprefix device/$(PWRCTL_DEV_FOLDER)/,$(MTK_DEV_OBJS:.c=.o))
${MODULE_NAME}-objs += $(MTK_DEV_OBJS)
ccflags-y += -I$(CUR_DIR)/device/$(PWRCTL_DEV_FOLDER)/

#test
ifeq ($(CONFIG_MTK_THINMD_SLT_ENABLE), y)
ifneq ($(CONFIG_MTK_THINMD_SLT_PWRCTL_FOLDER),)
MTK_TEST_OBJS := $(notdir $(wildcard $(CUR_DIR)/../$(CONFIG_MTK_THINMD_SLT_PWRCTL_FOLDER)/*.c))
MTK_TEST_OBJS := $(addprefix ../$(CONFIG_MTK_THINMD_SLT_PWRCTL_FOLDER)/,$(MTK_TEST_OBJS:.c=.o))
$(info "MTK_TEST_OBJS=$(MTK_TEST_OBJS)")
${MODULE_NAME}-objs += $(MTK_TEST_OBJS)
ccflags-y += -I$(CUR_DIR)/../$(CONFIG_MTK_THINMD_SLT_PWRCTL_FOLDER)/
endif
endif

$(info OUT_DIR=$(OUT_DIR))
$(info KERNEL_SRC=$(KERNEL_SRC))
ifneq ($(wildcard $(KERNEL_SRC)/$(DEVICE_MODULES_REL_DIR)/Makefile.include),)
include $(wildcard $(KERNEL_SRC)/$(DEVICE_MODULES_REL_DIR)/Makefile.include)
endif

# metrics collection
ifneq ($(CONFIG_METRICS_COLLECTION_FRAMEWORK),)
ccflags-y += -I$(ROOT_DIR)/private/google-modules/radio/common/metrics_collection/include
endif

ifneq ($(wildcard $(COMMON_OUT_DIR)/private/google-modules/radio/common/metrics_collection/metrics_collection_enabled_Module.symvers), )
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/radio/common/metrics_collection/metrics_collection_enabled_Module.symvers
endif

export AUTOCONF_H=$(0)include/generated/autoconf.h
ifneq ($(wildcard $(COMMON_OUT_DIR)/vendor/mediatek/kernel_modules/gpio_sap_ctrl/Module.symvers), )
EXTRA_SYMBOLS +=  $(COMMON_OUT_DIR)/vendor/mediatek/kernel_modules/gpio_sap_ctrl/Module.symvers
endif

EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/pci/google-pcie-v2_Module.symvers
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/radio/mediatek/t900/wwan/tmi3/pcie/customize/google/radio_google_Module.symvers

# Define an "all" target, which isn't really needed but doesn't hurt.
all: modules

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(@) $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)"

else
##################################################################################################2
$(info "-----------CONFIG_TARGET_SUBTARGET = $(CONFIG_TARGET_SUBTARGET)")
$(info "the INTERFACE_FOLDER=$(PWRCTL_INTERFACE_FOLDER)")
$(info "the PWRCTL_DEV_FOLDER=$(PWRCTL_DEV_FOLDER)")

ccflags-y += -I$(CUR_DIR)/

#module
MODULE_NAME := wwan_gpio_pwrctl
obj-m += ${MODULE_NAME}.o

#common
MTK_COMMON_OBJS := $(notdir $(wildcard $(CUR_DIR)/common/*.c))
MTK_COMMON_OBJS := $(addprefix common/,$(MTK_COMMON_OBJS:.c=.o))
${MODULE_NAME}-objs = $(MTK_COMMON_OBJS)
ccflags-y += -I$(CUR_DIR)/common/

#interface
MTK_INTERFACE_OBJS := $(notdir $(wildcard $(CUR_DIR)/interface/$(PWRCTL_INTERFACE_FOLDER)/*.c))
MTK_INTERFACE_OBJS := $(addprefix interface/$(PWRCTL_INTERFACE_FOLDER)/,$(MTK_INTERFACE_OBJS:.c=.o))
${MODULE_NAME}-objs += $(MTK_INTERFACE_OBJS)
ccflags-y += -I$(CUR_DIR)/interface/$(PWRCTL_INTERFACE_FOLDER)/

#device
MTK_DEV_OBJS := $(notdir $(wildcard $(CUR_DIR)/device/$(PWRCTL_DEV_FOLDER)/*.c))
MTK_DEV_OBJS := $(addprefix device/$(PWRCTL_DEV_FOLDER)/,$(MTK_DEV_OBJS:.c=.o))
${MODULE_NAME}-objs += $(MTK_DEV_OBJS)
ccflags-y += -I$(CUR_DIR)/device/$(PWRCTL_DEV_FOLDER)/

#test
ifeq ($(CONFIG_MTK_THINMD_SLT_ENABLE), y)
ifneq ($(PWRCTL_TEST_FOLDER),)
MTK_TEST_OBJS := $(notdir $(wildcard $(CUR_DIR)/../$(PWRCTL_TEST_FOLDER)/*.c))
MTK_TEST_OBJS := $(addprefix ../$(PWRCTL_TEST_FOLDER)/,$(MTK_TEST_OBJS:.c=.o))
$(info "MTK_TEST_OBJS=$(MTK_TEST_OBJS)")
${MODULE_NAME}-objs += $(MTK_TEST_OBJS)
ccflags-y += -I$(CUR_DIR)/../$(PWRCTL_TEST_FOLDER)/
endif
endif

endif #KERNEL_SRC
