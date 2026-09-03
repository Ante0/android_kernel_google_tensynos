// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2006 Intel Corp.
 *  Tom Long Nguyen (tom.l.nguyen@intel.com)
 *  Zhang Yanmin (yanmin.zhang@intel.com)
 *
 * Override some functionality from upstream pci.c.
 * Ideally, code in here is structured such that it may eventually fit
 * within the upstream driver if/when the underlying hardware driver/feature
 * can be integrated upstream.
 * Due to the limitation of accessing all the kernel code, and some use case
 * we are not run in our use case, here is the list of what we have simplified.
 * Marked out the pci_bus_sem down_read / up_read in the
 * pci_bridge_wait_for_secondary_bus(), this shall be a concern, but it
 * is a global parameter that we can't copy, so this is a risk we can't
 * avoid. And given that resilience is against hot-unplug scenario, it
 * shall not impact us.
 * The main reason we did all of this is that we can have reset_root_port
 * callback binding in our own structure.
 * Stripe out some of the code since they are not for root-port base AER
 * flow.
 */

#include <linux/aer.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pcie-designware.h"
#include "pcie-google.h"
#include "pcie-pci-customized.h"

#define PCI_RESET_WAIT 1000 /* msec */
#define PCIE_RESET_READY_POLL_MS 60000 /* msec */

static void google_pcie_clear_device_status(struct pci_dev *dev)
{
	u16 sta;

	pcie_capability_read_word(dev, PCI_EXP_DEVSTA, &sta);
	pcie_capability_write_word(dev, PCI_EXP_DEVSTA, sta);
}

/* Do any devices on or below this bus prevent a bus reset? */
static bool pci_bus_resettable(struct pci_bus *bus)
{
	struct pci_dev *dev;

	if (bus->self && (bus->self->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET))
		return false;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET ||
		    (dev->subordinate && !pci_bus_resettable(dev->subordinate)))
			return false;
	}

	return true;
}

/* Lock devices from the top of the tree down */
static void pci_bus_lock(struct pci_bus *bus)
{
	struct pci_dev *dev;

	pci_dev_lock(bus->self);
	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate)
			pci_bus_lock(dev->subordinate);
		else
			pci_dev_lock(dev);
	}
}

/* Unlock devices from the bottom of the tree up */
static void pci_bus_unlock(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate)
			pci_bus_unlock(dev->subordinate);
		else
			pci_dev_unlock(dev);
	}
	pci_dev_unlock(bus->self);
}

static void google_pcibios_reset_secondary_bus(struct pci_dev *dev)
{
	struct pci_host_bridge *host = pci_find_host_bridge(dev->bus);
	struct pci_bus *bus = host->bus;
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	int ret;

	if (!gpcie) {
		pci_err(dev, "gpcie Null pointer\n");
		return;
	}

	ret = gpcie->reset_root_port(host, dev);
	if (ret) {
		pci_err(dev, "Failed to reset Root Port: %d\n", ret);
		return;
	}
	/*
	 * Now restore the previously saved on success.
	 * Setting state_saved to true is a workaround to re-use the
	 * previously saved one.
	 */
	dev->state_saved = true;
	pci_restore_state(dev);
}

/*
 * Find maximum D3cold delay required by all the devices on the bus.  The
 * spec says 100 ms, but firmware can lower it and we allow drivers to
 * increase it as well.
 *
 * Called with @pci_bus_sem locked for reading.
 */
static int pci_bus_max_d3cold_delay(const struct pci_bus *bus)
{
	const struct pci_dev *pdev;
	int min_delay = 100;
	int max_delay = 0;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		if (pdev->d3cold_delay < min_delay)
			min_delay = pdev->d3cold_delay;
		if (pdev->d3cold_delay > max_delay)
			max_delay = pdev->d3cold_delay;
	}

	return max(min_delay, max_delay);
}

/**
 * pcie_wait_for_link_status - Wait for link status change
 * @pdev: Device whose link to wait for.
 * @use_lt: Use the LT bit if TRUE, or the DLLLA bit if FALSE.
 * @active: Waiting for active or inactive?
 *
 * Return 0 if successful, or -ETIMEDOUT if status has not changed within
 * PCIE_LINK_RETRAIN_TIMEOUT_MS milliseconds.
 */
