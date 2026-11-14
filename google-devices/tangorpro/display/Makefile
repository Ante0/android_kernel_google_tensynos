M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

KBUILD_OPTIONS += CONFIG_DRM_PANEL_BOE_TS110F5MLG0=m
KBUILD_OPTIONS += CONFIG_DRM_PANEL_CSOT_PPA957DB2D=m

EXTRA_CFLAGS += -DDYNAMIC_DEBUG_MODULE=1
EXTRA_CFLAGS += -I$(ROOT_DIR)/private/google-modules/display/common/include
EXTRA_CFLAGS += -I$(ROOT_DIR)/private/google-modules/display/samsung
EXTRA_CFLAGS += -I$(ROOT_DIR)/private/google-modules/display/samsung/include/uapi
EXTRA_CFLAGS += -Werror

EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/display/samsung/Module.symvers

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)
