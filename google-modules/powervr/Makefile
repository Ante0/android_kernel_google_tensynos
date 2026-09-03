
# SPDX-License-Identifier: GPL-2.0-only
KCFLAGS += \
	-I$(ROOT_DIR)/private/google-modules/soc/gs/include \
	-I$(ROOT_DIR)/private/google-modules/soc/rdo/include \
	-I$(ROOT_DIR)/private/google-modules/iif/include \
	-I$(ROOT_DIR)/private/google-modules/perf/core/google_pm_qos/include \

module_syms = $(wildcard $(COMMON_OUT_DIR)/private/google-modules/$(1)Module.symvers)
# We depend on trusty to access the gpu_secure trusted firmware app
EXTRA_SYMBOLS += $(call module_syms,trusty/)
# Needed to execute PT API calls (for SLC)
EXTRA_SYMBOLS += $(call module_syms,soc/gs/)
# Needed for the standalone IIF API
EXTRA_SYMBOLS += $(call module_syms,iif/iif_)
# Needed for goog_gtc_get_counter()
EXTRA_SYMBOLS += $(call module_syms,soc/rdo/)
# Needed for google vote manager API calls
EXTRA_SYMBOLS += $(call module_syms,perf/core/google_pm_qos/google_vote_manager_)

# This file is called by the GKI build system
modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) \
	$(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
