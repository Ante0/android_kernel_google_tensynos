########################################################################### ###
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

ifeq ($(PVR_KBUILD_OUT_OF_TREE),1)
_nulldisp_obj_prefix := kernel/drivers/staging/imgtec/nulldisp/

ccflags-y += -I$(TOP)/kernel/drivers/staging/imgtec/nulldisp
ifeq ($(LMA),1)
# For non-tc platforms, we still need tc dir in search path due to pdp_gem,
# which is a generic GEM allocator from VRAM. For tc-like platforms this will
# duplicate the include path, but the alternative is to do $(filter ...) which
# is even clumsier.
ccflags-y += -I$(TOP)/kernel/drivers/staging/imgtec/tc
endif
else
# Standalone Makefile

_nulldisp_dir := $(src)
_nulldisp_obj_prefix :=

ifeq ($(KBUILD_EXTMOD),)
  # For explanation of this logic, refer to pvrsrvkm Makefile
  _abs_this_makefile_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

  ifneq ($(_abs_this_makefile_dir),$(abspath $(src))/)
    _nulldisp_dir := $(srctree)/$(src)
  endif
endif

include $(_nulldisp_dir)/../config_kernel.mk
ccflags-y += -include $(_nulldisp_dir)/../config_kernel.h

ccflags-y += -I$(_nulldisp_dir)/..
ccflags-y += -I$(_nulldisp_dir)/../pvrsrvkm/include
ccflags-y += -I$(_nulldisp_dir)/../pvrsrvkm/include/drm
ccflags-y += -I$(_nulldisp_dir)/../pvrsrvkm/include/public
ifeq ($(LMA),1)
ccflags-y += -I$(_nulldisp_dir)/../tc
endif

obj-$(CONFIG_POWERVR_NULLDISP) += drm_nulldisp.o
endif

drm_nulldisp-y += \
  $(_nulldisp_obj_prefix)drm_nulldisp_drv.o \
  $(_nulldisp_obj_prefix)drm_nulldisp_netlink.o \
  $(_nulldisp_obj_prefix)drm_netlink_gem.o

ifeq ($(LMA),1)
drm_nulldisp-y += \
  $(_nulldisp_obj_prefix)../tc/drm_pdp_gem.o \
  $(_nulldisp_obj_prefix)drm_nulldisp_gem_vram.o
else
drm_nulldisp-y += $(_nulldisp_obj_prefix)drm_nulldisp_gem_shmem.o
endif
