#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/if_inet6.h>
#include <net/route.h>
#include <net/addrconf.h>
#include <linux/cpumask.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <soc/google/google_dpa.h>

#include "noa_test_harness.h"
#include "harness_buffer_management.h"
#include "harness_doorbell.h"
#include "harness_memory_mapper.h"
#include "harness_ring.h"
#include "harness_wlan_device.h"
#include "harness_modem_device.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/ring.h"

#define DRIVER_NAME "noa-harness-driver"

static struct workqueue_struct *noa_harness_wq;

struct noa_harness;

struct noa_harness_ring_params {
	u8 id;
	u32 size;
	u32 item_len;
};

struct noa_harness_device_params {
	struct noa_harness_ring_params tx_ring;
	int (*format_tx_data)(void *data, const struct noa_harness_pkt *pkt);
	struct noa_harness_ring_params rx_ring;
	int (*parse_rx_pkt)(struct noa_harness_pkt *pkt, const void *data);
	enum noa_harness_doorbell_id doorbell_id;
	const char *device_name;
	void (*device_isr)(struct work_struct *work);
	void (*device_setup)(struct net_device *dev);
	bool is_support_nep_buffer_pool;
	u8 nep_buffer_pool_id;
	u32 nep_buffer_pool_size;
	u32 nep_buffer_pool_tkid_offset;
};

#define DEFAULT_NOA_VWLAN_RING_SIZE (512U)
#define DEFAULT_NOA_VMODEM_RING_SIZE (512U)

static const char *const noa_harness_device_mode_str[] = {
	[NOA_HARNESS_DEVICE_MODE_FEEDTHROUGH] = "feedthrough",
	[NOA_HARNESS_DEVICE_MODE_NETENGINE] = "netengine",
};

static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct noa_harness_vdev *vdev = netdev_priv(to_net_dev(dev));

	return sysfs_emit(buf, "%s\n", noa_harness_device_mode_str[vdev->mode]);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			  size_t count)
{
	struct noa_harness_vdev *vdev = netdev_priv(to_net_dev(dev));
	int i;

	for (i = 0; i < ARRAY_SIZE(noa_harness_device_mode_str); i++) {
		if (sysfs_streq(buf, noa_harness_device_mode_str[i])) {
			vdev->mode = i;
			dev_info(dev, "Switch %s to mode: %s\n", vdev->dev->name,
				 noa_harness_device_mode_str[i]);
			return count;
		}
	}

	return -EINVAL;
}

static DEVICE_ATTR_RW(mode);

static struct attribute *noa_vdev_attrs[] = {
	&dev_attr_mode.attr,
	NULL,
};

static const struct attribute_group noa_vdev_group = {
	.attrs = noa_vdev_attrs,
};

static void remove_noa_harness_netdev(struct noa_harness *tm, enum noa_virtual_device_type type)
{
	struct noa_harness_vdev *vdev;
	struct net_device *dev;

	if (type >= NOA_VIRTUAL_DEVICE_MAX || !tm->noa_harness_netdevs[type])
		return;

	dev = tm->noa_harness_netdevs[type];
	vdev = netdev_priv(dev);
	noa_harness_doorbell_unregister_isr_task(&vdev->isr_task);
	cancel_work_sync(&vdev->work);
	noa_harness_unregister_ring(tm, &vdev->tx_ring);
	noa_harness_unregister_ring(tm, &vdev->rx_ring);
	if (vdev->nep_buffer_pool.size)
		noa_harness_unregister_nep_buffer_pool(tm, vdev);

	unregister_netdev(dev);
	free_netdev(dev);
	tm->noa_harness_netdevs[type] = NULL;
}