static int pcie_wait_for_link_status(struct pci_dev *pdev,
				     bool use_lt, bool active)
{
	u16 lnksta_mask, lnksta_match;
	unsigned long end_jiffies;
	u16 lnksta;

	lnksta_mask = use_lt ? PCI_EXP_LNKSTA_LT : PCI_EXP_LNKSTA_DLLLA;
	lnksta_match = active ? lnksta_mask : 0;

	end_jiffies = jiffies + msecs_to_jiffies(PCIE_LINK_RETRAIN_TIMEOUT_MS);
	do {
		pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnksta);
		if ((lnksta & lnksta_mask) == lnksta_match)
			return 0;
		msleep(1);
	} while (time_before(jiffies, end_jiffies));

	return -ETIMEDOUT;
}

/**
 * pcie_wait_for_link_delay - Wait until link is active or inactive
 * @pdev: Bridge device
 * @active: waiting for active or inactive?
 * @delay: Delay to wait after link has become active (in ms)
 *
 * Use this to wait till link becomes active or inactive.
 */
static bool pcie_wait_for_link_delay(struct pci_dev *pdev, bool active,
				     int delay)
{
	int rc;

	/*
	 * Some controllers might not implement link active reporting. In this
	 * case, we wait for 1000 ms + any delay requested by the caller.
	 */
	if (!pdev->link_active_reporting) {
		msleep(PCIE_LINK_RETRAIN_TIMEOUT_MS + delay);
		return true;
	}

	/*
	 * PCIe r4.0 sec 6.6.1, a component must enter LTSSM Detect within 20ms,
	 * after which we should expect the link to be active if the reset was
	 * successful. If so, software must wait a minimum 100ms before sending
	 * configuration requests to devices downstream this port.
	 *
	 * If the link fails to activate, either the device was physically
	 * removed or the link is permanently failed.
	 */
	if (active)
		msleep(20);
	rc = pcie_wait_for_link_status(pdev, false, active);
	if (active) {
		if (rc)
			return false;

		msleep(delay);
		return true;
	}

	if (rc)
		return false;

	return true;
}

static int pci_dev_wait(struct pci_dev *dev, char *reset_type, int timeout)
{
	int delay = 1;
	bool retrain = false;
	struct pci_dev *root, *bridge;

	root = pcie_find_root_port(dev);

	if (pci_is_pcie(dev)) {
		bridge = pci_upstream_bridge(dev);
		if (bridge)
			retrain = true;
	}

	/*
	 * The caller has already waited long enough after a reset that the
	 * device should respond to config requests, but it may respond
	 * with Request Retry Status (RRS) if it needs more time to
	 * initialize.
	 *
	 * If the device is below a Root Port with Configuration RRS
	 * Software Visibility enabled, reading the Vendor ID returns a
	 * special data value if the device responded with RRS.  Read the
	 * Vendor ID until we get non-RRS status.
	 *
	 * If there's no Root Port or Configuration RRS Software Visibility
	 * is not enabled, the device may still respond with RRS, but
	 * hardware may retry the config request.  If no retries receive
	 * Successful Completion, hardware generally synthesizes ~0
	 * (PCI_ERROR_RESPONSE) data to complete the read.  Reading Vendor
	 * ID for VFs and non-existent devices also returns ~0, so read the
	 * Command register until it returns something other than ~0.
	 */
	for (;;) {
		u32 id;

		if (pci_dev_is_disconnected(dev)) {
			pci_dbg(dev, "disconnected; not waiting\n");
			return -ENOTTY;
		}

		if (root && root->config_rrs_sv) {
			pci_read_config_dword(dev, PCI_VENDOR_ID, &id);
			if (!pci_bus_rrs_vendor_id(id))
				break;
		} else {
			pci_read_config_dword(dev, PCI_COMMAND, &id);
			if (!PCI_POSSIBLE_ERROR(id))
				break;
		}

		if (delay > timeout) {
			pci_warn(dev, "not ready %dms after %s; giving up\n",
				 delay - 1, reset_type);
			return -ENOTTY;
		}

		if (delay > PCI_RESET_WAIT) {
			if (retrain) {
				retrain = false;
				/*
				 * pcie_failed_link_retrain is a quirk for
				 * certain device which we are using, so
				 * delete the check here.
				 */
			}
			pci_info(dev, "not ready %dms after %s; waiting\n",
				 delay - 1, reset_type);
		}

		msleep(delay);
		delay *= 2;
	}

	if (delay > PCI_RESET_WAIT)
		pci_info(dev, "ready %dms after %s\n", delay - 1,
			 reset_type);
	else
		pci_dbg(dev, "ready %dms after %s\n", delay - 1,
			reset_type);

	return 0;
}

