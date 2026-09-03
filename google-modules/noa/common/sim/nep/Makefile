# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NEP Simulation driver
#

obj-$(CONFIG_NOA_SIM_SUPPORT) += nep_sim.o

noa_dir=../../common
# NEP simulator
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += nep.o netengine.o dma_processor.o dma_simulator.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += port_init.o ring_manager.o ring_controller.o ring_descriptor.o ring_manager_instance.o ring_shared_info_instance.o ring_manager_instance_singleton.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += nep_map_table.o nep_tables.o offload.o checksum.o netengine_utils.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += tethering_pipeline_service.o vpn_pipeline_service.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += util/ring_util.o util/memory_pool.o util/utilization_monitor.o

nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += $(noa_dir)/core.o $(noa_dir)/noatrace.o $(noa_dir)/ring.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += ring_service/ring_buffer_pool.o
nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += network_pipeline_framework/stage.o \
				network_pipeline_framework/packet_table.o \
				network_pipeline_framework/packet_initiator.o \
				network_pipeline_framework/task_scheduler.o \
				network_pipeline_framework/engine.o \
				network_pipeline_module/buffer_pool_engine.o \
				network_pipeline_module/dma_engine.o \
				network_pipeline_service/service.o

nep_sim-$(CONFIG_NOA_SIM_SUPPORT) += ring_pipeline_service/receiver.o \
				ring_pipeline_service/sender.o \
				ring_pipeline_service/feedthrough.o \
				ring_pipeline_service/obsolete_packet_handler.o

subdir-ccflags-y += \
	-g \
	-I$(SRC_DIR) \
	-I$(SRC_DIR)/sim \
	-I$(SRC_DIR)/sim/nep \
	-I$(SRC_DIR)/sim/nep/ring_service \
	-I$(SRC_DIR)/sim/common \
	-I$(SRC_DIR)/tether
