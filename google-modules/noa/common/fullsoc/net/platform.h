/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Common Network Layer Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_NET_PLATFORM_H__
#define __LVM_NET_PLATFORM_H__

#include <linux/platform_device.h>
#include <buffer/manager.h>
#include <net/netdev.h>


#define LVM_CLASS_NAME			"lvm"

struct lvm_platform_driver;

/**
 * enum lvm_state - LVM framework state machine
 * @LVM_STATE_IDLE: LVM framework is idle
 * @LVM_STATE_FRAMEWORK_READY: LVM framework is ready
 * @LVM_STATE_DRIVER_READY: LVM underlying driver is ready
 * @LVM_STATE_INTERFACE_RUNNING: LVM network interface is running
 * @LVM_STATE_DATA_TRANSFERRING: LVM network interface is transferring data
 * @__LVM_STATE_MAX: Number of LVM framework states
 */
enum lvm_state {
	LVM_STATE_IDLE,
	LVM_STATE_FRAMEWORK_READY,
	LVM_STATE_DRIVER_READY,
	LVM_STATE_INTERFACE_RUNNING,
	LVM_STATE_DATA_TRANSFERRING,

	__LVM_STATE_MAX,
};

/**
 * struct lvm_platform_device - LVM platform device structure
 * @id: Platform device ID
 * @dev: Pointer to the device structure
 */
struct lvm_platform_device {
	int				id;
	struct device			*dev;
};

/**
 * struct lvm_platform_driver_ops- LVM platform driver operations
 * @probe: The function to probe the platform driver
 * @remove: The function to remove the platform driver
 * @get_state: The function to get the platform driver state
 * @switch_state: The function to switch the platform driver state
 * @dump_reg: The function to dump hardware registers in the platform driver
 */
struct lvm_platform_driver_ops {
	int		(*probe)(struct lvm_platform_device *pdev);
	int		(*remove)(struct lvm_platform_device *pdev);
	const char *	(*get_state)(struct lvm_platform_driver *driver);
	int		(*switch_state)(struct lvm_platform_driver *driver,
					int new_state);
	ssize_t (*dump_reg)(struct lvm_platform_driver *driver, char *buf);
};

/**
 * struct lvm_platform_driver - LVM platform driver structure
 * @state: Current state of the platform driver
 * @name: Name of the platform driver
 * @kobj: Pointer to the kernel object for this driver
 * @pdev: Pointer to the LVM platform device structure
 * @netdev: Pointer to the LVM network device structure
 * @rx_manager: Pointer to the receive buffer manager
 * @tx_manager: Pointer to the transmit buffer manager
 * @ops: Pointer to the platform driver operations
 */
struct lvm_platform_driver {
	int				state;
	const char			*name;
	struct kobject			*kobj;
	struct lvm_platform_device	*pdev;
	struct lvm_netdev		*netdev;
	struct lvm_manager		*rx_manager;
	struct lvm_manager		*tx_manager;
	struct lvm_platform_driver_ops	*ops;
};


/**
 * struct lvm_net - Global LVM network structure
 * @state: Current state of the LVM framework
 * @class: Pointer to the sysfs class for LVM framework
 * @driver: Pointer to the LVM platform driver structure
 * @pktgen: Pointer to the LVM packet generator structure
 */
struct lvm_net {
	enum lvm_state			state;
	struct class			*class;
	struct lvm_platform_driver	*driver;
	struct lvm_pktgen		*pktgen;
};


int lvm_platform_probe(struct lvm_platform_driver *driver);
int lvm_platform_remove(struct lvm_platform_driver *driver);
int lvm_manager_register(struct lvm_platform_driver *driver,
			 struct lvm_manager *manager);
void lvm_manager_unregister(struct lvm_platform_driver *driver,
			    struct lvm_manager *manager);
int lvm_netdev_register(struct lvm_platform_driver *driver,
			struct lvm_netdev *netdev);
void lvm_netdev_unregister(struct lvm_platform_driver *driver);
int lvm_platform_driver_register(struct lvm_platform_driver *driver);
void lvm_platform_driver_unregister(struct lvm_platform_driver *driver);
int lvm_state_transit(enum lvm_state new_state);
int lvm_net_framework_init(void);
void lvm_net_framework_deinit(void);

#endif  /* __LVM_NET_PLATFORM_H__ */