static int create_noa_harness_netdev(struct noa_harness *tm, enum noa_virtual_device_type type,
				     const struct noa_harness_device_params *params)
{
	int ret;
	struct net_device *dev;
	unsigned int num_queues = num_online_cpus();
	struct noa_harness_vdev *vdev = NULL;

	if (type >= NOA_VIRTUAL_DEVICE_MAX)
		return -EINVAL;

	dev = alloc_netdev_mqs(sizeof(struct noa_harness_vdev), params->device_name,
			       NET_NAME_UNKNOWN, params->device_setup, num_queues, num_queues);
	if (!dev) {
		ret = -ENOMEM;
		goto out;
	}

	dev->sysfs_groups[0] = &noa_vdev_group;

	vdev = netdev_priv(dev);
	vdev->dev = dev;
	vdev->tm = tm;
	vdev->mode = NOA_HARNESS_DEVICE_MODE_FEEDTHROUGH;
	vdev->format_tx_data = params->format_tx_data;
	vdev->parse_rx_pkt = params->parse_rx_pkt;
	INIT_WORK(&vdev->work, params->device_isr);
	INIT_WORK(&vdev->refill_work, noa_harness_refill_nep_buffer_task);
	vdev->isr_task.vdev = vdev;
	vdev->doorbell = noa_harness_doorbell_get(tm, params->doorbell_id);
	if (!vdev->doorbell) {
		dev_err(tm->dev, "Failed to get doorbell for %s\n", dev->name);
		ret = -EINVAL;
		goto release_netdev;
	}

	ret = noa_harness_register_ring(tm, vdev, &vdev->tx_ring, NOA_RING_TYPE_PRODUCER,
					kNoaRingNepInput, params->tx_ring.id, dev->name,
					params->tx_ring.item_len, params->tx_ring.size);
	if (ret) {
		dev_err(tm->dev, "Failed to register tx ring for %s\n", dev->name);
		goto release_netdev;
	}

	ret = noa_harness_register_ring(tm, vdev, &vdev->rx_ring, NOA_RING_TYPE_CONSUMER,
					kNoaRingNepOutput, params->rx_ring.id, dev->name,
					params->rx_ring.item_len, params->rx_ring.size);
	if (ret) {
		dev_err(tm->dev, "Failed to register rx ring for %s\n", dev->name);
		goto unregister_tx_ring;
	}

	if (params->is_support_nep_buffer_pool) {
		ret = noa_harness_register_nep_buffer_pool(tm, vdev, params->nep_buffer_pool_id,
							   params->device_name,
							   params->nep_buffer_pool_tkid_offset,
							   params->nep_buffer_pool_size);
		if (ret) {
			dev_err(tm->dev, "Failed to register nep buffer pool for %s\n", dev->name);
			goto unregister_rx_ring;
		}
	}
	vdev->support_nep_buffer_pool = params->is_support_nep_buffer_pool;

	ret = noa_harness_doorbell_register_isr_task(&tm->doorbells[params->doorbell_id],
						     &vdev->isr_task);
	if (ret) {
		dev_err(tm->dev, "Failed to register isr task for %s\n", dev->name);
		goto unregister_nep_buffer_pool;
	}

	ret = register_netdev(dev);
	if (ret) {
		dev_err(tm->dev, "Failed to register %s\n", dev->name);
		goto unregister_doorbell;
	}

	tm->noa_harness_netdevs[type] = dev;
	dev_info(tm->dev, "Interface %s created with %u queues\n", dev->name, num_online_cpus());
	return 0;

unregister_doorbell:
	noa_harness_doorbell_unregister_isr_task(&vdev->isr_task);
unregister_nep_buffer_pool:
	noa_harness_unregister_nep_buffer_pool(tm, vdev);
unregister_rx_ring:
	noa_harness_unregister_ring(tm, &vdev->rx_ring);
unregister_tx_ring:
	noa_harness_unregister_ring(tm, &vdev->tx_ring);
release_netdev:
	free_netdev(dev);
out:
	return ret;
}

