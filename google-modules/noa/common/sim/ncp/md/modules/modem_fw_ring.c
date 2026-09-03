#include "modem_fw_ring.h"

const char *NoaModemRxRingName[kNoaModemRingRxDataEnd] = {
	// 4 NOA Modem FW input Rings
	"md_fw_input_default",
	"md_fw_input_0",
	"md_fw_input_1",
	"md_fw_input_2",
};

static void modem_fw_trigger_doorbell(struct noa_ring_wrapper *ring)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)ring->owner;
	struct noa_md_fw_rx *rx = md_fw->rx;

	/* Trigger NOA DMA scheduler to handle input ring */
	if (noa_ring_pos_is_moved(ring)) {
		writel(1, (void *)rx->doorbell_reg_addr);
		noa_sim_trig_rx();
	}
}

static void *modem_fw_ring_dma_alloc(struct noa_md_fw *md_fw, struct noa_md_fw_ring *fw_ring, size_t size)
{
	fw_ring->desc_base = dma_alloc_coherent(
		md_fw->dev,
		size,
		&fw_ring->dma_addr,
		GFP_KERNEL);

	return fw_ring->desc_base;
}

static void modem_fw_ring_dma_free(struct noa_md_fw *md_fw, struct noa_md_fw_ring *fw_ring, size_t size)
{
	dma_free_coherent(md_fw->dev, size, fw_ring->desc_base, fw_ring->dma_addr);
}

static int modem_fw_ring_init(struct noa_md_fw *md_fw, struct noa_md_fw_ring *fw_ring,
                              const int wrapper_type, const struct noa_ring_ops *const ops,
                              uint32_t size, uint32_t item_len, const int flow_of_ring,
                              const int type_of_ring, const int direction_of_ring,
                              const char *const name, void *buff_vbase)
{
	struct noa_ring_regs regs = {};
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.base = 0,
		.item_len = item_len,
		.size = size,
		.dpa_base = 0,
	};

	if (NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceModem, flow_of_ring,
                                      type_of_ring, direction_of_ring)) {
		NCP_MD_ERROR("Failed to get modem fw ring regs with type %d", type_of_ring);
		return -ENODEV;
	}

	if (noa_ring_regs_wrapper_init(&fw_ring->ring, wrapper_type, ops, &regs, md_fw, name,
				       0) < 0) {
		NCP_MD_ERROR("Failed to init noa regs wrapper");
		return -ENODEV;
	}

	/*ring sw initial*/
	info.base = (char *)buff_vbase;
	info.dpa_base = info.base;
	noa_ring_info_setup(&fw_ring->ring, &info);
	spin_lock_init(&fw_ring->lock);
	NCP_MD_INFO("end");
	return 0;
}

static int modem_fw_ring_begin_processing(struct noa_ring_wrapper *ring)
{
	return noa_ring_begin_processing(ring);
}

static int modem_fw_ring_complete_processing(struct noa_ring_wrapper *ring)
{
	noa_ring_complete_processing(ring);
	return 0;
}

static int modem_fw_ring_activate(struct noa_ring_wrapper *ring, int ring_type)
{
	noa_ring_activate(ring);
	if (noa_ring_is_consumer(ring)) {
		nep_ring_service_request_send(
			NEP_CMD_RING_ACTIVE,
			true,
			NoaRingPathIdConvert(
				kNoaNetworkInterfaceModem,
				kNoaNetworkFlowHostToDevice,
				ring_type),
			kNoaRingNepOutput);
	} else {
		nep_ring_service_request_send(
			NEP_CMD_RING_ACTIVE,
			true,
			NoaRingPathIdConvert(
				kNoaNetworkInterfaceModem,
				kNoaNetworkFlowDeviceToHost,
				ring_type),
			kNoaRingNepInput);
	}
	return 0;
}

static int modem_fw_ring_deactivate(struct noa_ring_wrapper *ring, int ring_type)
{
	if (noa_ring_is_consumer(ring)) {
		nep_ring_service_request_send(
			NEP_CMD_RING_ACTIVE,
			false,
			NoaRingPathIdConvert(
				kNoaNetworkInterfaceModem,
				kNoaNetworkFlowHostToDevice,
				ring_type),
			kNoaRingNepOutput);
	} else {
		nep_ring_service_request_send(
			NEP_CMD_RING_ACTIVE,
			false,
			NoaRingPathIdConvert(
				kNoaNetworkInterfaceModem,
				kNoaNetworkFlowDeviceToHost,
				ring_type),
			kNoaRingNepInput);
	}
	noa_ring_deactivate(ring);
	return 0;
}

