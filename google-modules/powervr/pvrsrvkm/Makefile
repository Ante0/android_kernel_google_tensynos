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

# Use relative paths instead since the sources are out of kernel source trees.
ifneq ($(CONFIG_POWERVR_RDO),)
  PIXEL_SOC=rdo
  BUILD_POWERVR_PIXEL=y
  GTC_FREQUENCY_HZ=25600000
endif

ifneq ($(CONFIG_POWERVR_LGA),)
  PIXEL_SOC=lga
  BUILD_POWERVR_PIXEL=y
  GTC_FREQUENCY_HZ=38400000
endif

ifneq ($(CONFIG_POWERVR_MBU),)
  PIXEL_SOC=mbu
  BUILD_POWERVR_PIXEL=y
  # TODO: verify as this is copied from LGA
  GTC_FREQUENCY_HZ=38400000
endif

ifeq ($(CONFIG_POWERVR_BRINGUP_MODE),y)
  MODE_DIR=bringup
else
  MODE_DIR=production
endif

_pvrsrvkm_dir := $(srctree)/$(src)

ifneq ($(KBUILD_EXTMOD),)
  # $(src) is reliable across kernel versions when building out of tree, so
  # there is no need to change it

  ccflags-$(CONFIG_X86_5LEVEL) += \
	-include $(srctree)/arch/x86/include/asm/cpufeatures.h
else
  # We're actually building pvrsrvkm in-tree, this makefile is our "anchor" to
  # work out whether $(src) has pre-6.10 (pre-b1992c3772e6) meaning or the new
  # meaning
  _abs_this_makefile_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

  ifneq ($(_abs_this_makefile_dir),$(abspath $(src))/)
    # Kernel is <6.10, use $(srctree)/$(src)
    _pvrsrvkm_dir := $(srctree)/$(src)
  endif
endif

include $(_pvrsrvkm_dir)/../pixel/$(PIXEL_SOC)/$(MODE_DIR)/config_kernel.mk

ccflags-y += -include $(_pvrsrvkm_dir)/../pixel/$(PIXEL_SOC)/$(MODE_DIR)/config_kernel.h \
 -I$(_pvrsrvkm_dir)/services/server/common/ \
 -I$(_pvrsrvkm_dir)/services/include \
 -I$(_pvrsrvkm_dir)/services/include/$(PVR_ARCH) \
 -I$(_pvrsrvkm_dir)/services/server/devices \
 -I$(_pvrsrvkm_dir)/services/server/devices/$(PVR_ARCH) \
 -I$(_pvrsrvkm_dir)/services/server/devices/hal/include \
 -I$(_pvrsrvkm_dir)/services/server/devices/hal/rgx \
 -I$(_pvrsrvkm_dir)/services/server/devices/hal/env/ree \
 -I$(_pvrsrvkm_dir)/services/server/env/linux \
 -I$(_pvrsrvkm_dir)/services/server/include \
 -I$(_pvrsrvkm_dir)/services/shared/common \
 -I$(_pvrsrvkm_dir)/services/shared/devices/$(PVR_ARCH) \
 -I$(_pvrsrvkm_dir)/services/shared/include \
 -I$(_pvrsrvkm_dir)/services/system/$(PVR_ARCH)/include \
 -I$(_pvrsrvkm_dir)/services/system/include \
 -I$(_pvrsrvkm_dir)/hwdefs/$(PVR_ARCH)/km \
 -I$(_pvrsrvkm_dir)/hwdefs/$(PVR_ARCH) \
 -I$(_pvrsrvkm_dir)/include/public \
 -I$(_pvrsrvkm_dir)/include/drm \
 -I$(_pvrsrvkm_dir)/include/$(PVR_ARCH) \
 -I$(_pvrsrvkm_dir)/include \
 -I$(_pvrsrvkm_dir)/..

ccflags-$(BUILD_POWERVR_PIXEL)  += \
    -I$(srctree)/drivers/devfreq \
    -I$(_pvrsrvkm_dir)/../pixel \
    -I$(_pvrsrvkm_dir)/../pixel/${PIXEL_SOC} \
    -DPIXEL_GPU_GENERATION="\"${PIXEL_SOC}\"" \
    -DGTC_FREQUENCY_HZ="${GTC_FREQUENCY_HZ}" \
    -I$(ROOT_DIR)/private/google-modules/soc/rdo/include/ \
    -I$(ROOT_DIR)/private/google-modules/trusty/include/ \
    -I$(ROOT_DIR)/private/google-modules/soc/gs/include/ \
    -I$(ROOT_DIR)/private/google-modules/perf/infra/mbfs/include/ \
    -I$(_pvrsrvkm_dir)/../customer/pixel/include

