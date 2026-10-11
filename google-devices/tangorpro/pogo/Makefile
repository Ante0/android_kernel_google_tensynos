M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

KBUILD_OPTIONS += CONFIG_POGO_TRANSPORT=m

EXTRA_CFLAGS += -DDYNAMIC_DEBUG_MODULE=1
EXTRA_CFLAGS += -Werror

EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/bms/Module.symvers
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/bms/misc/Module.symvers

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)
