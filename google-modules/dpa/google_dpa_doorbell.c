// SPDX-License-Identifier: GPL-2.0-only
/*
 * Doorbell driver for DPA
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <soc/google/google_dpa_doorbell.h>

#include "google_dpa_internal.h"
#include "google_dpa_power_domain.h"

#define GOOGLE_DPA_DOORBELL_ITR_OFFSET 0x20
#define GOOGLE_DPA_DOORBELL_IMR_OFFSET 0x24
#define GOOGLE_DPA_DOORBELL_ISR_OFFSET 0x28

struct dpa_doorbell_handler {
	doorbell_cb_t callback;
	void *data;
};

struct google_dpa_doorbell {
	struct device *dev;
	void __iomem *base;
	int irq;
	unsigned long imr;
	raw_spinlock_t lock;
	struct dpa_doorbell_handler handlers[BITS_PER_TYPE(u32)];
	struct device *pd_vdev;
	struct device_link *pd_link;
};

static inline void google_dpa_doorbell_lock_process_context(raw_spinlock_t *lock,
							    unsigned long *flags)
{
	unsigned long local_flags;
	/*
	 * Why *_irqsave* version of the spinlock? Because, *_irqsave() variant guarantees that
	 * other CPU or interrupt will not be able to preempt the flow. This is very important for
	 * a function which is running in process context, but its critical section is accessed from
	 * interrupt context too (i.e. trigger & mask register). If interrupts are allowed, then
	 * the same lock can be re-acquired by the handler on the same CPU, a classic deadlock.
	 */
	raw_spin_lock_irqsave(lock, local_flags);
	*flags = local_flags;
}

static inline void google_dpa_doorbell_unlock_process_context(raw_spinlock_t *lock,
							      unsigned long *flags)
{
	raw_spin_unlock_irqrestore(lock, *flags);
}

static void google_dpa_doorbell_mask_update(struct google_dpa_doorbell *doorbell, u8 id, bool set)
{
	unsigned long flags;

	pm_runtime_get_sync(doorbell->dev);
	google_dpa_doorbell_lock_process_context(&doorbell->lock, &flags);

	__assign_bit(id, &doorbell->imr, set);
	writel(doorbell->imr, doorbell->base + GOOGLE_DPA_DOORBELL_IMR_OFFSET);

	google_dpa_doorbell_unlock_process_context(&doorbell->lock, &flags);
	pm_runtime_put(doorbell->dev);
}

void google_dpa_doorbell_mask(struct google_dpa_doorbell *doorbell, u8 id)
{
	google_dpa_doorbell_mask_update(doorbell, id, false);
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_mask);

void google_dpa_doorbell_unmask(struct google_dpa_doorbell *doorbell, u8 id)
{
	google_dpa_doorbell_mask_update(doorbell, id, true);
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_unmask);

static void google_dpa_doorbell_itr_update(struct google_dpa_doorbell *doorbell, u8 id)
{
	writel(BIT(id), doorbell->base + GOOGLE_DPA_DOORBELL_ITR_OFFSET);
}

void google_dpa_doorbell_ring_mcu(struct google_dpa_doorbell *doorbell, u8 id)
{
	pm_runtime_get_sync(doorbell->dev);
	google_dpa_doorbell_itr_update(doorbell, id);
	pm_runtime_put(doorbell->dev);
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_ring_mcu);

