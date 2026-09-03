# SPDX-License-Identifier: GPL-2.0-or-later
"""This module defines source files based on configurations."""

load(":pixel/lga/bringup/config_kernel.bzl", lga_bringup_dict = "CONFIG_KERNEL_VARS")
load(":pixel/lga/production/config_kernel.bzl", lga_production_dict = "CONFIG_KERNEL_VARS")
load(":pixel/mbu/bringup/config_kernel.bzl", mbu_bringup_dict = "CONFIG_KERNEL_VARS")
load(":pixel/mbu/production/config_kernel.bzl", mbu_production_dict = "CONFIG_KERNEL_VARS")
load(":pixel/rdo/bringup/config_kernel.bzl", rdo_bringup_dict = "CONFIG_KERNEL_VARS")
load(":pixel/rdo/production/config_kernel.bzl", rdo_production_dict = "CONFIG_KERNEL_VARS")

def __srcs_from_config(cfg):
    srcs = []

    if cfg.get("PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD") == "1" and cfg.get("SUPPORT_LINUX_DVFS") == "1":
        srcs.append("pixel/uid_time_in_state.c")

    if cfg.get("PVRSRV_ENABLE_DYNAMIC_PHYSHEAPS") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/common/physmem_dlm.c",
            "pvrsrvkm/services/server/common/physmem_ima.c",
        ])

    if cfg.get("PVRSRV_ENABLE_GPU_MEMORY_INFO") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/common/ri_server.c",
            "pvrsrvkm/generated/volcanic/ri_bridge/server_ri_bridge.c",
            "pvrsrvkm/generated/volcanic/ri_bridge/client_ri_direct_bridge.c",
        ])

    if cfg.get("PVRSRV_ENABLE_HTB") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/common/htbserver.c",
            "pvrsrvkm/services/shared/common/htbuffer.c",
            "pvrsrvkm/services/server/common/htb_debug.c",
            "pvrsrvkm/generated/volcanic/htbuffer_bridge/server_htbuffer_bridge.c",
            "pvrsrvkm/generated/volcanic/htbuffer_bridge/client_htbuffer_direct_bridge.c",
        ])

    if cfg.get("PVRSRV_ENABLE_PVR_ION_STATS") == "1":
        srcs.append("pvrsrvkm/services/server/env/linux/pvr_ion_stats.c")

    if cfg.get("SUPPORT_DMA_TRANSFER") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/common/dma_km.c",
            "pvrsrvkm/generated/volcanic/dma_bridge/server_dma_bridge.c",
        ])

    if cfg.get("SUPPORT_LINUX_DVFS") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/env/linux/pvr_dvfs_device.c",
            "pvrsrvkm/services/server/env/linux/pvr_dvfs_common.c",
        ])
    elif cfg.get("SUPPORT_PDVFS") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/env/linux/pvr_dvfs_proactive.c",
            "pvrsrvkm/services/server/env/linux/pvr_dvfs_common.c",
            "pvrsrvkm/services/server/devices/rgxpdvfs.c",
        ])

    if cfg.get("SUPPORT_NATIVE_FENCE_SYNC") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/env/linux/pvr_sync_ioctl_common.c",
            "pvrsrvkm/services/server/env/linux/pvr_sync_file.c",
            "pvrsrvkm/services/server/env/linux/pvr_counting_timeline.c",
            "pvrsrvkm/services/server/env/linux/pvr_sw_fence.c",
            "pvrsrvkm/services/server/env/linux/pvr_export_fence.c",
            "pvrsrvkm/services/server/env/linux/pvr_fence.c",
            "pvrsrvkm/services/server/env/linux/pvr_fence_print_ops.c",
        ])
        if cfg.get("USE_PVRSYNC_DEVNODE") == "1":
            srcs.append("pvrsrvkm/services/server/env/linux/pvr_sync_ioctl_dev.c")
        else:
            srcs.append("pvrsrvkm/services/server/env/linux/pvr_sync_ioctl_drm.c")

    if cfg.get("SUPPORT_RGXKICKSYNC_BRIDGE") == "1":
        srcs.append("pvrsrvkm/generated/volcanic/rgxkicksync_bridge/server_rgxkicksync_bridge.c")

    if cfg.get("SUPPORT_RGXRAY_BRIDGE") == "1":
        srcs.extend([
            "pvrsrvkm/generated/volcanic/rgxray_bridge/server_rgxray_bridge.c",
            "pvrsrvkm/services/server/devices/volcanic/rgxray.c",
        ])

    if cfg.get("SUPPORT_WORKLOAD_ESTIMATION") == "1":
        srcs.append("pvrsrvkm/services/server/devices/rgxworkest.c")
        if cfg.get("SUPPORT_RGXRAY_BRIDGE") == "1":
            srcs.append("pvrsrvkm/services/server/devices/volcanic/rgxworkest_ray.c")

    if cfg.get("SUPPORT_WRAP_EXTMEM") == "1":
        srcs.extend([
            "pvrsrvkm/generated/volcanic/mmextmem_bridge/server_mmextmem_bridge.c",
            "pvrsrvkm/services/server/env/linux/physmem_extmem_linux.c",
            "pvrsrvkm/services/server/common/physmem_extmem.c",
        ])

    if cfg.get("SUPPORT_FASTPATH_FENCE") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/env/linux/physmem_physwrap_linux.c",
            "pvrsrvkm/services/server/env/linux/pvr_sync_fpf_token.c",
            "pvrsrvkm/services/server/common/devicemem_physwrap.c",
            "pvrsrvkm/services/system/common/env/linux/sys_fpf_common.c",
        ])
        if cfg.get("SUPPORT_FASTPATH_FENCE_CUSTOM_COMMS") == "1":
            srcs.append("pvrsrvkm/services/system/common/env/linux/sys_fpf_fpt_cb.c")
        else:
            srcs.append("pvrsrvkm/services/system/common/env/linux/sys_fpf_custom.c")

    if cfg.get("PVRSRV_PHYSMEM_CPUMAP_HISTORY") == "1":
        srcs.extend([
            "pvrsrvkm/services/server/common/physmem_cpumap_history.c",
        ])

    return srcs

