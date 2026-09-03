/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOG_MBA_GDMC_IFACE_H_
#define _GOOG_MBA_GDMC_IFACE_H_

struct device;
struct gdmc_iface;

/*
 * gdmc_iface_get - Get a gdmc_iface handle for this device
 * @dev:			device that requests the GDMC interace
 * Return:
 *	On success, a gdmc interface handle.
 *	-ENODEV if dev doesn't have a device node entry, the device node doesn't have a
 *		"gdmc-iface" property, or the property has an invalid phandle.
 *	-EPROBE_DEFER if the phandle is valid, but the gdmc interface has not yet been probed.
 *
 * It is expected that this function is called once when a GDMC client device is being probed
 * and the returned gdmc_iface is stored in the client driver's context struct.  The GDMC client
 * device driver should call gdmc_iface_put() it its remove() callback.
 */
struct gdmc_iface *gdmc_iface_get(struct device *dev);

/*
 * gdmc_iface_put - Put a gdmc_iface handle for this device
 * @gdmc_iface:			A GDMC interface handle previously returned from gdmc_iface_get().
 */
void gdmc_iface_put(struct gdmc_iface *dev);

/*
 * gdmc_host_cb_t - Callback invoked for GDMC-initiated transactions
 * @msg:			Pointer to the message payload received from the GDMC host.
 * @priv_data:			Client's private data (registered via `gdmc_register_host_cb`).
 *
 * Client call `gdmc_register_host_cb` to register callback. Client will be notified with
 * GDMC-initiated request.
 */
typedef void (*gdmc_host_cb_t)(void *msg, void *priv_data);

/*
 * gdmc_register_host_cb - Registers a callback function to handle GDMC-initiated requests for a
 *			   specific service ID.
 *
 * @gdmc_iface:			A handle to the GDMC interface.
 * @service_id:			The unique identifier of the service for which the callback is
 *				registered.
 * @host_cb:			The callback function to be invoked when GDMC initiates a request
 *				for the specified service.
 * @priv_data:			Optional private data that will be passed to the callback function.
 *
 * Return:			0 on successful registration.
 *				-EBUSY: if a callback is already registered for the same service ID.
 *				-EINVAL: if an invalid service ID is provided.
 */
int gdmc_register_host_cb(struct gdmc_iface *gdmc_iface, int service_id,
			  gdmc_host_cb_t host_cb, void *priv_data);

/*
 * gdmc_unregister_host_cb - Unregister to specific @service_id.
 * @gdmc_iface:			A gdmc interface handle.
 * @service_id:			Specific service ID to remove callback.
 */
void gdmc_unregister_host_cb(struct gdmc_iface *gdmc_iface, int service_id);

/*
 * gdmc_send_message - Send message through GDMC normal channels
 * @gdmc_iface:			A gdmc interface handle.
 * @msg:			Message buffer to be sent to GDMC host.
 *				Message buffer will be overwrite for responsed message.
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_send_message(struct gdmc_iface *gdmc_iface, void *msg);

/*
 * gdmc_async_resp_cb_t - Async callback function type for GDMC response
 * @resp_msg:			Response Message buffer received from GDMC host.
 * @priv_data:			Client's private data that was passed to gdmc_send_message_async()
 *				and gdmc_send_message_critical_async().
 */
typedef void (*gdmc_async_resp_cb_t)(void *resp_msg, void *priv_data);

/*
 * gdmc_send_message_async - Asynchronously send message through GDMC normal channels
 * @gdmc_iface:			A gdmc interface handle.
 * @msg:			Message buffer to be sent to GDMC host.
 * @resp_cb:			Async callback function to be invoked on response from GDMC host.
 * @priv_data:			Client's private data
 *
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_send_message_async(struct gdmc_iface *gdmc_iface,
			    void *msg,
			    gdmc_async_resp_cb_t resp_cb,
			    void *priv_data);

/*
 * gdmc_send_message_critical - Send message through GDMC critical channels
 * @gdmc_iface:			A gdmc interface handle.
 * @msg:			Message buffer to be sent to GDMC host.
 *				Message buffer will be overwrite for responsed message.
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_send_message_critical(struct gdmc_iface *gdmc_iface, void *msg);

/*
 * gdmc_send_message_critical_async - Asynchronously send message through GDMC critical channels
 *
 * Refer gdmc_send_message_async(), only difference is that this API would use GDMC critical
 * channels.
 */
