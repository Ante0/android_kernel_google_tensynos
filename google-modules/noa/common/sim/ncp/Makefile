# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NOA Simulation driver
#

obj-$(CONFIG_NOA_SIM_SUPPORT) += ncp_sim.o

noa_dir=../../common
# NCP main process
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += ncp.o $(noa_dir)/ring.o $(noa_dir)/noatrace.o $(noa_dir)/core.o

# NCP WiFi Firmware
WLAN_DIR:=wlan
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/ncp_wlan_fw.o
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/buffer_manager.o
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/system_utility.o
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/nep_utility.o
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/device.o
ncp_sim-$(CONFIG_NOA_WLAN_QCA_SUPPORT) += $(WLAN_DIR)/device_qca.o
ncp_sim-$(CONFIG_NOA_WLAN_BRCM_SUPPORT) += $(WLAN_DIR)/device_brcm.o
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(WLAN_DIR)/trace.o

# NCP Modem Firmware
ncp_sim-$(CONFIG_NOA_SIM_SUPPORT) += md/samsung/ncp_md_fw.o

subdir-ccflags-y += \
	-g \
	-I$(SRC_DIR) \
	-I$(SRC_DIR)/sim \
	-I$(SRC_DIR)/sim/ncp