def __ftrace_srcs(cfg):
    srcs = []

    if cfg.get("PVRSRV_TRACE_ROGUE_EVENTS") == "1" or cfg.get("PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD") == "1" or cfg.get("PVRSRV_ANDROID_TRACE_GPU_FREQ") == "1":
        srcs.append("pvrsrvkm/services/server/env/linux/pvr_gputrace.c")

    return srcs

# We need to prepare sources per every config, because this script will be
# read before evaluation of build flags.
# Dictionaries are populated on Bazel's Loading Phase, and the right dictionary
# is selected during Analysis Phase.
RDO_BRINGUP_SRCS = __srcs_from_config(rdo_bringup_dict)
RDO_PRODUCTION_SRCS = __srcs_from_config(rdo_production_dict)
LGA_BRINGUP_SRCS = __srcs_from_config(lga_bringup_dict)
LGA_PRODUCTION_SRCS = __srcs_from_config(lga_production_dict)
MBU_BRINGUP_SRCS = __srcs_from_config(mbu_bringup_dict)
MBU_PRODUCTION_SRCS = __srcs_from_config(mbu_production_dict)

RDO_BRINGUP_FTRACE_SRCS = __ftrace_srcs(rdo_bringup_dict)
RDO_PRODUCTION_FTRACE_SRCS = __ftrace_srcs(rdo_production_dict)
LGA_BRINGUP_FTRACE_SRCS = __ftrace_srcs(lga_bringup_dict)
LGA_PRODUCTION_FTRACE_SRCS = __ftrace_srcs(lga_production_dict)
MBU_BRINGUP_FTRACE_SRCS = __ftrace_srcs(mbu_bringup_dict)
MBU_PRODUCTION_FTRACE_SRCS = __ftrace_srcs(mbu_production_dict)

