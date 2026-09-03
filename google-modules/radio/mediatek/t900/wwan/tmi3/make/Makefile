ifneq ($(KERNEL_SRC),)
        CONFIG_MTK_DATA_CPU_LOADING_OPTIMIZE=y
endif

ifeq ($(DEVICE_MODULES_PATH),)
        DEVICE_MODULES_PATH = $(srctree)
endif

MODULE_ROOT_DIR := $(abspath $(lastword $(MAKEFILE_LIST)))
MODULE_ROOT_DIR := $(patsubst %/,%,$(dir $(MODULE_ROOT_DIR)))

TARGET_FOLDERS := $(shell find $(MODULE_ROOT_DIR) -maxdepth 1 -mindepth 1 -type d)
FILTER_FOLDERS := common make out
TARGET_FOLDERS := $(foreach folder,$(TARGET_FOLDERS),$(notdir $(folder)))

ccflags-y += $(extra_flags)

ifeq ($(BUS),)
        BUS := pcie
endif

ifneq ($(BUS),)
        ifneq ($(BUS),all)
                TARGET_FOLDERS := $(BUS)
        else
                TARGET_FOLDERS := $(filter-out $(FILTER_FOLDERS),$(TARGET_FOLDERS))
        endif
else
        TARGET_FOLDERS :=
endif

