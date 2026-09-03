SUB_DIR_M9XX := $(abspath $(lastword $(MAKEFILE_LIST)))
SUB_DIR_M9XX := $(patsubst $(MODULE_ROOT_DIR)/%/,%,$(dir $(SUB_DIR_M9XX)))

ccflags-y += -I$(MODULE_ROOT_DIR)/$(SUB_DIR_M9XX)/

label_product += m9xx_0x0900

mtk_pcie-y += $(SUB_DIR_M9XX)/mtk_pci_drv_m9xx.o
mtk_pcie-y += $(SUB_DIR_M9XX)/mtk_dpmaif_drv_m9xx.o
mtk_pcie-y += $(SUB_DIR_M9XX)/mtk_cldma_drv_m9xx.o
mtk_pcie-y += $(SUB_DIR_M9XX)/mtk_ctrl_cfg_m9xx.o
mtk_pcie-y += $(SUB_DIR_M9XX)/mtk_utility_cfg_m9xx.o