PIXEL_SRCS = select({
    ":rdo_bringup": RDO_BRINGUP_SRCS,
    ":rdo_production": RDO_PRODUCTION_SRCS,
    ":lga_bringup": LGA_BRINGUP_SRCS,
    ":lga_production": LGA_PRODUCTION_SRCS,
    ":mbu_bringup": MBU_BRINGUP_SRCS,
    ":mbu_production": MBU_PRODUCTION_SRCS,
    "//conditions:default": [],
})

FTRACE_SRCS = select({
    ":rdo_bringup": RDO_BRINGUP_FTRACE_SRCS,
    ":rdo_production": RDO_PRODUCTION_FTRACE_SRCS,
    ":lga_bringup": LGA_BRINGUP_FTRACE_SRCS,
    ":lga_production": LGA_PRODUCTION_FTRACE_SRCS,
    ":mbu_bringup": MBU_BRINGUP_FTRACE_SRCS,
    ":mbu_production": MBU_PRODUCTION_FTRACE_SRCS,
    "//conditions:default": [],
})

CONDITIONAL_SRCS = {
    "CONFIG_DRM": {
        True: [
            "pvrsrvkm/services/server/env/linux/pvr_drm.c",
        ],
    },
    "CONFIG_DMA_SHARED_BUFFER": {
        True: [
            "pvrsrvkm/services/server/env/linux/physmem_dmabuf.c",
            "pvrsrvkm/services/server/env/linux/physmem_dmabuf_fbc_tracker.c",
            "pvrsrvkm/generated/volcanic/dmabuf_bridge/server_dmabuf_bridge.c",
        ],
    },
    "CONFIG_BUILD_POWERVR_PIXEL": {
        True: [
            "customer/pixel/custom_command.c",
            "customer/pixel/dvfs.c",
            "pixel/dvfs.c",
            "pixel/dvfs_governor.c",
            "pixel/debug.c",
            "pixel/genpd.c",
            "pixel/gpu_uevent.c",
            "pixel/ioctl.c",
            "pixel/of.c",
            "pixel/mba.c",
            "pixel/physmem.c",
            "pixel/scheduling.c",
            "pixel/sscd.c",
            "pixel/sysconfig.c",
            "pvrsrvkm/services/system/common/env/linux/interrupt_support.c",
        ],
    },
    "CONFIG_POWERVR_PIXEL_SLC": {
        True: [
            "pixel/slc.c",
        ],
    },
    "CONFIG_POWERVR_PIXEL_IIF": {
        True: [
            "pixel/fence_manager.c",
        ],
    },
    "CONFIG_TRUSTY": {
        True: [
            "pixel/gpu_secure.c",
        ],
    },
    "CONFIG_POWERVR_ROGUE_DEVICEMEM_HISTORY": {
        True: [
            "pvrsrvkm/services/server/common/devicemem_history_server.c",
        ],
    },
    "CONFIG_FTRACE": {
        True: FTRACE_SRCS,
    },
    "CONFIG_EVENT_TRACING": {
        True: [
            "pvrsrvkm/services/server/env/linux/trace_events.c",
        ],
    },
    "CONFIG_DEBUG_FS": {
        True: [
            "pvrsrvkm/services/server/env/linux/pvr_debugfs.c",
        ],
    },
    "CONFIG_PROC_FS": {
        True: [
            "pvrsrvkm/services/server/env/linux/pvr_procfs.c",
        ],
    },
    "CONFIG_ARM64": {
        True: [
            "pvrsrvkm/services/server/env/linux/osfunc_arm64.c",
        ],
    },
    "CONFIG_POWERVR_ROGUE_DEBUG": {
        True: [
            "pvrsrvkm/generated/volcanic/rgxkicksync_bridge/server_rgxkicksync_bridge.c",
        ],
    },
    "CONFIG_POWERVR_ROGUE_PDUMP": {
        True: [
            "pvrsrvkm/services/shared/common/devicemem_pdump.c",
            "pvrsrvkm/services/shared/common/devicememx_pdump.c",
            "pvrsrvkm/services/server/common/pdump_server.c",
            "pvrsrvkm/services/server/common/pdump_mmu.c",
            "pvrsrvkm/services/server/common/pdump_physmem.c",
            "pvrsrvkm/services/server/devices/rgxpdump_common.c",
            "pvrsrvkm/services/server/devices/volcanic/rgxpdump.c",
            "pvrsrvkm/generated/volcanic/pdumpmm_bridge/server_pdumpmm_bridge.c",
            "pvrsrvkm/generated/volcanic/pdumpmm_bridge/client_pdumpmm_direct_bridge.c",
            "pvrsrvkm/generated/volcanic/pdump_bridge/server_pdump_bridge.c",
            "pvrsrvkm/generated/volcanic/pdump_bridge/client_pdump_direct_bridge.c",
            "pvrsrvkm/generated/volcanic/pdumpctrl_bridge/server_pdumpctrl_bridge.c",
            "pvrsrvkm/generated/volcanic/rgxpdump_bridge/server_rgxpdump_bridge.c",
            "pvrsrvkm/generated/volcanic/rgxkicksync_bridge/server_rgxkicksync_bridge.c",
        ],
    },
    "SUPPORT_WORKLOAD_ESTIMATION": {
        True: [
            "pvrsrvkm/services/server/devices/rgxworkest.c",
        ],
    },
    "SUPPORT_NATIVE_FENCE_SYNC": {
        True: [
            "pvrsrvkm/services/server/env/linux/pvr_sync_ioctl_common.c",
        ],
    },
}

