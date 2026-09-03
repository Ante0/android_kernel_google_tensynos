CUR_DIR := $(abspath $(lastword $(MAKEFILE_LIST)))
CUR_DIR := $(patsubst %/,%,$(dir $(CUR_DIR)))
CUR_FLODER := $(notdir $(CUR_DIR))

ccflags-y += -I$(CUR_DIR)/
ifeq ($(CONFIG_SUPPORT_DBG), y)
ccflags-y += -DDEBUG
endif

mtk_common-y += $(CUR_FLODER)/mtk_dev.o
mtk_common-y += $(CUR_FLODER)/mtk_debug.o
mtk_common-y += $(CUR_FLODER)/mtk_ctrl_plane.o
mtk_common-y += $(CUR_FLODER)/mtk_port.o
mtk_common-y += $(CUR_FLODER)/mtk_port_io.o
mtk_common-y += $(CUR_FLODER)/mtk_bm.o
mtk_common-y += $(CUR_FLODER)/mtk_ctrl_plane.o
mtk_common-y += $(CUR_FLODER)/mtk_trace.o
mtk_common-y += $(CUR_FLODER)/mtk_fsm.o
mtk_common-y += $(CUR_FLODER)/mtk_memlog.o
mtk_common-y += $(CUR_FLODER)/mtk_ethtool.o
mtk_common-y += $(CUR_FLODER)/mtk_data_plane.o
mtk_common-y += $(CUR_FLODER)/mtk_statistics.o

ifeq ($(CONFIG_DEBUG_FS), y)
mtk_common-y += $(CUR_FLODER)/mtk_debugfs.o
endif

ifeq ($(CONFIG_MTK_DEVLINK), y)
mtk_common-y += $(CUR_FLODER)/mtk_devlink.o
endif

ifeq ($(CONFIG_MTK_HOST_DRV_WWAN_SUPPORT), y)
mtk_common-y += $(CUR_FLODER)/mtk_wwan.o
else
mtk_common-y += $(CUR_FLODER)/mtk_wwan_legacy.o
endif