static void modem_fw_ring_exit(struct noa_md_fw *md_fw, struct noa_md_fw_ring *fw_ring, int ring_type)
{
	struct noa_ring_wrapper *ring = &fw_ring->ring;
	NCP_MD_INFO("enter");
	modem_fw_ring_deactivate(ring, ring_type);
	spin_lock(&fw_ring->lock);

	if (fw_ring->desc_base && fw_ring->dma_addr) {
		modem_fw_ring_dma_free(md_fw, fw_ring, ring->basic.size * ring->basic.item_len);
	}
	fw_ring->desc_base = NULL;
	fw_ring->dma_addr = 0;

	(void)md_fw;
	noa_ring_info_clean(ring);
	spin_unlock(&fw_ring->lock);
	NCP_MD_INFO("end");
}

static void modem_fw_tx_ring_exit(struct noa_md_fw *md_fw, int ring_type)
{
	NCP_MD_TX_INFO("enter");
	struct noa_md_fw_tx *tx = md_fw->tx;
	if (!tx) {
		NCP_MD_TX_ERROR("tx is null");
		return;
	}
        modem_fw_ring_exit(md_fw, &tx->tx_ring, ring_type);

	tx->ints_reg_addr = 0;

	NCP_MD_TX_INFO("exit");
}

static void modem_fw_rx_ring_exit(struct noa_md_fw *md_fw, int ring_type)
{
	NCP_MD_RX_INFO("enter");
        struct noa_md_fw_rx *rx = md_fw->rx;
	if (!rx) {
		NCP_MD_RX_ERROR("rx is null");
		return;
	}
	modem_fw_ring_exit(md_fw, &rx->rx_ring[ring_type], ring_type);

	rx->doorbell_reg_addr = 0;
	rx->rx_ring[ring_type].num_desc = 0;
	rx->rx_ring[ring_type].virtual_write_idx = 0;

	NCP_MD_RX_INFO("exit");
}

static const char *modem_fw_ring_get_name(struct noa_ring_wrapper *ring)
{
	return ring->name;
}

static int modem_fw_rx_ring_write(struct noa_ring_wrapper *ring,
						   void *data, size_t len)
{
	return noa_ring_write(ring, data, len);
}

static ssize_t modem_fw_input_desc_write(void *output_buf, size_t buf_len, const void *input_data,
						  size_t data_len)
{
	if (buf_len < data_len)
		return -ENOMEM;
	memcpy(output_buf, input_data, data_len);

	return data_len;
}

const static struct noa_ring_ops rx_ring_ops = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = NULL,
	.write_payload = modem_fw_input_desc_write,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = modem_fw_trigger_doorbell,
};

static int modem_fw_rx_ring_init(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_md_fw_ring *rx_ring;
	struct noa_ring_wrapper *ring;
	int ret;
	void *buf = NULL;

	NCP_MD_RX_INFO("enter");

	for (int ring_type = 0; ring_type < kNoaModemRingRxDataEnd; ring_type++) {
		rx_ring = &rx->rx_ring[ring_type];
		ring = &rx_ring->ring;

		buf = modem_fw_ring_dma_alloc(
			md_fw,
			rx_ring,
			NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE * NOA_FW_RING_SIZE);

		if (!buf) {
			NCP_MD_RX_ERROR("no memory");
			return -ENOMEM;
		}

		ret = modem_fw_ring_init(
				md_fw, rx_ring, NOA_RING_TYPE_PRODUCER, &rx_ring_ops,
				NOA_FW_RING_SIZE, NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE,
				kNoaNetworkFlowDeviceToHost, ring_type,
				kNoaRingNepInput, NoaModemRxRingName[ring_type], buf);

		if (ret) {
			NCP_MD_RX_ERROR("Failed to init modem fw input ring, ret=[%d]", ret);
			modem_fw_rx_ring_exit(md_fw, ring_type);
			return ret;
		}

		ret = modem_fw_ring_activate(ring, ring_type);

		rx_ring->num_desc = NOA_FW_RING_SIZE;
		rx_ring->virtual_write_idx = 0;
		struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);
		rx->doorbell_reg_addr = (unsigned long)&port->doorbell;
	}
	NCP_MD_RX_INFO("exit");
	return 0;
}

static int modem_fw_tx_ring_read(struct noa_ring_wrapper *ring, void *data, size_t len)
{
	return noa_ring_read(ring, data, len);
}