/**
 * pci_bridge_wait_for_secondary_bus - Wait for secondary bus to be accessible
 * @dev: PCI bridge
 * @reset_type: reset type in human-readable form
 *
 * Handle necessary delays before access to the devices on the secondary
 * side of the bridge are permitted after D3cold to D0 transition
 * or Conventional Reset.
 *
 * For PCIe this means the delays in PCIe 5.0 section 6.6.1. For
 * conventional PCI it means Tpvrh + Trhfa specified in PCI 3.0 section
 * 4.3.2.
 *
 * Return 0 on success or -ENOTTY if the first device on the secondary bus
 * failed to become accessible.
 */
int pci_bridge_wait_for_secondary_bus(struct pci_dev *dev, char *reset_type)
{
	struct pci_dev *child __free(pci_dev_put) = NULL;
	int delay;

	if (pci_dev_is_disconnected(dev))
		return 0;

	if (!pci_is_bridge(dev))
		return 0;

	/*
	 * We only deal with devices that are present currently on the bus.
	 * For any hot-added devices the access delay is handled in pciehp
	 * board_added(). In case of ACPI hotplug the firmware is expected
	 * to configure the devices before OS is notified.
	 */
	if (!dev->subordinate || list_empty(&dev->subordinate->devices))
		return 0;

	/* Take d3cold_delay requirements into account */
	delay = pci_bus_max_d3cold_delay(dev->subordinate);
	if (!delay)
		return 0;

	child = pci_dev_get(list_first_entry(&dev->subordinate->devices,
					     struct pci_dev, bus_list));

	/*
	 * For PCIe downstream and root ports that do not support speeds
	 * greater than 5 GT/s need to wait minimum 100 ms. For higher
	 * speeds (gen3) we need to wait first for the data link layer to
	 * become active.
	 *
	 * However, 100 ms is the minimum and the PCIe spec says the
	 * software must allow at least 1s before it can determine that the
	 * device that did not respond is a broken device. Also device can
	 * take longer than that to respond if it indicates so through Request
	 * Retry Status completions.
	 *
	 * Therefore we wait for 100 ms and check for the device presence
	 * until the timeout expires.
	 */

	if (pcie_get_speed_cap(dev) <= PCIE_SPEED_5_0GT) {
		u16 status;

		pci_dbg(dev, "waiting %d ms for downstream link\n", delay);
		msleep(delay);

		if (!pci_dev_wait(child, reset_type, PCI_RESET_WAIT - delay))
			return 0;

		/*
		 * If the port supports active link reporting we now check
		 * whether the link is active and if not bail out early with
		 * the assumption that the device is not present anymore.
		 */
		if (!dev->link_active_reporting)
			return -ENOTTY;

		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &status);
		if (!(status & PCI_EXP_LNKSTA_DLLLA))
			return -ENOTTY;

		return pci_dev_wait(child, reset_type,
				    PCIE_RESET_READY_POLL_MS - PCI_RESET_WAIT);
	}

	pci_dbg(dev, "waiting %d ms for downstream link, after activation\n",
		delay);
	if (!pcie_wait_for_link_delay(dev, true, delay)) {
		/* Did not train, no need to wait any further */
		pci_info(dev, "Data Link Layer Link Active not set in %d msec\n", delay);
		return -ENOTTY;
	}

	return pci_dev_wait(child, reset_type,
			    PCIE_RESET_READY_POLL_MS - delay);
}

static int google_pci_bridge_secondary_bus_reset(struct pci_dev *dev)
{
	if (!dev->block_cfg_access)
		pci_warn_once(dev, "unlocked secondary bus reset via: %pS\n",
			      __builtin_return_address(0));
	google_pcibios_reset_secondary_bus(dev);

	return pci_bridge_wait_for_secondary_bus(dev, "bus reset");
}