int gdmc_send_message_critical_async(struct gdmc_iface *gdmc_iface,
				     void *msg,
				     gdmc_async_resp_cb_t resp_cb,
				     void *priv_data);

/*
 * gdmc_ping - ping gdmc service and get an incremental result
 * @gdmc_iface:			A gdmc interface handle.
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_ping(struct gdmc_iface *gdmc_iface);

/*
 * gdmc_ehld_timer_config - Enable or Disable GDMC Periodic Timer
 * @gdmc_iface:			A gdmc interface handle.
 * @cmd				Enable, Disable, set parameters or set pmu counter id
 * @msg:			Message buffer to be sent to GDMC host.
 * @dev:			Device data
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_ehld_config(struct gdmc_iface *gdmc_iface, int cmd, void *msg, struct device *dev);

/*
 * gdmc_reboot_with_reason - Trigger warm reboot with reason
 * @gdmc_iface:			A gdmc interface handle.
 * @cmd				Reboot type
 * @msg:			Message buffer to be sent to GDMC host.
 * @dev:			Device data
 * Return:			0 for succeeded; Negative value for failure.
 */
int gdmc_reboot_with_reason(struct gdmc_iface *gdmc_iface, int cmd, void *msg, struct device *dev);

typedef void (*gdmc_aoc_reset_cb_t)(void *reg_dump, unsigned int reg_dump_len, void *priv_data);

/*
 * gdmc_register_aoc_reset_notifier() - registers aoc reset callback notifier
 * @gdmc_iface:			A gdmc interface handle.
 * @aoc_reset_cb:		Function to be called when GDMC detects that AOC has encountered
 *				watchdog reset.
 * @prv_data:			priv_data of the service provider, this will be passed back as
 *				argument to @aoc_reset_cb.
 *
 * Client registers a callback function of type gdmc_aoc_reset_cb_t.
 * This callback function will be invoked whenever an AoC reset occurs.
 * If register dump data is available, the @reg_dump parameter will point to a memory region
 * containing the register data dumped from the AoC by GDMC. Otherwise, @NULL will be provided.
 */
int gdmc_register_aoc_reset_notifier(struct gdmc_iface *gdmc_iface,
				     gdmc_aoc_reset_cb_t aoc_reset_cb,
				     void *prv_data);

/*
 * gdmc_unregister_aoc_reset_notifier - Unregister aoc reset notification
 * @gdmc_iface:			A gdmc interface handle.
 */
void gdmc_unregister_aoc_reset_notifier(struct gdmc_iface *gdmc_iface);

typedef void (*gdmc_dpa_reset_cb_t)(void *reg_dump, unsigned int reg_dump_len, void *priv_data);
/*
 * gdmc_register_dpa_reset_notifier() - registers dpa reset callback notifier
 * @gdmc_iface:			A gdmc interface handle.
 * @dpa_reset_cb:		Function to be called when GDMC detects that DPA has encountered
 *				watchdog reset.
 * @prv_data:			priv_data of the service provider, this will be passed back as
 *				argument to @dpa_reset_cb.
 *
 * Client registers a callback function of type gdmc_dpa_reset_cb_t.
 * This callback function will be invoked whenever a DPA reset occurs.
 * If register dump data is available, the @reg_dump parameter will point to a memory region
 * containing the register data dumped from the DPA by GDMC. Otherwise, @NULL will be provided.
 */
int gdmc_register_dpa_reset_notifier(struct gdmc_iface *gdmc_iface,
				     gdmc_dpa_reset_cb_t dpa_reset_cb,
				     void *prv_data);