TARGET_PATH := $(foreach tmp_folder,$(TARGET_FOLDERS),$(wildcard $(MODULE_ROOT_DIR)/$(tmp_folder)))
TARGET_ALL_DIRS := $(shell find -L $(TARGET_PATH) -type d )
TARGET_EXCLUDE_NO_MK := $(foreach dir,$(TARGET_ALL_DIRS),$(if $(wildcard $(dir)/Makefile),$(dir),))
TARGET_CORE := $(foreach tmp_folder,$(TARGET_FOLDERS),$(wildcard $(MODULE_ROOT_DIR)/$(tmp_folder)/core))
TARGET_UT := $(foreach tmp_folder,$(TARGET_FOLDERS),$(wildcard $(MODULE_ROOT_DIR)/$(tmp_folder)/ut))
TARGET_INTERNAL := $(foreach tmp_folder,$(TARGET_FOLDERS),$(wildcard $(MODULE_ROOT_DIR)/internal/$(tmp_folder)/*))
TARGET_EXCLUDE_UT := $(filter-out $(TARGET_UT),$(TARGET_EXCLUDE_NO_MK))
TARGET_EXCLUDE_CORE := $(filter-out $(TARGET_CORE),$(TARGET_EXCLUDE_UT))

ALL_DIR += $(TARGET_EXCLUDE_CORE)
ALL_DIR += $(TARGET_CORE)
ifneq ($(strip $(TARGET_INTERNAL)),)
        ALL_DIR += $(TARGET_INTERNAL)
endif
ALL_DIR += $(MODULE_ROOT_DIR)/common
ccflags-y += -Werror

$(info CONFIG_UBSAN:$(CONFIG_UBSAN))
ifeq ($(CONFIG_UBSAN), y)
        ifeq ($(KERNEL_SRC),)
                ccflags-y += -fsanitize=undefined -fno-sanitize=pointer-overflow,nonnull-attribute,alignment
        else
                ccflags-y += -fsanitize=integer-divide-by-zero
        endif
endif

INCLUDE_FILES := $(patsubst %,%/Makefile,$(ALL_DIR))
TARGET_MODULES := $(patsubst %,mtk_%.o,$(TARGET_FOLDERS))
OUT := out

ifeq ($(CONFIG_MTK_DATA_CPU_LOADING_OPTIMIZE), y)
        ccflags-y += -DCONFIG_DATA_CPU_LOADING_OPTIMIZE
endif

ifeq ($(CONFIG_DATA_GRO_WITHOUT_NAPI_POLL), y)
        ccflags-y += -DCONFIG_DATA_GRO_WITHOUT_NAPI_POLL
endif

ifeq ($(CONFIG_MTK_DATA_FEATURE_TEST), y)
        ccflags-y += -DCONFIG_MTK_DATA_FEATURE_TEST
endif

ifeq ($(CONFIG_MTK_DATA_TEST), y)
        ccflags-y += -DCONFIG_DATA_TEST_MODE
endif

ifeq ($(CONFIG_WWAN), )
        override CONFIG_MTK_HOST_DRV_WWAN_SUPPORT = n
endif

ifeq ($(CONFIG_MTK_HOST_DRV_WWAN_SUPPORT), y)
        ccflags-y += -DCONFIG_MTK_HOST_DRV_WWAN_SUPPORT
        ifeq ($(CONFIG_SUPPORT_RELAYFS), y)
                ccflags-y += -DCONFIG_SUPPORT_RELAYFS
        endif
endif

ifeq ($(CONFIG_MTK_WWAN_PWRCTL_SUPPORT), y)
ccflags-y += -DCONFIG_MTK_WWAN_PWRCTL_SUPPORT
endif

ifeq ($(CONFIG_MTK_MEMLOG_EVENT_SUPPORT), y)
ccflags-y += -DCONFIG_MTK_MEMLOG_EVENT_SUPPORT
endif

GOOGLE_CUSTOM_BASE := private/google-modules/radio/mediatek/t900/wwan/tmi3/pcie/customize/
ccflags-y += -I$(ROOT_DIR)/$(GOOGLE_CUSTOM_BASE)/google/
ccflags-y += -I$(ROOT_DIR)/$(GOOGLE_CUSTOM_BASE)/adsp/

# For google customization
extra_symbols += $(COMMON_OUT_DIR)/$(GOOGLE_CUSTOM_BASE)/google/radio_google_Module.symvers
extra_symbols += $(COMMON_OUT_DIR)/$(GOOGLE_CUSTOM_BASE)/adsp/mtk_pcimsg_google_Module.symvers

ifeq ($(CONFIG_MTK_DEVLINK), y)
        ccflags-y += -DCONFIG_MTK_DEVLINK
        ifeq ($(CONFIG_MTK_DEVLINK_COREDUMP_SUPPORT), y)
                ccflags-y += -DCONFIG_MTK_DEVLINK_COREDUMP_SUPPORT
        endif
        ifeq ($(CONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT), y)
                ccflags-y += -DCONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT
        endif
        ifeq ($(CONFIG_MTK_DEVLINK_FLASH_SUPPORT), y)
                ccflags-y += -DCONFIG_MTK_DEVLINK_FLASH_SUPPORT
        endif
endif

ifneq ($(PRINT_DBG_INFO),)
        $(info DEVICE_MODULES_PATH:$(DEVICE_MODULES_PATH))
        $(info srctree:$(srctree))
        $(info MODULE_ROOT_DIR:$(MODULE_ROOT_DIR))
        $(info KERNEL_SRC:$(KERNEL_SRC))
        $(info src:$(src))
        $(info KERNELRELEASE:$(KERNELRELEASE))
        #$(info TARGET_EXCLUDE_NO_MK:$(TARGET_EXCLUDE_NO_MK))
        #$(info TARGET_EXCLUDE_UT:$(TARGET_EXCLUDE_UT))
        #$(info Target ALL:$(ALL_DIR))
endif

include $(INCLUDE_FILES)
ifneq ($(KERNELRELEASE),)
        obj-m += mtk_common.o
        obj-m += $(TARGET_MODULES)
else
        ifeq ($(KERNEL_SRC),)
                COMPILER = "gcc"
                LINKER = "ld"
                TARGET_ARCH := x86_64

                ifneq ($(KERNEL),)
                        KDIR=$(KERNEL)
                else
                        ifeq ($(KERN_VER),)
                                KDIR=/lib/modules/$(shell uname -r)/build
                        else
                                KDIR=/lib/modules/$(KERN_VER)/build
                        endif
                endif

                define clean_ops
	                make -C $(KDIR) M=$(MODULE_ROOT_DIR) clean
	                rm -rf $(MODULE_ROOT_DIR)/$(OUT)
                endef
                all:
	                @mkdir -p $(MODULE_ROOT_DIR)/$(OUT)/tools/utilities/
	                make -C $(KDIR) M=$(MODULE_ROOT_DIR) modules 2>&1 | tee $(MODULE_ROOT_DIR)/$(OUT)/build.log
	                @echo current dir: $(shell pwd)
	                @echo root dir: $(MODULE_ROOT_DIR)
	                @mv $(MODULE_ROOT_DIR)/*.ko $(MODULE_ROOT_DIR)/$(OUT)/
	                cp -rf $(MODULE_ROOT_DIR)/tools/utilities/*.sh $(MODULE_ROOT_DIR)/$(OUT)/tools/utilities/
                clean:
	                make -C $(KDIR) M=$(MODULE_ROOT_DIR) clean
	                @rm -rf $(MODULE_ROOT_DIR)/$(OUT)
	                @rm -rf $(MODULE_ROOT_DIR)/*.mod
	                @rm -rf $(MODULE_ROOT_DIR)/common/*.mod
	                make -C $(MODULE_ROOT_DIR)/common/ clean
	                @for submake in $(TARGET_CORE); do \
		                make -C $$submake clean; \
	                done
                .PHONY:all clean
                .DEFAULT_GOAL:=all
        else
                #$(info extra_symbols:$(extra_symbols))
                all: PRIVATE_SYMBOLS := $(extra_symbols)

                # Define an "all" target, which isn't really needed but doesn't hurt.
                all: modules

                modules modules_install clean compile_commands.json:
	                $(MAKE) -C $(KERNEL_SRC) M=$(M) $(@) $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(PRIVATE_SYMBOLS)"

                .DEFAULT_GOAL:=all
        endif
endif