static bool modem_fw_tx_ring_is_empty(struct noa_ring_wrapper *ring)
{
	return noa_ring_is_empty(ring);
}

static int modem_fw_tx_ring_tail_inc(struct noa_ring_wrapper *ring)
{
	noa_ring_tail_inc(ring);
	return 0;
}

const static struct noa_ring_ops tx_ring_ops = {
	.payload_len = NULL,
	.read_payload = noa_generic_read_raw_pointer,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

static int modem_fw_tx_ring_init(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_md_fw_ring *tx_ring = &tx->tx_ring;
        struct noa_ring_wrapper *ring = &tx_ring->ring;
	int ret;
	void *buf = NULL;

	NCP_MD_TX_INFO("enter");

	buf = modem_fw_ring_dma_alloc(md_fw, tx_ring, NOA_DESC_MODEM_TX_MTK_BYTE *
								     NOA_FW_RING_SIZE);

	if (!buf) {
		NCP_MD_TX_ERROR("no memory");
		return -ENOMEM;
	}

	ret = modem_fw_ring_init(
		md_fw, tx_ring, NOA_RING_TYPE_CONSUMER, &tx_ring_ops,
		NOA_FW_RING_SIZE, NOA_DESC_MODEM_TX_MTK_BYTE,
		kNoaNetworkFlowHostToDevice, kNoaModemRingTxData,
		kNoaRingNepOutput, "md_fw_output", buf);

	if (ret) {
		NCP_MD_TX_ERROR("Failed to init modem fw output ring, ret=[%d]", ret);
		modem_fw_tx_ring_exit(md_fw, kNoaModemRingTxData);
		return ret;
	}

	ret = modem_fw_ring_activate(ring, kNoaModemRingTxData);

	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);
	tx->ints_reg_addr = (unsigned long)&port->ints;

	NCP_MD_TX_INFO("exit");
	return 0;
}

const struct modem_fw_ring_ops *modem_fw_ring_get_tx_ops(void)
{
	SEC_FAST_CONST static struct modem_fw_ring_ops modem_fw_ring_tx_ops = {
		.begin_processing = modem_fw_ring_begin_processing,
		.complete_processing = modem_fw_ring_complete_processing,
		.read = modem_fw_tx_ring_read,
		.write = NULL,
		.tail_inc = modem_fw_tx_ring_tail_inc,
		.is_empty = modem_fw_tx_ring_is_empty,
		.init = modem_fw_tx_ring_init,
		.exit = modem_fw_tx_ring_exit,
		.activate = modem_fw_ring_activate,
		.get_name = modem_fw_ring_get_name,
	};

	return &modem_fw_ring_tx_ops;
}

const struct modem_fw_ring_ops *modem_fw_ring_get_rx_ops(void)
{
	SEC_FAST_CONST static struct modem_fw_ring_ops modem_fw_ring_rx_ops = {
		.begin_processing = modem_fw_ring_begin_processing,
		.complete_processing = modem_fw_ring_complete_processing,
		.read = NULL,
		.write = modem_fw_rx_ring_write,
		.tail_inc = NULL,
		.is_empty = NULL,
		.init = modem_fw_rx_ring_init,
		.exit = modem_fw_rx_ring_exit,
		.activate = modem_fw_ring_activate,
		.get_name = modem_fw_ring_get_name,
	};

	return &modem_fw_ring_rx_ops;
}

static irqreturn_t modem_fw_nep_interrupt_handler(int32_t id, void *data)
{
	NCP_MD_TX_INFO("enter");
	(void)id;
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_tx *tx = md_fw->tx;

	// clear ints first, then schedule a bottom half task
	writel(0, (void *) tx->ints_reg_addr);

	tasklet_schedule(&tx->md_tx_task);
	NCP_MD_TX_INFO("exit");

	return IRQ_HANDLED;
}

static inline int32_t modem_fw_get_nep_irq(void)
{
	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);
	return port->irq;
}

int modem_fw_register_nep_interrupt(struct noa_md_fw *md_fw)
{
        int ret = 0;

        int32_t irq_id = modem_fw_get_nep_irq();
	/* register noa interrupt */
	ret = noa_interrupt_register(irq_id, modem_fw_nep_interrupt_handler, md_fw);

	return ret;
}

void modem_fw_free_nep_interrupt(struct noa_md_fw *md_fw)
{
	noa_interrupt_unregister(modem_fw_get_nep_irq(), md_fw);
}