int google_dpa_doorbell_ring_mcu_atomic_safe(struct google_dpa_doorbell *doorbell, u8 id)
{
	struct device *dev = doorbell->dev;
	int ret;

	/*
	 * Callers ensure that the doorbell device is RPM active. Bump up the
	 * usage count if the doorbell is active. Returns an error if the
	 * doorbell device is not RPM active.
	 */
	ret = pm_runtime_get_if_in_use(dev);
	if (ret == 0) {
		dev_WARN(dev,
			 "DPA doorbell's IRQ-safe variant function is called when doorbell device is not RPM active.\n"
			 "Client drivers are responsible for calling pm_runtime_get() on doorbell device when calling IRQ safe variant.\n");
		return -EINVAL;
	} else if (ret < 0) {
		dev_err(dev, "Failed to increment the RPM usage count.\n");
		return ret;
	}
	google_dpa_doorbell_itr_update(doorbell, id);

	pm_runtime_put(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_ring_mcu_atomic_safe);

int google_dpa_doorbell_enable_doorbell(struct google_dpa_doorbell *doorbell, u8 id,
					doorbell_cb_t callback, void *data)
{
	struct dpa_doorbell_handler *handler;
	unsigned long flags;

	if (!callback || id >= BITS_PER_TYPE(u32))
		return -EINVAL;

	handler = &doorbell->handlers[id];

	google_dpa_doorbell_lock_process_context(&doorbell->lock, &flags);
	if (handler->callback) {
		google_dpa_doorbell_unlock_process_context(&doorbell->lock, &flags);
		return -EINVAL;
	}

	handler->callback = callback;
	handler->data = data;
	google_dpa_doorbell_unlock_process_context(&doorbell->lock, &flags);

	google_dpa_doorbell_mask_update(doorbell, id, true);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_enable_doorbell);

void google_dpa_doorbell_disable_doorbell(struct google_dpa_doorbell *doorbell, u8 id)
{
	struct dpa_doorbell_handler *handler;
	unsigned long flags;

	if (id >= BITS_PER_TYPE(u32)) {
		dev_err(doorbell->dev, "Invalid id in disable_doorbell call");
		return;
	}
	google_dpa_doorbell_mask_update(doorbell, id, false);
	handler = &doorbell->handlers[id];

	google_dpa_doorbell_lock_process_context(&doorbell->lock, &flags);
	handler->callback = NULL;
	handler->data = NULL;
	google_dpa_doorbell_unlock_process_context(&doorbell->lock, &flags);
}
EXPORT_SYMBOL_GPL(google_dpa_doorbell_disable_doorbell);

struct google_dpa_doorbell *google_dpa_get_doorbell_by_index(struct device *dev, int index)
{
	struct google_dpa_doorbell *doorbell;
	struct device_node *doorbell_np;
	struct device *doorbell_dev;
	struct device_link *link;

	doorbell_np = of_parse_phandle(dev->of_node, "doorbells", index);
	if (!doorbell_np) {
		dev_err(dev, "%pOF is missing doorbells[%d] entry\n", dev->of_node, index);
		return ERR_PTR(-EINVAL);
	}

	doorbell_dev =
		driver_find_device_by_of_node(&google_dpa_doorbell_driver.driver, doorbell_np);
	of_node_put(doorbell_np);
	if (!doorbell_dev)
		return ERR_PTR(-EPROBE_DEFER);

	link = device_link_add(dev, doorbell_dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	/*
	 * Both driver_find_device_by_of_node() and device_link_add() call
	 * get_device() on doorbell_dev. Call put_device() to decrement the
	 * refcount by one. DL_FLAG_AUTOREMOVE_CONSUMER ensures that the
	 * refcount is automatically decremented when the consumer device is
	 * removed.
	 */
	put_device(doorbell_dev);
	if (!link) {
		dev_err(dev, "Unable to add doorbell device link\n");
		return ERR_PTR(-EINVAL);
	}

	doorbell = dev_get_drvdata(doorbell_dev);
	if (!doorbell)
		return ERR_PTR(-EPROBE_DEFER);

	return doorbell;
}
EXPORT_SYMBOL_GPL(google_dpa_get_doorbell_by_index);

struct google_dpa_doorbell *google_dpa_get_doorbell(struct device *dev, const char *id)
{
	int index;

	index = of_property_match_string(dev->of_node, "doorbell_ids", id);
	if (index < 0) {
		dev_err(dev, "%pOF fails to match string %s in doorbell_ids property\n",
			dev->of_node, id);
		return ERR_PTR(index);
	}

	return google_dpa_get_doorbell_by_index(dev, index);
}
EXPORT_SYMBOL_GPL(google_dpa_get_doorbell);

struct device *google_dpa_get_doorbell_dev(struct google_dpa_doorbell *doorbell)
{
	return doorbell->dev;
}
EXPORT_SYMBOL_GPL(google_dpa_get_doorbell_dev);

/*
 * This function is expected called from DPA driver's remove().
 * See the comment in google_dpa_doorbell_driver_remove().
 */
void google_dpa_doorbell_deinit(struct google_dpa_doorbell *doorbell)
{
	pm_runtime_disable(doorbell->dev);
	google_dpa_detach_power_domain(doorbell->pd_vdev, doorbell->pd_link);
}

static irqreturn_t google_dpa_doorbell_irq(int irq, void *dev)
{
	struct google_dpa_doorbell *doorbell = dev_get_drvdata(dev);
	unsigned long isr;
	unsigned long bit;

	/*
	 * Why raw_spin_lock() variant here? Because this is already interrupt context and
	 * interrupts to local core are disabled already. So, no need to do extra by saving context
	 * again. Use efficient variant here.
	 */
	raw_spin_lock(&doorbell->lock);

	/*
	 * Why no pm_runtime_get() here? The Kernel's view of the DPA power domain may be that it is
	 * off, but the power-domain doesn't support power-on in atomic context. We must assume that
	 * the DPA FW is in charge of its own power here, and that it will guarantee power will
	 * remain on until we've handled the interrupt (either in hw through qactive signals or
	 * through a SW protocol).
	 */

	/* Ignore any masked interrupt */
	isr = readl(doorbell->base + GOOGLE_DPA_DOORBELL_ISR_OFFSET) & doorbell->imr;

	/*
	 * Clear doorbell first so that we don't miss any rings between the time we handle and
	 * clear it
	 */
	writel(isr, doorbell->base + GOOGLE_DPA_DOORBELL_ISR_OFFSET);

	raw_spin_unlock(&doorbell->lock);

	for_each_set_bit(bit, &isr, BITS_PER_TYPE(u32))
		doorbell->handlers[bit].callback(doorbell->handlers[bit].data);

	return IRQ_HANDLED;
}

static int google_dpa_doorbell_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_dpa_doorbell *doorbell;
	struct resource *res;
	int ret;

	doorbell = devm_kzalloc(dev, sizeof(*doorbell), GFP_KERNEL);
	if (!doorbell)
		return -ENOMEM;

	doorbell->dev = dev;

	/*
	 * Why *raw_* version of the spinlock? This guarantees that interrupts are disabled when
	 * the lock is taken in process context, preventing a deadlock when acquiring the lock in
	 * hard irq context.
	 */
	raw_spin_lock_init(&doorbell->lock);

	doorbell->irq = platform_get_irq(pdev, 0);
	if (doorbell->irq < 0) {
		dev_err(dev, "No doorbell interrupt for device\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Failed to find reg entry.");
		return -EINVAL;
	}
	doorbell->base = devm_ioremap(dev, res->start, res->end - res->start);
	if (!doorbell->base) {
		dev_err(dev, "Failed to ioremap mba client region.");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, doorbell->irq, google_dpa_doorbell_irq,
			       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND | IRQF_NO_THREAD, dev_name(dev),
			       dev);
	if (ret != 0) {
		dev_err(dev, "failed to register doorbell interrupt handler: %d\n", ret);
		return ret;
	}

	ret = google_dpa_attach_power_domain(dev, &doorbell->pd_vdev, &doorbell->pd_link);
	if (ret) {
		dev_err(dev, "failed to attach power domain: %d\n", ret);
		return ret;
	}

	ret = devm_pm_runtime_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable runtime PM\n");
		goto detach_power_domain;
	}

	platform_set_drvdata(pdev, doorbell);
	return ret;

detach_power_domain:
	google_dpa_detach_power_domain(doorbell->pd_vdev, doorbell->pd_link);

	return ret;
}