static const struct noa_harness_device_params device_params[] = {
	[NOA_VIRTUAL_WLAN_HOST] = {
		.tx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
					kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData),
			.size = DEFAULT_NOA_VWLAN_RING_SIZE,
			.item_len = NOA_DESC_WLAN_TX_BRCM_BYTE,
		},
		.format_tx_data = noa_harness_wlan_format_tx_data,
		.rx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
					kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData),
			.size = DEFAULT_NOA_VWLAN_RING_SIZE,
			.item_len = NOA_DESC_MAX_BYTE,
		},
		.parse_rx_pkt = noa_harness_wlan_parse_rx_pkt,
		.doorbell_id = NOA_HARNESS_DOORBELL_WLAN,
		.device_name = "noa_vwlan_host",
		.device_isr = noa_harness_wlan_isr_task,
		.device_setup = noa_harness_wlan_host_netdev_setup,
		.is_support_nep_buffer_pool = false,
		.nep_buffer_pool_id = 0,
		.nep_buffer_pool_size = 0,
		.nep_buffer_pool_tkid_offset = 0,
	},
	[NOA_VIRTUAL_WLAN_NCP] = {
		.tx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
					kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData),
			.size = DEFAULT_NOA_VWLAN_RING_SIZE,
			.item_len = NOA_DESC_WLAN_RX_BRCM_BYTE,
		},
		.format_tx_data = noa_harness_wlan_format_tx_data,
		.rx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
					kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData),
			.size = DEFAULT_NOA_VWLAN_RING_SIZE,
			.item_len = NOA_DESC_MAX_BYTE,
		},
		.parse_rx_pkt = noa_harness_wlan_parse_rx_pkt,
		.doorbell_id = NOA_HARNESS_DOORBELL_WLAN,
		.device_name = "noa_vwlan_ncp",
		.device_isr = noa_harness_wlan_isr_task,
		.device_setup = noa_harness_wlan_ncp_netdev_setup,
		.is_support_nep_buffer_pool = true,
		.nep_buffer_pool_id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool),
		.nep_buffer_pool_size = MAX_NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_SIZE,
		.nep_buffer_pool_tkid_offset = NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_OFFSET,
	},
	[NOA_VIRTUAL_MODEM_HOST] = {
		.tx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
					kNoaNetworkFlowHostToDevice, kNoaModemRingTxData),
			.size = DEFAULT_NOA_VMODEM_RING_SIZE,
			.item_len = NOA_DESC_MODEM_TX_MTK_BYTE,
		},
		.format_tx_data = noa_harness_modem_host_format_tx_data,
		.rx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
					kNoaNetworkFlowDeviceToHost, kNoaModemRingRxq0),
			.size = DEFAULT_NOA_VMODEM_RING_SIZE,
			.item_len = NOA_HARNESS_MODEM_DESC_BYTE,
		},
		.parse_rx_pkt = noa_harness_modem_host_parse_rx_pkt,
		.doorbell_id = NOA_HARNESS_DOORBELL_MODEM,
		.device_name = "noa_vmodem_host",
		.device_isr = noa_harness_modem_isr_task,
		.device_setup = noa_harness_modem_netdev_setup,
		.is_support_nep_buffer_pool = false,
		.nep_buffer_pool_id = 0,
		.nep_buffer_pool_size = 0,
		.nep_buffer_pool_tkid_offset = 0,
	},
	[NOA_VIRTUAL_MODEM_NCP] = {
		.tx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
					kNoaNetworkFlowDeviceToHost, kNoaModemRingRxq0),
			.size = DEFAULT_NOA_VMODEM_RING_SIZE,
			.item_len = NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE,
		},
		.format_tx_data = noa_harness_modem_ncp_format_tx_data,
		.rx_ring = {
			.id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
					kNoaNetworkFlowHostToDevice, kNoaModemRingTxData),
			.size = DEFAULT_NOA_VMODEM_RING_SIZE,
			.item_len = NOA_DESC_MODEM_TX_MTK_BYTE,
		},
		.parse_rx_pkt = noa_harness_modem_ncp_parse_rx_pkt,
		.doorbell_id = NOA_HARNESS_DOORBELL_MODEM,
		.device_name = "noa_vmodem_ncp",
		.device_isr = noa_harness_modem_isr_task,
		.device_setup = noa_harness_modem_netdev_setup,
		.is_support_nep_buffer_pool = true,
		.nep_buffer_pool_id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool),
		.nep_buffer_pool_size = MAX_NOA_HARNESS_MODEM_NEP_BUFFER_POOL_TKID_SIZE,
		.nep_buffer_pool_tkid_offset = NOA_HARNESS_MODEM_NEP_BUFFER_POOL_TKID_OFFSET,
	},
};

