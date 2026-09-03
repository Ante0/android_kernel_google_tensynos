ifneq ($(KERNEL_SRC),)
        ifneq ($(src),)
                cp_label := $(shell cp $(MODULE_ROOT_DIR)/pcie/core/label.ini $(MODULE_ROOT_DIR)/pcie/core/host_driver_tmi3_label.ini)
        endif
endif

# find device folder
PWRCTL_DEV_FOLDER :=$(shell find -L $(MODULE_ROOT_DIR)/../pwrctl/device/ -maxdepth 1 -mindepth 1 -type d)
PWRCTL_DEV_FOLDER := $(foreach folderd,$(PWRCTL_DEV_FOLDER),$(notdir $(folderd)))
$(info PWRCTL_DEV_FOLDER:$(PWRCTL_DEV_FOLDER))
KO_CODE_PATH := $(if $(filter /%,$(src)),,$(srctree)/)$(src)
$(info KO_CODE_PATH:$(KO_CODE_PATH))
ccflags-y += -I$(KO_CODE_PATH)/../pwrctl/device/$(PWRCTL_DEV_FOLDER)
ccflags-y += -I$(KO_CODE_PATH)/../wwan/gpio_pwrctl/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/pci/controller

SUB_DIR_CORE := $(abspath $(lastword $(MAKEFILE_LIST)))
SRC_PATH := $(shell dirname $(SUB_DIR_CORE))
SUB_DIR_CORE := $(patsubst $(MODULE_ROOT_DIR)/%/,%,$(dir $(SUB_DIR_CORE)))

ccflags-y += -I$(MODULE_ROOT_DIR)/$(SUB_DIR_CORE)/
ccflags-y += -I$(src)/$(SUB_DIR_CORE)/

ifeq ($(DEVICE_MODULES_PATH),)
        DEVICE_MODULES_PATH = $(srctree)
endif

ifneq ($(KERNELRELEASE),)
        $(shell mkdir -p $(src)/$(SUB_DIR_CORE))
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_hw_interface.pl $(src)/$(SUB_DIR_CORE)/mtk_pci_dev_cfg.h "$(label_product)")
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_hw_interface.pl $(src)/$(SUB_DIR_CORE)/mtk_cldma_hw_interface.h "$(label_product)")
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_hw_interface.pl $(src)/$(SUB_DIR_CORE)/mtk_dpmaif_drv_interface.h "$(label_product)")
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_hw_interface.pl $(src)/$(SUB_DIR_CORE)/mtk_ctrl_cfg.h "$(label_product)")
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_hw_interface.pl $(src)/$(SUB_DIR_CORE)/mtk_utility_cfg.h "$(label_product)")
        genfile := $(shell perl $(SRC_PATH)/mtk_auto_gen_build_info.pl $(src)/$(SUB_DIR_CORE)/mtk_build_info.h)
else
        ifneq ($(KERNEL_SRC),)
                extra_symbols += $(COMMON_OUT_DIR)/private/google-modules/radio/mediatek/t900/wwan/pwrctl/Module.symvers
        endif
endif

mtk_pcie-y += $(SUB_DIR_CORE)/mtk_pcie_trace.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_pci.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_trans_ctrl.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_except.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_pm.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_dpmaif.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_cldma.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_dpmaif_drv.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_cldma_drv.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_dpmaif_ring.o
mtk_pcie-y += $(SUB_DIR_CORE)/mtk_frc.o

ifeq ($(CONFIG_ARCH_GOOGLE), y)
        mtk_pcie-y += $(SUB_DIR_CORE)/mtk_google.o
endif