static void google_dpa_doorbell_driver_remove(struct platform_device *pdev)
{
	/*
	 * skip clearing doorbell IMR because DPA driver tear down DPA block
	 * before dpa-doorbell's remove(). Due to the HW limitation, DPA block
	 * cannot be turned off before booting DPA FW (i.e. the allowed power
	 * transition is OFF->WAIT->ON->OFF). If the DPA block is turned on in
	 * this function, we cannot turn it off because DPA block gets stuck in
	 * WAIT state and DPA driver is already removed.
	 *
	 * DPA driver is responsible for calling doorbell_deinit() to clean up
	 * power-related resources. Linux kernel calls pm_runtime_get_sync()
	 * before calling doorbell's remove(). Doorbell is removed after DPA
	 * driver's remove() and doorbell driver does not have ability to tear
	 * down DPA block when DPA block gets stuck in WAIT state. The
	 * invocation of doorbell_deinit() from DPA driver's remove() prevents
	 * Linux kernel from turning on DPA block at doorbell's remove().
	 */
}

static const struct of_device_id google_dpa_doorbell_of_match_table[] = {
	{ .compatible = "google,dpa-doorbell" },
	{}
};
MODULE_DEVICE_TABLE(of, google_dpa_doorbell_of_match_table);

static DEFINE_RUNTIME_DEV_PM_OPS(google_dpa_doorbell_dev_pm_ops, NULL, NULL, NULL);

struct platform_driver google_dpa_doorbell_driver = {
	.probe = google_dpa_doorbell_driver_probe,
	.remove = google_dpa_doorbell_driver_remove,
	.driver = {
		.name = "google-dpa-doorbell",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_dpa_doorbell_of_match_table),
		.pm = &google_dpa_doorbell_dev_pm_ops,
	},
};

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google DPA doorbell driver");
MODULE_LICENSE("GPL");