POWERVR_COMMON_SRCS = [
    "pvrsrvkm/services/server/env/linux/allocmem.c",
    "pvrsrvkm/services/server/common/cache_km.c",
    "pvrsrvkm/services/server/common/connection_server.c",
    "pvrsrvkm/services/server/common/debug_common.c",
    "pvrsrvkm/services/shared/common/devicemem.c",
    "pvrsrvkm/services/server/common/devicemem_heapcfg.c",
    "pvrsrvkm/services/server/common/devicemem_history_server.c",
    "pvrsrvkm/services/server/common/devicemem_server.c",
    "pvrsrvkm/services/shared/common/devicemem_utils.c",
    "pvrsrvkm/services/server/common/di_impl_brg.c",
    "pvrsrvkm/services/server/common/di_server.c",
    "pvrsrvkm/services/server/env/linux/dkf_server.c",
    "pvrsrvkm/services/server/env/linux/event.c",
    "pvrsrvkm/services/server/env/linux/fwload.c",
    "pvrsrvkm/services/server/common/handle.c",
    "pvrsrvkm/services/server/env/linux/handle_idr.c",
    "pvrsrvkm/services/shared/common/hash.c",
    "pvrsrvkm/services/shared/common/hash_functions.c",
    "pvrsrvkm/services/server/common/info_page_km.c",
    "pvrsrvkm/services/server/env/linux/km_apphint.c",
    "pvrsrvkm/services/server/common/lists.c",
    "pvrsrvkm/services/shared/common/mem_utils.c",
    "pvrsrvkm/services/server/common/mmu_common.c",
    "pvrsrvkm/services/server/env/linux/module_common.c",
    "pvrsrvkm/services/server/env/linux/osconnection_server.c",
    "pvrsrvkm/services/server/env/linux/osfunc.c",
    "pvrsrvkm/services/server/env/linux/osmmap_stub.c",
    "pvrsrvkm/services/server/common/physheap.c",
    "pvrsrvkm/services/server/common/physmem.c",
    "pvrsrvkm/services/server/common/physmem_lma.c",
    "pvrsrvkm/services/server/common/physmem_ramem.c",
    "pvrsrvkm/services/server/common/physmem_osmem.c",
    "pvrsrvkm/services/server/env/linux/physmem_osmem_linux.c",
    "pvrsrvkm/services/server/common/physmem_hostmem.c",
    "pvrsrvkm/services/server/env/linux/physmem_test.c",
    "pvrsrvkm/services/server/env/linux/pvr_platform_drv.c",
    "pvrsrvkm/services/server/common/pmr.c",
    "pvrsrvkm/services/server/env/linux/pmr_env.c",
    "pvrsrvkm/services/server/env/linux/pmr_os.c",
    "pvrsrvkm/services/server/common/power.c",
    "pvrsrvkm/services/server/common/process_stats.c",
    "pvrsrvkm/services/server/env/linux/pvr_bridge_k.c",
    "pvrsrvkm/services/server/env/linux/pvr_debug.c",
    "pvrsrvkm/services/server/common/pvr_notifier.c",
    "pvrsrvkm/services/server/common/pvrsrv.c",
    "pvrsrvkm/services/server/common/pvrsrv_bridge_init.c",
    "pvrsrvkm/services/shared/common/pvrsrv_error.c",
    "pvrsrvkm/services/server/common/pvrsrv_pool.c",
    "pvrsrvkm/services/shared/common/ra.c",
    "pvrsrvkm/services/server/devices/rgx_bridge_init.c",
    "pvrsrvkm/services/server/devices/rgxbreakpoint.c",
    "pvrsrvkm/services/server/devices/rgxbvnc.c",
    "pvrsrvkm/services/server/devices/rgxccb.c",
    "pvrsrvkm/services/server/devices/rgxcompute.c",
    "pvrsrvkm/services/server/devices/rgxdebug_common.c",
    "pvrsrvkm/services/server/devices/rgxinit_common.c",
    "pvrsrvkm/services/server/devices/rgxfwcmnctx.c",
    "pvrsrvkm/services/server/devices/rgxfwdbg.c",
    "pvrsrvkm/services/server/devices/rgxfwutils.c",
    "pvrsrvkm/services/server/devices/rgxfwriscv.c",
    "pvrsrvkm/services/server/devices/rgxfwtrace_strings.c",
    "pvrsrvkm/services/server/devices/rgxkicksync.c",
    "pvrsrvkm/services/server/devices/rgxmem.c",
    "pvrsrvkm/services/server/devices/rgxmmuinit.c",
    "pvrsrvkm/services/server/devices/rgxpower.c",
    "pvrsrvkm/services/server/devices/rgxregconfig.c",
    "pvrsrvkm/services/server/devices/rgxregaccess.c",
    "pvrsrvkm/services/server/devices/rgxshader.c",
    "pvrsrvkm/services/server/devices/rgxsyncutils.c",
    "pvrsrvkm/services/server/devices/rgxtdmtransfer.c",
    "pvrsrvkm/services/server/devices/rgxtimecorr.c",
    "pvrsrvkm/services/server/devices/rgxutils.c",
    "pvrsrvkm/services/server/common/srvcore.c",
    "pvrsrvkm/services/shared/common/sync.c",
    "pvrsrvkm/services/server/common/sync_checkpoint.c",
    "pvrsrvkm/services/server/common/sync_server.c",
    "pvrsrvkm/services/server/common/sync_qbs.c",
    "pvrsrvkm/services/system/common/sysconfig_cmn.c",
    "pvrsrvkm/services/shared/common/tlclient.c",
    "pvrsrvkm/services/server/common/tlintern.c",
    "pvrsrvkm/services/server/common/tlserver.c",
    "pvrsrvkm/services/server/common/tlstream.c",
    "pvrsrvkm/services/server/devices/rgxtimerquery.c",
    "pvrsrvkm/services/shared/common/uniq_key_splay_tree.c",
    "pvrsrvkm/services/server/common/vmm_pvz_server.c",
    "pvrsrvkm/services/server/common/vmm_pvz_client.c",
    "pvrsrvkm/services/server/common/vz_vmm_pvz.c",
    "pvrsrvkm/services/server/common/vz_vmm_vm.c",
    "pvrsrvkm/services/server/devices/rgxhwperf_common.c",
    "pvrsrvkm/services/server/devices/hal/rgx/rgxfwimageutils.c",
    "pvrsrvkm/services/server/devices/hal/env/ree/rgxhal_driver_cmn.c",
]