# Note: replacement for
# tmp := $(addprefix -I,$(wildcard $(srctree)/$(src)/generated/$(PVR_ARCH)/*))
# ccflags-y += $(tmp)

ccflags-y += $(addprefix -I,$(wildcard $(_pvrsrvkm_dir)/generated/$(PVR_ARCH)/*))
ccflags-y += -include $(OUT_DIR)/include/generated/uapi/linux/version.h

ccflags-y += $(INTERNAL_KBUILD_CFLAGS)

ifeq ($(PVR_BUILD_TYPE),debug)
ccflags-y += -DDYNAMIC_DEBUG_MODULE
endif

# To enable debug logs in Pixel integration
# ccflags-y += -DPVR_DPF_CUSTOMER_DEBUG_ON

ccflags-$(CONFIG_X86) += -mno-soft-float

ccflags-$(PVR_ARCH:rogue=y)    += -DPVR_ARCH_ROGUE
ccflags-$(PVR_ARCH:volcanic=y) += -DPVR_ARCH_VOLCANIC

override PVRSRV_MODNAME := pvrsrvkm
obj-$(CONFIG_POWERVR_ROGUE) += $(PVRSRV_MODNAME).o

# System dir
ifneq ($(wildcard $(_pvrsrvkm_dir)/services/system/$(PVR_ARCH)/$(PVR_SYSTEM)/Kbuild.mk),)
SYSTEM_DIR := $(_pvrsrvkm_dir)/services/system/$(PVR_ARCH)/$(PVR_SYSTEM)
else
SYSTEM_DIR := $(_pvrsrvkm_dir)/services/system/$(PVR_SYSTEM)
endif

$(PVRSRV_MODNAME)-y += \
 services/server/env/linux/dkf_server.o \
 services/server/env/linux/event.o \
 services/server/env/linux/fwload.o \
 services/server/env/linux/km_apphint.o \
 services/server/env/linux/module_common.o \
 services/server/env/linux/osmmap_stub.o \
 services/server/env/linux/osfunc.o \
 services/server/env/linux/allocmem.o \
 services/server/env/linux/osconnection_server.o \
 services/server/env/linux/physmem_osmem_linux.o \
 services/server/env/linux/pmr_os.o \
 services/server/env/linux/pmr_env.o \
 services/server/env/linux/pvr_bridge_k.o \
 services/server/env/linux/pvr_debug.o \
 services/server/env/linux/pvr_drm.o \
 services/server/common/devicemem_heapcfg.o \
 services/shared/common/devicemem.o \
 services/shared/common/devicemem_utils.o \
 services/shared/common/hash.o \
 services/shared/common/hash_functions.o \
 services/shared/common/ra.o \
 services/shared/common/sync.o \
 services/shared/common/mem_utils.o \
 services/server/common/devicemem_server.o \
 services/server/common/handle.o \
 services/server/common/lists.o \
 services/server/common/mmu_common.o \
 services/server/common/connection_server.o \
 services/server/common/physheap.o \
 services/server/common/physmem.o \
 services/server/common/physmem_lma.o \
 services/server/common/physmem_ramem.o \
 services/server/common/physmem_osmem.o \
 services/server/common/physmem_hostmem.o \
 services/server/common/pmr.o \
 services/server/common/power.o \
 services/server/common/process_stats.o \
 services/server/common/pvr_notifier.o \
 services/server/common/pvrsrv.o \
 services/server/common/srvcore.o \
 services/server/common/sync_checkpoint.o \
 services/server/common/sync_server.o \
 services/server/common/sync_qbs.o \
 services/server/common/tlintern.o \
 services/shared/common/tlclient.o \
 services/server/common/tlserver.o \
 services/server/common/tlstream.o \
 services/server/common/cache_km.o \
 services/shared/common/uniq_key_splay_tree.o \
 services/server/common/pvrsrv_pool.o \
 services/server/common/pvrsrv_bridge_init.o \
 services/server/common/info_page_km.o \
 services/shared/common/pvrsrv_error.o \
 services/server/common/debug_common.o \
 services/server/common/di_server.o \
 services/server/common/vmm_pvz_client.o \
 services/server/common/vmm_pvz_server.o \
 services/server/common/vz_vmm_pvz.o \
 services/server/common/vz_vmm_vm.o \
 services/system/common/vmm_type_stub.o

ifeq ($(PVRSRV_ENABLE_DYNAMIC_PHYSHEAPS),1)
 $(PVRSRV_MODNAME)-y += \
 services/server/common/physmem_dlm.o \
 services/server/common/physmem_ima.o
endif

ifeq ($(PVRSRV_ENABLE_HTB),1)
$(PVRSRV_MODNAME)-y += \
 services/shared/common/htbuffer.o \
 services/server/common/htbserver.o \
 services/server/common/htb_debug.o
endif

ifeq ($(SUPPORT_DMA_TRANSFER),1)
 $(PVRSRV_MODNAME)-y += \
 services/server/common/dma_km.o
endif

# Wrap ExtMem support
ifeq ($(SUPPORT_WRAP_EXTMEM),1)
 $(PVRSRV_MODNAME)-y += \
 services/server/env/linux/physmem_extmem_linux.o \
 services/server/common/physmem_extmem.o
endif

ifeq ($(SUPPORT_PHYSMEM_TEST),1)
 $(PVRSRV_MODNAME)-y += \
 services/server/env/linux/physmem_test.o
endif

ifneq ($(PVR_LOADER),)
 $(PVRSRV_MODNAME)-y += services/server/env/linux/$(PVR_LOADER).o
else
 $(PVRSRV_MODNAME)-y += services/server/env/linux/pvr_platform_drv.o
endif

$(PVRSRV_MODNAME)-y += \
 services/server/devices/rgx_bridge_init.o \
 services/server/devices/rgxfwdbg.o \
 services/server/devices/rgxtimerquery.o \
 services/server/devices/rgxccb.o \
 services/server/devices/rgxdebug_common.o \
 services/server/devices/$(PVR_ARCH)/rgxdebug.o \
 services/server/devices/rgxfwtrace_strings.o \
 services/server/devices/rgxfwutils.o \
 services/server/devices/rgxregaccess.o \
 services/server/devices/rgxfwcmnctx.o \
 services/server/devices/rgxfwriscv.o \
 services/server/devices/rgxinit_common.o \
 services/server/devices/$(PVR_ARCH)/rgxinit.o \
 services/server/devices/rgxbvnc.o \
 services/server/devices/hal/env/ree/rgxhal_driver_cmn.o \
 services/server/devices/hal/env/ree/$(PVR_ARCH)/rgxhal_driver.o \
 services/server/devices/rgxmem.o \
 services/server/devices/rgxmmuinit.o \
 services/server/devices/rgxregconfig.o \
 services/server/devices/$(PVR_ARCH)/rgxta3d.o \
 services/server/devices/rgxsyncutils.o \
 services/server/devices/rgxtdmtransfer.o \
 services/server/devices/rgxutils.o \
 services/server/devices/rgxhwperf_common.o \
 services/server/devices/$(PVR_ARCH)/rgxhwperf.o \
 services/server/devices/rgxpower.o \
 services/server/devices/hal/rgx/$(PVR_ARCH)/rgxstartstop.o \
 services/server/devices/rgxtimecorr.o \
 services/server/devices/rgxcompute.o \
 services/server/devices/$(PVR_ARCH)/rgxmulticore.o \
 services/server/devices/rgxshader.o

# add pvr_gputrace.o if any of these features are enabled
ifneq (, $(filter 1,$(PVRSRV_TRACE_ROGUE_EVENTS) $(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) $(PVRSRV_ANDROID_TRACE_GPU_FREQ)))
$(PVRSRV_MODNAME)-$(CONFIG_FTRACE) += services/server/env/linux/pvr_gputrace.o
endif

ifeq ($(SUPPORT_RGXKICKSYNC_BRIDGE),1)
$(PVRSRV_MODNAME)-y += \
 services/server/devices/rgxkicksync.o
endif

ifeq ($(SUPPORT_USC_BREAKPOINT),1)
$(PVRSRV_MODNAME)-y += \
 services/server/devices/rgxbreakpoint.o
endif

ifeq ($(PVR_ARCH),volcanic)
$(PVRSRV_MODNAME)-y += \
 services/server/devices/$(PVR_ARCH)/rgxray.o
endif


ifeq ($(PVR_ARCH),rogue)
 $(PVRSRV_MODNAME)-y += \
  services/server/devices/$(PVR_ARCH)/rgxtransfer.o \
  services/server/devices/$(PVR_ARCH)/rgxmipsmmuinit.o
endif

ifeq ($(SUPPORT_WORKLOAD_ESTIMATION),1)
 $(PVRSRV_MODNAME)-y += \
 services/server/devices/rgxworkest.o
 ifeq ($(PVR_ARCH),volcanic)
  $(PVRSRV_MODNAME)-y += \
  services/server/devices/$(PVR_ARCH)/rgxworkest_ray.o
 endif
endif
ifeq ($(PDUMP),1)
$(PVRSRV_MODNAME)-y += \
 services/server/common/pdump_server.o \
 services/server/common/pdump_mmu.o \
 services/server/common/pdump_physmem.o \
 services/shared/common/devicemem_pdump.o \
 services/shared/common/devicememx_pdump.o

$(PVRSRV_MODNAME)-y += \
 services/server/devices/$(PVR_ARCH)/rgxpdump.o
$(PVRSRV_MODNAME)-y += \
 services/server/devices/rgxpdump_common.o

endif


$(PVRSRV_MODNAME)-$(BUILD_POWERVR_PIXEL) += \
    ../customer/pixel/custom_command.o \
    ../customer/pixel/dvfs.o \
    ../pixel/dvfs.o \
    ../pixel/dvfs_governor.o \
    ../pixel/debug.o \
    ../pixel/genpd.o \
    ../pixel/gpu_uevent.o \
    ../pixel/ioctl.o \
    ../pixel/of.o \
    ../pixel/mba.o \
    ../pixel/physmem.o \
    ../pixel/scheduling.o
    ../pixel/sscd.o \
    ../pixel/sysconfig.o \
		services/system/common/env/linux/interrupt_support.o

# Support per-UID TIS
ifeq ($(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD),1)
ifeq ($(SUPPORT_LINUX_DVFS),1)
$(PVRSRV_MODNAME)-$(BUILD_POWERVR_PIXEL) += ../pixel/uid_time_in_state.o
endif
endif

$(PVRSRV_MODNAME)-$(CONFIG_POWERVR_PIXEL_SLC) += \
    ../pixel/slc.o

$(PVRSRV_MODNAME)-$(CONFIG_POWERVR_PIXEL_IIF) += \
    ../pixel/fence_manager.o

$(PVRSRV_MODNAME)-$(CONFIG_TRUSTY:m=y) += ../pixel/gpu_secure.o

$(PVRSRV_MODNAME)-y +=\
    services/system/common/sysconfig_cmn.o

ifeq ($(PVRSRV_ENABLE_GPU_MEMORY_INFO),1)
$(PVRSRV_MODNAME)-y += services/server/common/ri_server.o
endif
$(PVRSRV_MODNAME)-y += services/server/common/devicemem_history_server.o

ifeq ($(PVRSRV_PHYSMEM_CPUMAP_HISTORY),1)
$(PVRSRV_MODNAME)-y += services/server/common/physmem_cpumap_history.o
endif

ifeq ($(PVR_HANDLE_BACKEND),generic)
$(PVRSRV_MODNAME)-y += services/server/common/handle_generic.o
else
ifeq ($(PVR_HANDLE_BACKEND),idr)
$(PVRSRV_MODNAME)-y += services/server/env/linux/handle_idr.o
endif
endif

$(PVRSRV_MODNAME)-$(CONFIG_DMA_SHARED_BUFFER) += services/server/env/linux/physmem_dmabuf.o

# Event tracing
$(PVRSRV_MODNAME)-$(CONFIG_EVENT_TRACING) += services/server/env/linux/trace_events.o

# Debug info interface
$(PVRSRV_MODNAME)-$(CONFIG_DEBUG_FS) += services/server/env/linux/pvr_debugfs.o
$(PVRSRV_MODNAME)-$(CONFIG_PROC_FS) += services/server/env/linux/pvr_procfs.o

# Android native synchronisation
ifeq ($(SUPPORT_NATIVE_FENCE_SYNC),1)
 $(PVRSRV_MODNAME)-y += services/server/env/linux/pvr_sync_ioctl_common.o \
  services/server/env/linux/pvr_sync_file.o \
  services/server/env/linux/pvr_counting_timeline.o \
  services/server/env/linux/pvr_sw_fence.o \
  services/server/env/linux/pvr_export_fence.o \
  services/server/env/linux/pvr_fence.o \
  services/server/env/linux/pvr_fence_print_ops.o

 ifeq ($(USE_PVRSYNC_DEVNODE),1)
  $(PVRSRV_MODNAME)-y += services/server/env/linux/pvr_sync_ioctl_dev.o
 else
  $(PVRSRV_MODNAME)-y += services/server/env/linux/pvr_sync_ioctl_drm.o
 endif
endif

ifeq ($(SUPPORT_LINUX_DVFS),1)
$(PVRSRV_MODNAME)-y += services/server/env/linux/pvr_dvfs_device.o \
  services/server/env/linux/pvr_dvfs_common.o
else
 ifeq ($(SUPPORT_PDVFS),1)
  $(PVRSRV_MODNAME)-y += \
   services/server/env/linux/pvr_dvfs_proactive.o \
   services/server/env/linux/pvr_dvfs_common.o \
   services/server/devices/rgxpdvfs.o
 endif
endif

ifeq ($(PVRSRV_ENABLE_PVR_ION_STATS),1)
$(PVRSRV_MODNAME)-y += \
 services/server/env/linux/pvr_ion_stats.o
endif

$(PVRSRV_MODNAME)-$(CONFIG_X86) += services/server/env/linux/osfunc_x86.o
$(PVRSRV_MODNAME)-$(CONFIG_ARM) += services/server/env/linux/osfunc_arm.o
$(PVRSRV_MODNAME)-$(CONFIG_ARM64) += services/server/env/linux/osfunc_arm64.o
$(PVRSRV_MODNAME)-$(CONFIG_METAG) += services/server/env/linux/osfunc_metag.o
$(PVRSRV_MODNAME)-$(CONFIG_MIPS) += services/server/env/linux/osfunc_mips.o

$(PVRSRV_MODNAME)-y += services/server/common/di_impl_brg.o

# Srvinit headers and source files

$(PVRSRV_MODNAME)-y += \
 services/server/devices/$(PVR_ARCH)/rgxsrvinit.o \
 services/server/devices/hal/rgx/rgxfwimageutils.o
ifeq ($(PVR_ARCH),rogue)
$(PVRSRV_MODNAME)-y += \
 services/shared/devices/$(PVR_ARCH)/rgx_hwperf_table.o
endif

$(PVRSRV_MODNAME)-y += \
 services/system/$(PVR_ARCH)/common/env/linux/dma_support.o

# Generated bridge code
$(PVRSRV_MODNAME)-$(CONFIG_DMA_SHARED_BUFFER) += generated/$(PVR_ARCH)/dmabuf_bridge/server_dmabuf_bridge.o

ifeq ($(SUPPORT_ANDROID_PLATFORM),1)
 $(PVRSRV_MODNAME)-$(CONFIG_DMA_SHARED_BUFFER) += \
   services/server/env/linux/physmem_dmabuf_fbc_tracker.o
endif

ccflags-y += -I$(SYSTEM_DIR)

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/mm_bridge/server_mm_bridge.o \
 generated/$(PVR_ARCH)/cmm_bridge/server_cmm_bridge.o \
 generated/$(PVR_ARCH)/srvcore_bridge/server_srvcore_bridge.o \
 generated/$(PVR_ARCH)/sync_bridge/server_sync_bridge.o \
 generated/$(PVR_ARCH)/pvrtl_bridge/server_pvrtl_bridge.o \
 generated/$(PVR_ARCH)/cache_bridge/server_cache_bridge.o

ifeq ($(PVRSRV_ENABLE_HTB),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/htbuffer_bridge/server_htbuffer_bridge.o
endif

ifeq ($(SUPPORT_DMA_TRANSFER),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/dma_bridge/server_dma_bridge.o
endif

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxtq2_bridge/server_rgxtq2_bridge.o \
 generated/$(PVR_ARCH)/rgxta3d_bridge/server_rgxta3d_bridge.o \
 generated/$(PVR_ARCH)/rgxhwperf_bridge/server_rgxhwperf_bridge.o \
 generated/$(PVR_ARCH)/rgxcmp_bridge/server_rgxcmp_bridge.o \
 generated/$(PVR_ARCH)/rgxregconfig_bridge/server_rgxregconfig_bridge.o \
 generated/$(PVR_ARCH)/rgxtimerquery_bridge/server_rgxtimerquery_bridge.o \
 generated/$(PVR_ARCH)/rgxfwdbg_bridge/server_rgxfwdbg_bridge.o
ifeq ($(PVR_ARCH),volcanic)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxray_bridge/server_rgxray_bridge.o
endif
ifeq ($(PVR_ARCH),rogue)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxtq_bridge/server_rgxtq_bridge.o
endif
ifeq ($(SUPPORT_USC_BREAKPOINT),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxbreakpoint_bridge/server_rgxbreakpoint_bridge.o
endif

ifeq ($(PVRSRV_ENABLE_GPU_MEMORY_INFO),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/ri_bridge/server_ri_bridge.o \
 generated/$(PVR_ARCH)/ri_bridge/client_ri_direct_bridge.o
endif
ifeq ($(SUPPORT_RGXKICKSYNC_BRIDGE),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxkicksync_bridge/server_rgxkicksync_bridge.o
endif

ifeq ($(SUPPORT_WRAP_EXTMEM),1)
$(PVRSRV_MODNAME)-y += generated/$(PVR_ARCH)/mmextmem_bridge/server_mmextmem_bridge.o
endif

ifeq ($(PDUMP),1)
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/rgxpdump_bridge/server_rgxpdump_bridge.o

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/pdump_bridge/server_pdump_bridge.o \
 generated/$(PVR_ARCH)/pdumpctrl_bridge/server_pdumpctrl_bridge.o \
 generated/$(PVR_ARCH)/pdumpmm_bridge/server_pdumpmm_bridge.o
endif

ifeq ($(PVRSRV_ENABLE_GPU_MEMORY_INFO),1)
$(PVRSRV_MODNAME)-y += generated/$(PVR_ARCH)/ri_bridge/server_ri_bridge.o
endif

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/devicememhistory_bridge/server_devicememhistory_bridge.o

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/synctracking_bridge/server_synctracking_bridge.o

$(PVRSRV_MODNAME)-y += generated/$(PVR_ARCH)/di_bridge/server_di_bridge.o


# Direct bridges

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/mm_bridge/client_mm_direct_bridge.o \
 generated/$(PVR_ARCH)/sync_bridge/client_sync_direct_bridge.o \
 generated/$(PVR_ARCH)/cache_bridge/client_cache_direct_bridge.o \
 generated/$(PVR_ARCH)/pvrtl_bridge/client_pvrtl_direct_bridge.o

ifeq ($(PVRSRV_ENABLE_HTB),1)
$(PVRSRV_MODNAME)-y +=\
 generated/$(PVR_ARCH)/htbuffer_bridge/client_htbuffer_direct_bridge.o
endif

ifeq ($(PDUMP),1)
$(PVRSRV_MODNAME)-y += generated/$(PVR_ARCH)/pdumpmm_bridge/client_pdumpmm_direct_bridge.o
endif

ifeq ($(PVRSRV_ENABLE_GPU_MEMORY_INFO),1)
$(PVRSRV_MODNAME)-y += generated/$(PVR_ARCH)/ri_bridge/client_ri_direct_bridge.o
endif

ifeq ($(PDUMP),1)
 $(PVRSRV_MODNAME)-y += \
  generated/$(PVR_ARCH)/pdump_bridge/client_pdump_direct_bridge.o \
  generated/$(PVR_ARCH)/pdumpctrl_bridge/client_pdumpctrl_direct_bridge.o
endif

# Volcanic arch particular files
ifeq ($(SUPPORT_RGXRAY_BRIDGE),1)
PVRSRV_SUPPORT_RAY_BRIDGE := 1
else
PVRSRV_SUPPORT_RAY_BRIDGE := 0
endif
pvrsrvkm-$(PVRSRV_SUPPORT_RAY_BRIDGE:1=y) += \
  generated/$(PVR_ARCH)/rgxray_bridge/server_rgxray_bridge.o
pvrsrvkm-$(PVRSRV_SUPPORT_RAY_BRIDGE:1=y) += services/server/devices/$(PVR_ARCH)/rgxray.o
ifeq ($(SUPPORT_WORKLOAD_ESTIMATION),1)
 pvrsrvkm-$(PVRSRV_SUPPORT_RAY_BRIDGE:1=y) += services/server/devices/$(PVR_ARCH)/rgxworkest_ray.o
endif
$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/devicememhistory_bridge/client_devicememhistory_direct_bridge.o

$(PVRSRV_MODNAME)-y += \
 generated/$(PVR_ARCH)/synctracking_bridge/client_synctracking_direct_bridge.o

# Pixel doesn't use it
# # syslayer Kbuild.mks may use TOP - make sure it's set
# ifeq ($(origin TOP),undefined)
#   TOP := $(_pvrsrvkm_dir)
# endif
# include $(SYSTEM_DIR)/Kbuild.mk