static int pci_bus_reset(struct pci_bus *bus, bool probe)
{
	int ret;

	if (!bus->self || !pci_bus_resettable(bus))
		return -ENOTTY;

	if (probe)
		return 0;

	pci_bus_lock(bus);

	might_sleep();

	ret = google_pci_bridge_secondary_bus_reset(bus->self);

	pci_bus_unlock(bus);

	return ret;
}

static int google_pci_bus_error_reset(struct pci_dev *bridge)
{
	/*
	 * bus->slots in our use case is empty, so simplified the flow.
	 */
	return pci_bus_reset(bridge->subordinate, PCI_RESET_DO_RESET);
}

static pci_ers_result_t pci_host_reset_root_port(struct pci_dev *dev)
{
	int ret;

	ret = google_pci_bus_error_reset(dev);
	if (ret) {
		pci_err(dev, "Failed to reset Root Port: %d\n", ret);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_info(dev, "Root Port has been reset\n");

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t merge_result(enum pci_ers_result orig,
				  enum pci_ers_result new)
{
	if (new == PCI_ERS_RESULT_NO_AER_DRIVER)
		return PCI_ERS_RESULT_NO_AER_DRIVER;

	if (new == PCI_ERS_RESULT_NONE)
		return orig;

	switch (orig) {
	case PCI_ERS_RESULT_CAN_RECOVER:
	case PCI_ERS_RESULT_RECOVERED:
		orig = new;
		break;
	case PCI_ERS_RESULT_DISCONNECT:
		if (new == PCI_ERS_RESULT_NEED_RESET)
			orig = PCI_ERS_RESULT_NEED_RESET;
		break;
	default:
		break;
	}

	return orig;
}

static int report_error_detected(struct pci_dev *dev,
				 pci_channel_state_t state,
				 enum pci_ers_result *result)
{
	struct pci_driver *pdrv;
	pci_ers_result_t vote;
	const struct pci_error_handlers *err_handler;

	device_lock(&dev->dev);
	pdrv = dev->driver;
	if (pci_dev_is_disconnected(dev)) {
		vote = PCI_ERS_RESULT_DISCONNECT;
	} else if (!pci_dev_set_io_state(dev, state)) {
		pci_info(dev, "can't recover (state transition %u -> %u invalid)\n",
			dev->error_state, state);
		vote = PCI_ERS_RESULT_NONE;
	} else if (!pdrv || !pdrv->err_handler ||
		   !pdrv->err_handler->error_detected) {
		/*
		 * If any device in the subtree does not have an error_detected
		 * callback, PCI_ERS_RESULT_NO_AER_DRIVER prevents subsequent
		 * error callbacks of "any" device in the subtree, and will
		 * exit in the disconnected error state.
		 */
		if (dev->hdr_type != PCI_HEADER_TYPE_BRIDGE) {
			vote = PCI_ERS_RESULT_NO_AER_DRIVER;
			pci_info(dev, "can't recover (no error_detected callback)\n");
		} else {
			vote = PCI_ERS_RESULT_NONE;
		}
	} else {
		err_handler = pdrv->err_handler;
		vote = err_handler->error_detected(dev, state);
	}
	*result = merge_result(*result, vote);
	device_unlock(&dev->dev);
	return 0;
}

static int report_resume(struct pci_dev *dev, void *data)
{
	struct pci_driver *pdrv;
	const struct pci_error_handlers *err_handler;

	device_lock(&dev->dev);
	pdrv = dev->driver;
	if (!pci_dev_set_io_state(dev, pci_channel_io_normal) ||
	    !pdrv || !pdrv->err_handler || !pdrv->err_handler->resume)
		goto out;

	err_handler = pdrv->err_handler;
	err_handler->resume(dev);
out:
	device_unlock(&dev->dev);
	return 0;
}

static int report_slot_reset(struct pci_dev *dev, void *data)
{
	struct pci_driver *pdrv;
	pci_ers_result_t vote, *result = data;
	const struct pci_error_handlers *err_handler;

	device_lock(&dev->dev);
	pdrv = dev->driver;
	if (!pci_dev_set_io_state(dev, pci_channel_io_normal) ||
	    !pdrv || !pdrv->err_handler || !pdrv->err_handler->slot_reset)
		goto out;

	err_handler = pdrv->err_handler;
	vote = err_handler->slot_reset(dev);
	*result = merge_result(*result, vote);
out:
	device_unlock(&dev->dev);
	return 0;
}

static int report_mmio_enabled(struct pci_dev *dev, void *data)
{
	struct pci_driver *pdrv;
	pci_ers_result_t vote, *result = data;
	const struct pci_error_handlers *err_handler;

	device_lock(&dev->dev);
	pdrv = dev->driver;
	if (!pdrv || !pdrv->err_handler || !pdrv->err_handler->mmio_enabled)
		goto out;

	err_handler = pdrv->err_handler;
	vote = err_handler->mmio_enabled(dev);
	*result = merge_result(*result, vote);
out:
	device_unlock(&dev->dev);
	return 0;
}

static int pci_pm_runtime_get_sync(struct pci_dev *pdev, void *data)
{
	pm_runtime_get_sync(&pdev->dev);
	return 0;
}

static int pci_pm_runtime_put(struct pci_dev *pdev, void *data)
{
	pm_runtime_put(&pdev->dev);
	return 0;
}

static int report_frozen_detected(struct pci_dev *dev, void *data)
{
	return report_error_detected(dev, pci_channel_io_frozen, data);
}

/**
 * pci_walk_bridge - walk bridges potentially AER affected
 * @bridge:	bridge which may be a Port, an RCEC, or an RCiEP
 * @cb:		callback to be called for each device found
 * @userdata:	arbitrary pointer to be passed to callback
 *
 * If the device provided is a bridge, walk the subordinate bus, including
 * any bridged devices on buses under this bus.  Call the provided callback
 * on each device found.
 *
 * If the device provided has no subordinate bus, e.g., an RCEC or RCiEP,
 * call the callback on the device itself.
 */
static void pci_walk_bridge(struct pci_dev *bridge,
			    int (*cb)(struct pci_dev *, void *),
			    void *userdata)
{
	if (bridge->subordinate)
		pci_walk_bus(bridge->subordinate, cb, userdata);
	else
		cb(bridge, userdata);
}

static pci_ers_result_t google_pcie_do_recovery(struct pci_dev *dev,
		pci_ers_result_t (*reset_subordinates)(struct pci_dev *pdev))
{
	struct pci_dev *bridge;
	pci_ers_result_t status = PCI_ERS_RESULT_CAN_RECOVER;

	/*
	 * We only detect error on the Rootport so the dev will be the
	 * bridge device.
	 */
	bridge = dev;
	pci_walk_bridge(bridge, pci_pm_runtime_get_sync, NULL);

	pci_dbg(bridge, "broadcast error_detected message\n");

	pci_walk_bridge(bridge, report_frozen_detected, &status);
	if (reset_subordinates(bridge) != PCI_ERS_RESULT_RECOVERED) {
		pci_warn(bridge, "subordinate device reset failed\n");
		goto failed;
	}


	if (status == PCI_ERS_RESULT_CAN_RECOVER) {
		status = PCI_ERS_RESULT_RECOVERED;
		pci_dbg(bridge, "broadcast mmio_enabled message\n");
		pci_walk_bridge(bridge, report_mmio_enabled, &status);
	}

	if (status == PCI_ERS_RESULT_NEED_RESET) {
		status = PCI_ERS_RESULT_RECOVERED;
		pci_dbg(bridge, "broadcast slot_reset message\n");
		pci_walk_bridge(bridge, report_slot_reset, &status);
	}

	if (status != PCI_ERS_RESULT_RECOVERED)
		goto failed;

	pci_dbg(bridge, "broadcast resume message\n");
	pci_walk_bridge(bridge, report_resume, &status);
	google_pcie_clear_device_status(dev);
	pci_aer_clear_nonfatal_status(dev);

	pci_walk_bridge(bridge, pci_pm_runtime_put, NULL);

	pci_info(bridge, "device recovery successful\n");
	return status;

failed:
	pci_walk_bridge(bridge, pci_pm_runtime_put, NULL);

	pci_info(bridge, "device recovery failed\n");

	return status;
}

static void pci_host_recover_root_port(struct pci_dev *port)
{
	google_pcie_do_recovery(port, pci_host_reset_root_port);
}

void google_pci_host_handle_link_down(struct pci_dev *port)
{
	pci_info(port, "Recovering Root Port due to Link Down\n");
	pci_host_recover_root_port(port);
}