struct noa_harness_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct noa_harness *, char *);
	ssize_t (*store)(struct noa_harness *, const char *, size_t count);
};

#define to_noa_harness(x) container_of(x, struct noa_harness, kobj)

static int start_noa_harness(struct noa_harness *tm)
{
	int i;
	int ret;

	ret = create_noa_harness_netdev(tm, NOA_VIRTUAL_WLAN_HOST,
					&device_params[NOA_VIRTUAL_WLAN_HOST]);
	if (ret) {
		dev_err(tm->dev, "Failed to create wlan host netdev\n");
		goto out;
	}

	ret = create_noa_harness_netdev(tm, NOA_VIRTUAL_WLAN_NCP,
					&device_params[NOA_VIRTUAL_WLAN_NCP]);
	if (ret) {
		dev_err(tm->dev, "Failed to create wlan ncp netdev\n");
		goto out;
	}

	ret = create_noa_harness_netdev(tm, NOA_VIRTUAL_MODEM_HOST,
					&device_params[NOA_VIRTUAL_MODEM_HOST]);
	if (ret) {
		dev_err(tm->dev, "Failed to create modem host netdev\n");
		goto out;
	}

	ret = create_noa_harness_netdev(tm, NOA_VIRTUAL_MODEM_NCP,
					&device_params[NOA_VIRTUAL_MODEM_NCP]);
	if (ret) {
		dev_err(tm->dev, "Failed to create modem ncp netdev\n");
		goto out;
	}

	ret = noa_harness_doorbell_enable(tm);
	if (ret) {
		dev_err(tm->dev, "Failed to enable doorbell\n");
		goto out;
	}
	ret = 0;
	dev_info(tm->dev, "NOA Harness started\n");

out:
	if (ret) {
		for (i = 0; i < NOA_VIRTUAL_DEVICE_MAX; i++)
			remove_noa_harness_netdev(tm, i);
		dev_err(tm->dev, "Failed to start NOA Harness");
	}
	return ret;
}

static void stop_noa_harness(struct noa_harness *tm)
{
	int i;

	noa_harness_doorbell_disable(tm);
	for (i = 0; i < NOA_VIRTUAL_DEVICE_MAX; i++)
		remove_noa_harness_netdev(tm, i);
	dev_info(tm->dev, "NOA Harness stopped\n");
}

static ssize_t harness_control_write(struct noa_harness *tm, const char *buf, size_t count)
{
	int ret;

	if (sysfs_streq(buf, "start")) {
		ret = start_noa_harness(tm);
		if (ret)
			return ret;
	} else if (sysfs_streq(buf, "stop")) {
		stop_noa_harness(tm);
	} else {
		dev_warn(tm->dev, "Invalid value. Use 'start' or 'stop'.\n");
		return -EINVAL;
	}

	return count;
}

static struct noa_harness_kobj_attr attr_harness_control =
	__ATTR(harness_control, 0200, NULL, harness_control_write);

static struct attribute *noa_harness_attrs[] = {
	&attr_harness_control.attr,
	NULL,
};
ATTRIBUTE_GROUPS(noa_harness);

static ssize_t noa_harness_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct noa_harness *tm = to_noa_harness(kobj);
	struct noa_harness_kobj_attr *noa_harness_attr =
		container_of(attr, struct noa_harness_kobj_attr, attr);

	if (noa_harness_attr->show)
		return noa_harness_attr->show(tm, buf);
	return -EIO;
}

