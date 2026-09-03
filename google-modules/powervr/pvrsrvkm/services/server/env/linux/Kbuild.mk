########################################################################### ###
#@File
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
#
# The contents of this file are subject to the MIT license as set out below.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
#
# This License is also included in this distribution in the file called
# "MIT-COPYING".
#
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

# Window system
ccflags-y += -DWINDOW_SYSTEM=\"$(WINDOW_SYSTEM)\"

# For pvrsrvkm.mk
imgtec_kernel_top := $(TOP)/kernel/drivers/staging/imgtec

include $(TOP)/services/server/env/linux/pvrsrvkm.mk

# Ignore address-of-packed-member warning for all bridge files
$(foreach _o,$(addprefix CFLAGS_,$(filter generated/%.o,$($(PVRSRV_MODNAME)-y))),$(eval $(_o) += -Wno-address-of-packed-member))

# With certain build configurations, e.g., ARM, Werror, we get a build
# failure in the ftrace Linux kernel header.  So disable the relevant check.
CFLAGS_services/server/env/linux/trace_events.o := -Wno-missing-prototypes

# Make sure the mem_utils are built in 'free standing' mode, so the compiler
# is not encouraged to call out to C library functions
ifeq ($(CC),clang)
 ifneq ($(SUPPORT_ANDROID_PLATFORM),1)
  CFLAGS_services/shared/common/mem_utils.o := -ffreestanding -fforce-enable-int128
 else
  CFLAGS_services/shared/common/mem_utils.o := -ffreestanding
 endif
endif

# Enable -Werror for all built object files
ifneq ($(W),1)
ccflags-y += -Werror
endif

# Chrome OS kernel adds some issues
ccflags-y += -Wno-ignored-qualifiers

# Treat #warning as a warning
ccflags-y += -Wno-error=cpp