/*
 * gdmc_unregister_dpa_reset_notifier - Unregister dpa reset notification
 * @gdmc_iface:			A gdmc interface handle.
 */
void gdmc_unregister_dpa_reset_notifier(struct gdmc_iface *gdmc_iface);

/*
 * struct gdmc_dhub_iface - Interface for DHUB mailbox operations.
 *
 * dhub_mux_get: query DHUB UART mux setting
 * @dhub_iface:			A DHUB interface handle.
 * @uart_num:			On success, filled with uart_mux setting.
 * Return:			0 on success; Negative value on failure.
 *
 * dhub_mux_set: Set the UART mux.
 * @dhub_iface:			A DHUB interface handle.
 * @uart_num:			The new UART mux setting.
 * Return:			0 on success; Negative value on failure.
 *
 * dhub_baudrate_get - query UART baudrate in DHUB
 * @dhub_iface:			A DHUB interface handle.
 * @uart_num:			The UART whose baudrate to query
 * @baudrate:			On success, filled with current baudrate.
 * Return:			0 on success; Negative value on failure.

 * dhub_baudrate_set - set UART baudrate in DHUB
 * @dhub_iface:			A DHUB interface handle.
 * @uart_num:			The UART to change.
 * @baudrate:			The new baudrate.
 * Return:			0 on success; Negative value on failure.
 *
 * Note: This command only updates DHUB baudrate registers, it does not
 * propagate baudrate changes to the corresponding selected source.
 * Care must be take to ensure that the DHUB's registers are configured to
 * match the baudrate of the corresponding subsystem's UART.

 * dhub_virt_en_get - query which UARTs are enabled for virtualization
 * @dhub_iface:			A DHUB interface handle.
 * @mask:			On success, the bit mask of enabled virtual UARTs.
 * Return:			0 on success; Negative value on failure.

 * dhub_virt_en_set - select which UARTs are enabled for virtualization
 * @dhub_iface:			A DHUB interface handle.
 * @mask:			A new bit mask of enabled virtual UARTs.
 * Return:			0 on success; Negative value on failure.
 */
struct gdmc_dhub_iface {
	int (*dhub_mux_get)(struct gdmc_dhub_iface *dhub_iface, u32 *uart_id);
	int (*dhub_mux_set)(struct gdmc_dhub_iface *dhub_iface, u32 uart_id);
	int (*dhub_baudrate_get)(struct gdmc_dhub_iface *dhub_iface, u32 uart_num, u32 *baudrate);
	int (*dhub_baudrate_set)(struct gdmc_dhub_iface *dhub_iface, u32 uart_num, u32 baudrate);
	int (*dhub_virt_en_get)(struct gdmc_dhub_iface *dhub_iface, u32 *mask);
	int (*dhub_virt_en_set)(struct gdmc_dhub_iface *dhub_iface, u32 mask);
};

/*
 * dhub_iface_get - Get a gdmc_dhub_iface handle
 * @dev:			device that requests the DHUB interface
 * Return:
 *	On success, a dhub interface handle.
 *	-ENODEV if dev doesn't have a device node entry, the device node doesn't have a
 *		"gdmc-iface" property, or the property has an invalid phandle.
 *	-EPROBE_DEFER if the phandle is valid, but the gdmc interface has not yet been probed.
 *
 * It is expected that this function is called once when a DHUB client device is being probed
 * and the returned dhub_iface is stored in the client driver's context struct. The DHUB client
 * device driver should call gdmc_iface_put() it its remove() callback.
 */
struct gdmc_dhub_iface *dhub_iface_get(struct device *dev);

/*
 * dhub_iface_put - Put a gdmc_dhub_iface handle
 * @dhub_iface:			A DHUB interface handle previously returned from dhub_iface_get().
 */
void dhub_iface_put(struct gdmc_dhub_iface *dhub_iface);

#endif /* _GOOG_MBA_GDMC_IFACE_H_ */