static ssize_t noa_harness_sysfs_store(struct kobject *kobj, struct attribute *attr,
				       const char *buf, size_t count)
{
	struct noa_harness *tm = to_noa_harness(kobj);
	struct noa_harness_kobj_attr *noa_harness_attr =
		container_of(attr, struct noa_harness_kobj_attr, attr);

	if (noa_harness_attr->store)
		return noa_harness_attr->store(tm, buf, count);
	return -EIO;
}

static const struct sysfs_ops noa_harness_sysfs_ops = {
	.show = noa_harness_sysfs_show,
	.store = noa_harness_sysfs_store,
};

static struct kobj_type noa_harness_ktype = {
	.sysfs_ops = &noa_harness_sysfs_ops,
	.default_groups = noa_harness_groups,
};

static int noa_harness_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct noa_harness *tm;

#if !IS_ENABLED(CONFIG_NOA_TEST_HARNESS)
	return 0;
#endif

	tm = devm_kzalloc(dev, sizeof(*tm), GFP_KERNEL);
	if (!tm)
		return -ENOMEM;

	platform_set_drvdata(pdev, tm);
	tm->dev = dev;

	tm->wq = noa_harness_wq;
	tm->dpa = google_dpa_get(dev);
	if (IS_ERR(tm->dpa)) {
		ret = PTR_ERR(tm->dpa);
		tm->dpa = NULL;
		dev_err_probe(dev, ret, "Failed to get dpa structure.");
		return ret;
	}

	ret = noa_harness_buf_mgr_init(&tm->buf_mgr, dev, tm->dpa);
	if (ret) {
		dev_err(dev, "Failed to init buffer manager, err: %d\n", ret);
		goto err_put_dpa;
	}

	ret = noa_harness_doorbell_init(tm);
	if (ret) {
		goto err_deinit_buf_mgr;
	}

	ret = kobject_init_and_add(&tm->kobj, &noa_harness_ktype, &dev->kobj, "noa_harness");
	if (ret) {
		dev_err(dev, "Failed to create sysfs group, err: %d\n", ret);
		kobject_put(&tm->kobj);
		goto err_deinit_doorbell;
	}

	return 0;

err_deinit_doorbell:
	noa_harness_doorbell_disable(tm);
err_deinit_buf_mgr:
	noa_harness_buf_mgr_deinit(&tm->buf_mgr);
err_put_dpa:
	google_dpa_put(tm->dpa);
	return ret;
}

static void noa_harness_remove(struct platform_device *pdev)
{
	struct noa_harness *tm = platform_get_drvdata(pdev);
	int i;

	kobject_put(&tm->kobj);

	noa_harness_doorbell_disable(tm);

	for (i = 0; i < NOA_VIRTUAL_DEVICE_MAX; i++)
		remove_noa_harness_netdev(tm, i);

	noa_harness_buf_mgr_deinit(&tm->buf_mgr);

	if (tm->dpa)
		google_dpa_put(tm->dpa);

	dev_info(tm->dev, "Module unloaded\n");
}

static const struct of_device_id noa_harness_of_match[] = {
	{
		.compatible = "google,dpa-test-harness",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, noa_harness_of_match);

static struct platform_driver noa_harness_driver = {
	.probe = noa_harness_probe,
	.remove = noa_harness_remove,
	.driver = {
		.name = "dpa_test_harness",
		.owner = THIS_MODULE,
		.of_match_table = noa_harness_of_match,
	},
};

static int __init noa_harness_init(void)
{
	int ret;

	noa_harness_wq = create_singlethread_workqueue(DRIVER_NAME);
	if (!noa_harness_wq)
		return -ENOMEM;

	ret = platform_driver_register(&noa_harness_driver);
	if (ret)
		destroy_workqueue(noa_harness_wq);
	return ret;
}

static void __exit noa_harness_exit(void)
{
	platform_driver_unregister(&noa_harness_driver);
	destroy_workqueue(noa_harness_wq);
}

module_init(noa_harness_init);
module_exit(noa_harness_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kai-Wen Hu");
MODULE_DESCRIPTION(
	"A simple virtual network driver creating a pair of Ethernet interfaces for NOA testing.");