POWERVR_VOLCANIC_SRCS = [
    "pvrsrvkm/services/server/devices/volcanic/rgxdebug.c",
    "pvrsrvkm/services/server/devices/volcanic/rgxhwperf.c",
    "pvrsrvkm/services/server/devices/volcanic/rgxinit.c",
    "pvrsrvkm/services/server/devices/volcanic/rgxmulticore.c",
    "pvrsrvkm/services/server/devices/hal/rgx/volcanic/rgxstartstop.c",
    "pvrsrvkm/services/server/devices/volcanic/rgxta3d.c",
    "pvrsrvkm/services/system/common/vmm_type_stub.c",
    "pvrsrvkm/services/server/devices/volcanic/rgxsrvinit.c",
    "pvrsrvkm/services/system/volcanic/common/env/linux/dma_support.c",
    "pvrsrvkm/generated/volcanic/di_bridge/server_di_bridge.c",
    "pvrsrvkm/generated/volcanic/mm_bridge/server_mm_bridge.c",
    "pvrsrvkm/generated/volcanic/mm_bridge/client_mm_direct_bridge.c",
    "pvrsrvkm/generated/volcanic/cmm_bridge/server_cmm_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxta3d_bridge/server_rgxta3d_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxcmp_bridge/server_rgxcmp_bridge.c",
    "pvrsrvkm/generated/volcanic/srvcore_bridge/server_srvcore_bridge.c",
    "pvrsrvkm/generated/volcanic/sync_bridge/server_sync_bridge.c",
    "pvrsrvkm/generated/volcanic/sync_bridge/client_sync_direct_bridge.c",
    "pvrsrvkm/generated/volcanic/cache_bridge/client_cache_direct_bridge.c",
    "pvrsrvkm/generated/volcanic/cache_bridge/server_cache_bridge.c",
    "pvrsrvkm/generated/volcanic/pvrtl_bridge/server_pvrtl_bridge.c",
    "pvrsrvkm/generated/volcanic/pvrtl_bridge/client_pvrtl_direct_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxfwdbg_bridge/server_rgxfwdbg_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxhwperf_bridge/server_rgxhwperf_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxregconfig_bridge/server_rgxregconfig_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxtimerquery_bridge/server_rgxtimerquery_bridge.c",
    "pvrsrvkm/generated/volcanic/rgxtq2_bridge/server_rgxtq2_bridge.c",
    "pvrsrvkm/generated/volcanic/synctracking_bridge/client_synctracking_direct_bridge.c",
    "pvrsrvkm/generated/volcanic/synctracking_bridge/server_synctracking_bridge.c",
    "pvrsrvkm/generated/volcanic/devicememhistory_bridge/server_devicememhistory_bridge.c",
    "pvrsrvkm/generated/volcanic/devicememhistory_bridge/client_devicememhistory_direct_bridge.c",
    "pvrsrvkm/services/server/devices/hal/env/ree/volcanic/rgxhal_driver.c",
]

SOC_SPECIFIC_COPTS = select({
    "//private/devices/google/common:soc_is_laguna": [
        '-DPIXEL_GPU_GENERATION="lga"',
        "-DGTC_FREQUENCY_HZ=38400000",
    ],
    "//private/devices/google/common:soc_is_malibu": [
        '-DPIXEL_GPU_GENERATION="mbu"',
        "-DGTC_FREQUENCY_HZ=38400000",
    ],
    "//conditions:default": [],
})
