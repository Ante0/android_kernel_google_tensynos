// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory-mapped interface driver for DW SPI Core
 *
 * Copyright (c) 2010, Octasic semiconductor.
 */

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/devinfo.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/acpi.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <aoss-ssr-notifier/aoss_ssr_notifier.h>
#include <linux/seq_file.h>
#include <linux/spi/spi.h>

#include "drivers/spi/spi-dw.h"

#define CREATE_TRACE_POINTS
#include "spi-dw-mmio-trace.h"

#define DRIVER_NAME "dw_spi_mmio"

/*
 * TODO(b/281621411): Start with conservative value
 * and update for observability.
 */
#define RPM_AUTOSUSPEND_DELAY_MS 200
#define SPI_DMA_16B_ALIGN_ONLY  BIT(0)
#define GOOGLE_SPI_GPIO_CS_WITH_CPOL_HIGH  BIT(1)
#define GOOGLE_SPI_OVERWRITE_DMA_TX_BURST  BIT(2)
#define MAX_INPUT_SERIAL_CLOCK_HZ 200000000
#define DMA_HISTOGRAM_BUCKETS 11
#define AOSS_SSR_ONLINE_TIMEOUT_MS 20000
#define DW_SPI_RX_BUSY 0
#define DW_SPI_TX_BUSY 1
#define GOOG_SPI_DMA_TIMEOUT_MAX_MS 5000
#define SPI_MAX_LOOP_CNT_LIMIT 1000000
#define SPI_MAX_LOOP_CNT_FACTOR 3

/* Transaction lengths (in bytes) <= limit for each bucket */
static const unsigned int histogram_size_limits[DMA_HISTOGRAM_BUCKETS] = {
	4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

struct dma_profiling_stats {
	u32 dma_transactions;
	u32 pio_transactions;
	u32 dma_tx_counts_hist[DMA_HISTOGRAM_BUCKETS];
	u32 pio_tx_counts_hist[DMA_HISTOGRAM_BUCKETS];
};

struct dw_spi_mmio {
	struct dw_spi  dws;
	struct clk     *clk;
	struct clk     *pclk;
	void           *priv;
	struct reset_control *rstc;
	struct dw_spi_dma_ops dma_ops_modified;
	int quirks;
	struct pinctrl *pinctrl;
	struct pinctrl_state *cli_state;
	u32 cs_delay;
	u32 max_freq_at_probe;
	bool aoss_ssr_started;
	struct dentry *debugfs;
	struct notifier_block aoss_ssr_nb;
	struct delayed_work aoss_ssr_work;
	struct device *dev;
	void __iomem *lcm_virt_addr;
	struct dma_profiling_stats dma_stats;
	atomic_t alive;
};

#define MSCC_CPU_SYSTEM_CTRL_GENERAL_CTRL	0x24
#define OCELOT_IF_SI_OWNER_OFFSET		4
#define JAGUAR2_IF_SI_OWNER_OFFSET		6
#define MSCC_IF_SI_OWNER_MASK			GENMASK(1, 0)
#define MSCC_IF_SI_OWNER_SISL			0
#define MSCC_IF_SI_OWNER_SIBM			1
#define MSCC_IF_SI_OWNER_SIMC			2

#define MSCC_SPI_MST_SW_MODE			0x14
#define MSCC_SPI_MST_SW_MODE_SW_PIN_CTRL_MODE	BIT(13)
#define MSCC_SPI_MST_SW_MODE_SW_SPI_CS(x)	(x << 5)

#define SPARX5_FORCE_ENA			0xa4
#define SPARX5_FORCE_VAL			0xa8

struct dw_spi_mscc {
	struct regmap       *syscon;
	void __iomem        *spi_mst; /* Not sparx5 */
};

/*
 * Elba SoC does not use ssi, pin override is used for cs 0,1 and
 * gpios for cs 2,3 as defined in the device tree.
 *
 * cs:  |       1               0
 * bit: |---3-------2-------1-------0
 *      |  cs1   cs1_ovr   cs0   cs0_ovr
 */
#define ELBA_SPICS_REG			0x2468
#define ELBA_SPICS_OFFSET(cs)		((cs) << 1)
#define ELBA_SPICS_MASK(cs)		(GENMASK(1, 0) << ELBA_SPICS_OFFSET(cs))
#define ELBA_SPICS_SET(cs, val)		\
		((((val) << 1) | BIT(0)) << ELBA_SPICS_OFFSET(cs))

/*
 * The Designware SPI controller (referred to as master in the documentation)
 * automatically deasserts chip select when the tx fifo is empty. The chip
 * selects then needs to be either driven as GPIOs or, for the first 4 using
 * the SPI boot controller registers. the final chip select is an OR gate
 * between the Designware SPI controller and the SPI boot controller.
 */
static void dw_spi_mscc_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct dw_spi_mscc *dwsmscc = dwsmmio->priv;
	u32 cs = spi_get_chipselect(spi, 0);

	trace_spi_dw_mmio_set_cs(spi, enable);

	if (cs < 4) {
		u32 sw_mode = MSCC_SPI_MST_SW_MODE_SW_PIN_CTRL_MODE;

		if (!enable)
			sw_mode |= MSCC_SPI_MST_SW_MODE_SW_SPI_CS(BIT(cs));

		writel(sw_mode, dwsmscc->spi_mst + MSCC_SPI_MST_SW_MODE);
	}

	dw_spi_set_cs(spi, enable);
}

static int dw_spi_mscc_init(struct platform_device *pdev,
			    struct dw_spi_mmio *dwsmmio,
			    const char *cpu_syscon, u32 if_si_owner_offset)
{
	struct dw_spi_mscc *dwsmscc;

	dwsmscc = devm_kzalloc(&pdev->dev, sizeof(*dwsmscc), GFP_KERNEL);
	if (!dwsmscc)
		return -ENOMEM;

	dwsmscc->spi_mst = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dwsmscc->spi_mst)) {
		dev_err(&pdev->dev, "SPI_MST region map failed\n");
		return PTR_ERR(dwsmscc->spi_mst);
	}

	dwsmscc->syscon = syscon_regmap_lookup_by_compatible(cpu_syscon);
	if (IS_ERR(dwsmscc->syscon))
		return PTR_ERR(dwsmscc->syscon);

	/* Deassert all CS */
	writel(0, dwsmscc->spi_mst + MSCC_SPI_MST_SW_MODE);

	/* Select the owner of the SI interface */
	regmap_update_bits(dwsmscc->syscon, MSCC_CPU_SYSTEM_CTRL_GENERAL_CTRL,
			   MSCC_IF_SI_OWNER_MASK << if_si_owner_offset,
			   MSCC_IF_SI_OWNER_SIMC << if_si_owner_offset);

	dwsmmio->dws.set_cs = dw_spi_mscc_set_cs;
	dwsmmio->priv = dwsmscc;

	return 0;
}

static int dw_spi_mscc_ocelot_init(struct platform_device *pdev,
				   struct dw_spi_mmio *dwsmmio)
{
	return dw_spi_mscc_init(pdev, dwsmmio, "mscc,ocelot-cpu-syscon",
				OCELOT_IF_SI_OWNER_OFFSET);
}

static int dw_spi_mscc_jaguar2_init(struct platform_device *pdev,
				    struct dw_spi_mmio *dwsmmio)
{
	return dw_spi_mscc_init(pdev, dwsmmio, "mscc,jaguar2-cpu-syscon",
				JAGUAR2_IF_SI_OWNER_OFFSET);
}

/*
 * The Designware SPI controller (referred to as master in the
 * documentation) automatically deasserts chip select when the tx fifo
 * is empty. The chip selects then needs to be driven by a CS override
 * register. enable is an active low signal.
 */
static void dw_spi_sparx5_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct dw_spi_mscc *dwsmscc = dwsmmio->priv;
	u8 cs = spi_get_chipselect(spi, 0);

	if (!enable) {
		/* CS override drive enable */
		regmap_write(dwsmscc->syscon, SPARX5_FORCE_ENA, 1);
		/* Now set CSx enabled */
		regmap_write(dwsmscc->syscon, SPARX5_FORCE_VAL, ~BIT(cs));
		/* Allow settle */
		usleep_range(1, 5);
	} else {
		/* CS value */
		regmap_write(dwsmscc->syscon, SPARX5_FORCE_VAL, ~0);
		/* Allow settle */
		usleep_range(1, 5);
		/* CS override drive disable */
		regmap_write(dwsmscc->syscon, SPARX5_FORCE_ENA, 0);
	}

	dw_spi_set_cs(spi, enable);
}

static int dw_spi_mscc_sparx5_init(struct platform_device *pdev,
				   struct dw_spi_mmio *dwsmmio)
{
	const char *syscon_name = "microchip,sparx5-cpu-syscon";
	struct device *dev = &pdev->dev;
	struct dw_spi_mscc *dwsmscc;

	if (!IS_ENABLED(CONFIG_SPI_MUX)) {
		dev_err(dev, "This driver needs CONFIG_SPI_MUX\n");
		return -EOPNOTSUPP;
	}

	dwsmscc = devm_kzalloc(dev, sizeof(*dwsmscc), GFP_KERNEL);
	if (!dwsmscc)
		return -ENOMEM;

	dwsmscc->syscon =
		syscon_regmap_lookup_by_compatible(syscon_name);
	if (IS_ERR(dwsmscc->syscon)) {
		dev_err(dev, "No syscon map %s\n", syscon_name);
		return PTR_ERR(dwsmscc->syscon);
	}

	dwsmmio->dws.set_cs = dw_spi_sparx5_set_cs;
	dwsmmio->priv = dwsmscc;

	return 0;
}

static int dw_spi_alpine_init(struct platform_device *pdev,
			      struct dw_spi_mmio *dwsmmio)
{
	dwsmmio->dws.caps = DW_SPI_CAP_CS_OVERRIDE;

	return 0;
}

static int dw_spi_pssi_init(struct platform_device *pdev,
			    struct dw_spi_mmio *dwsmmio)
{
	dw_spi_dma_setup_generic(&dwsmmio->dws);

	return 0;
}

static int dw_spi_hssi_init(struct platform_device *pdev,
			    struct dw_spi_mmio *dwsmmio)
{
	dwsmmio->dws.ip = DW_HSSI_ID;

	dw_spi_dma_setup_generic(&dwsmmio->dws);

	return 0;
}

static int dw_spi_intel_init(struct platform_device *pdev,
			     struct dw_spi_mmio *dwsmmio)
{
	dwsmmio->dws.ip = DW_HSSI_ID;

	return 0;
}

/*
 * DMA-based mem ops are not configured for this device and are not tested.
 */
static int dw_spi_mountevans_imc_init(struct platform_device *pdev,
				      struct dw_spi_mmio *dwsmmio)
{
	/*
	 * The Intel Mount Evans SoC's Integrated Management Complex DW
	 * apb_ssi_v4.02a controller has an errata where a full TX FIFO can
	 * result in data corruption. The suggested workaround is to never
	 * completely fill the FIFO. The TX FIFO has a size of 32 so the
	 * fifo_len is set to 31.
	 */
	dwsmmio->dws.fifo_len = 31;

	return 0;
}

static int dw_spi_canaan_k210_init(struct platform_device *pdev,
				   struct dw_spi_mmio *dwsmmio)
{
	/*
	 * The Canaan Kendryte K210 SoC DW apb_ssi v4 spi controller is
	 * documented to have a 32 word deep TX and RX FIFO, which
	 * spi_hw_init() detects. However, when the RX FIFO is filled up to
	 * 32 entries (RXFLR = 32), an RX FIFO overrun error occurs. Avoid this
	 * problem by force setting fifo_len to 31.
	 */
	dwsmmio->dws.fifo_len = 31;

	return 0;
}

static void dw_spi_elba_override_cs(struct regmap *syscon, int cs, int enable)
{
	regmap_update_bits(syscon, ELBA_SPICS_REG, ELBA_SPICS_MASK(cs),
			   ELBA_SPICS_SET(cs, enable));
}

static void dw_spi_elba_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct regmap *syscon = dwsmmio->priv;
	u8 cs;

	cs = spi_get_chipselect(spi, 0);
	if (cs < 2)
		dw_spi_elba_override_cs(syscon, spi_get_chipselect(spi, 0), enable);

	/*
	 * The DW SPI controller needs a native CS bit selected to start
	 * the serial engine.
	 */
	spi_set_chipselect(spi, 0, 0);
	dw_spi_set_cs(spi, enable);
	spi_set_chipselect(spi, 0, cs);
}

static int dw_spi_elba_init(struct platform_device *pdev,
			    struct dw_spi_mmio *dwsmmio)
{
	struct regmap *syscon;

	syscon = syscon_regmap_lookup_by_phandle(dev_of_node(&pdev->dev),
						 "amd,pensando-elba-syscon");
	if (IS_ERR(syscon))
		return dev_err_probe(&pdev->dev, PTR_ERR(syscon),
				     "syscon regmap lookup failed\n");

	dwsmmio->priv = syscon;
	dwsmmio->dws.set_cs = dw_spi_elba_set_cs;

	return 0;
}

static void update_histogram(struct dma_profiling_stats *stats,
			     unsigned int len, bool is_dma)
{
	int i;

	for (i = 0; i < DMA_HISTOGRAM_BUCKETS; i++) {
		if (len <= histogram_size_limits[i]) {
			if (is_dma)
				stats->dma_tx_counts_hist[i]++;
			else
				stats->pio_tx_counts_hist[i]++;
			break;
		}
	}
}

static bool spi_google_can_dma(struct spi_controller *master,
			       struct spi_device *spi,
			       struct spi_transfer *xfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	dma_addr_t addr_align = (dma_addr_t)xfer->tx_buf |
				(dma_addr_t)xfer->rx_buf;
	bool can_dma = (xfer->len > dws->fifo_len) &&
			(IS_ALIGNED(xfer->len, 16) &&
			IS_ALIGNED(addr_align, 16));

	if (can_dma)
		dwsmmio->dma_stats.dma_transactions++;
	else
		dwsmmio->dma_stats.pio_transactions++;

	update_histogram(&dwsmmio->dma_stats, xfer->len, can_dma);

	return can_dma;
}

static void spi_google_set_idle_cpol_high(struct device *dev, struct dw_spi_mmio *dwsmmio)
{
	struct dw_spi *dws = &dwsmmio->dws;
	int retry = DW_SPI_WAIT_RETRIES;
	u32 cr0 = 0;

	/*
	 * The DW SSI IP drives the clock polarity as low to the pads on reset, when
	 * there is a switch in clock polarity the controller corrects the polarity right
	 * before asserting the native CS.
	 * The same does not happen when a GPIO Chip Select is used since the SPI core
	 * asserts the CS before the controller starts the transfer leading to the clock
	 * polarity changing after CS assertion which is seen as an edge to the device.
	 *
	 * Doing a 1 byte transfer with CPHA high when the pads are not driven by the
	 * controller sets the steady state polarity as high.
	 *
	 * This helps workaround this issue but only if all devices connected to the host
	 * operate with the same clock polarity setting.
	 */

	dw_spi_reset_chip(dws);
	dw_spi_enable_chip(dws, 0);
	/* Configure divider for 10Mhz*/
	dw_spi_set_clk(dws, (dws->max_freq / 10000000) & 0xffe);
	cr0 |= FIELD_PREP(DW_PSSI_CTRLR0_FRF_MASK, DW_SPI_CTRLR0_FRF_MOTO_SPI);
	cr0 |= FIELD_PREP(DW_PSSI_CTRLR0_TMOD_MASK, DW_SPI_CTRLR0_TMOD_TO);
	cr0 |= FIELD_PREP(DW_PSSI_CTRLR0_DFS32_MASK, 0x7);
	cr0 |= DW_PSSI_CTRLR0_SCPOL;
	dw_writel(dws, DW_SPI_CTRLR0, cr0);
	dw_spi_enable_chip(dws, 1);
	dw_writel(dws, DW_SPI_DR, 0xAA);
	dw_writel(dws, DW_SPI_SER, BIT(0));

	/* Poll for completion which should take less than 1us at 10Mhz*/
	while (dw_readl(dws, DW_SPI_SR) & DW_SPI_SR_BUSY && retry--)
		udelay(1);

	if (retry < 0)
		dev_err(dev, "Transfer timed out\n");
}

static void spi_google_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	bool cs_high = !!(spi->mode & SPI_CS_HIGH);

	/*
	 * Google DW SSI IP is synthesized with SSI_NUM_SLAVES = 1 hence always
	 * select chip select index 0 to initiate SPI transactions.
	 * Transfers to multiple slaves are carried out with GPIO Chip Select
	 * control.
	 */

	if (cs_high == enable) {
		dw_writel(dws, DW_SPI_SER, BIT(0));
		udelay(dwsmmio->cs_delay);
	} else
		dw_writel(dws, DW_SPI_SER, 0);
}

static void spi_google_start_aoss_ssr(struct dw_spi_mmio *dwsmmio)
{
	struct dw_spi *dws = &dwsmmio->dws;

	dev_info(dwsmmio->dev, "Starting AOSS SSR\n");

	atomic_set(&dwsmmio->alive, 0);
	if (dws->rxchan || dws->txchan)
		complete(&dws->dma_completion);

	dwsmmio->lcm_virt_addr = ioremap(0xda34000, 0x4000);

	spi_controller_suspend(dwsmmio->dws.host);
	pm_runtime_force_suspend(dwsmmio->dev);
}

static void spi_google_finish_aoss_ssr(struct dw_spi_mmio *dwsmmio)
{
	dev_info(dwsmmio->dev, "Finishing AOSS SSR\n");

	pm_runtime_force_resume(dwsmmio->dev);
	spi_controller_resume(dwsmmio->dws.host);
}

static int aoss_ssr_read(void *data, u64 *val)
{
	*val = ((struct dw_spi_mmio *)data)->aoss_ssr_started;
	return 0;
}

static int aoss_ssr_write(void *data, u64 val)
{
	struct dw_spi_mmio *dwsmmio = data;

	if (val && !dwsmmio->aoss_ssr_started)
		spi_google_start_aoss_ssr(dwsmmio);
	else if (!val && dwsmmio->aoss_ssr_started)
		spi_google_finish_aoss_ssr(dwsmmio);

	dwsmmio->aoss_ssr_started = val;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(aoss_ssr_fops, aoss_ssr_read, aoss_ssr_write, "%llu\n");

static int dma_profiling_read(struct seq_file *s, void *data)
{
	struct dw_spi_mmio *dwsmmio = s->private;
	struct dma_profiling_stats *stats = &dwsmmio->dma_stats;
	int i;

	seq_printf(s, "DMA transactions: %u\n", stats->dma_transactions);
	seq_printf(s, "PIO transactions: %u\n", stats->pio_transactions);

	seq_printf(s, "\n%-10s", "Size <=");
	for (i = 0; i < DMA_HISTOGRAM_BUCKETS; i++)
		seq_printf(s, "%8u", histogram_size_limits[i]);
	seq_printf(s, "\n");

	seq_printf(s, "%-10s", "DMA");
	for (i = 0; i < DMA_HISTOGRAM_BUCKETS; i++)
		seq_printf(s, "%8u", stats->dma_tx_counts_hist[i]);
	seq_printf(s, "\n");

	seq_printf(s, "%-10s", "PIO");
	for (i = 0; i < DMA_HISTOGRAM_BUCKETS; i++)
		seq_printf(s, "%8u", stats->pio_tx_counts_hist[i]);
	seq_printf(s, "\n");

	return 0;
}

static int dma_profiling_open(struct inode *inode, struct file *file)
{
	return single_open(file, dma_profiling_read, inode->i_private);
}

static const struct file_operations dma_profiling_fops = {
	.owner		= THIS_MODULE,
	.open		= dma_profiling_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void spi_google_debugfs_init(struct platform_device *pdev, struct dw_spi_mmio *dwsmmio)
{
	struct dentry *tmp;

	dwsmmio->debugfs = debugfs_create_dir(dev_name(&pdev->dev), NULL);
	if (IS_ERR_OR_NULL(dwsmmio->debugfs)) {
		dev_err(&pdev->dev, "failed to create debugfs directory: %ld\n",
			PTR_ERR(dwsmmio->debugfs));
		return;
	}

	tmp = debugfs_create_file("trigger-aoss-ssr", 0644, dwsmmio->debugfs,
				  dwsmmio, &aoss_ssr_fops);
	if (IS_ERR_OR_NULL(tmp)) {
		dev_err(&pdev->dev, "failed to create debugfs trigger-aoss-ssr file: %ld\n",
			PTR_ERR(tmp));
		debugfs_remove_recursive(dwsmmio->debugfs);
		dwsmmio->debugfs = NULL;
		return;
	}

	tmp = debugfs_create_file("dma-profiling", 0444, dwsmmio->debugfs,
				  dwsmmio, &dma_profiling_fops);
	if (IS_ERR_OR_NULL(tmp)) {
		dev_err(&pdev->dev, "failed to create debugfs dma-profiling file: %ld\n",
			PTR_ERR(tmp));
		debugfs_remove_recursive(dwsmmio->debugfs);
		dwsmmio->debugfs = NULL;
		return;
	}
}

static void spi_google_aoss_ssr_work_fn(struct work_struct *work)
{
	struct dw_spi_mmio *dwsmmio = container_of(work, struct dw_spi_mmio, aoss_ssr_work.work);

	dev_warn(&dwsmmio->dws.host->dev, "AOSS_SSR_ONLINE timeout. AOSS SSR %s\n",
		 dwsmmio->aoss_ssr_started ? "active" : "inactive");
	if (dwsmmio->aoss_ssr_started) {
		spi_google_finish_aoss_ssr(dwsmmio);
		dwsmmio->aoss_ssr_started = false;
	}
}

static int aoss_ssr_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	struct dw_spi_mmio *dwsmmio = container_of(notifier, struct dw_spi_mmio, aoss_ssr_nb);


	switch (event) {
	case AOSS_SSR_PG_DOWN:
		dev_info(&dwsmmio->dws.host->dev, "PG down. AOSS SSR %s\n",
			 dwsmmio->aoss_ssr_started ? "active" : "inactive");
		if (!dwsmmio->aoss_ssr_started) {
			spi_google_start_aoss_ssr(dwsmmio);
			dwsmmio->aoss_ssr_started = true;
		}
		schedule_delayed_work(&dwsmmio->aoss_ssr_work,
				      msecs_to_jiffies(AOSS_SSR_ONLINE_TIMEOUT_MS));
		break;
	case AOSS_SSR_ONLINE:
		dev_info(&dwsmmio->dws.host->dev, "Online. AOSS SSR %s\n",
			 dwsmmio->aoss_ssr_started ? "active" : "inactive");
		cancel_delayed_work_sync(&dwsmmio->aoss_ssr_work);
		if (dwsmmio->aoss_ssr_started) {
			spi_google_finish_aoss_ssr(dwsmmio);
			dwsmmio->aoss_ssr_started = false;
		}
		break;
	default:
		dev_info(&dwsmmio->dws.host->dev, "don't care\n");
		break;
	}

	return NOTIFY_OK;
}

static inline bool spi_xfer_is_dma_mapped(struct spi_controller *ctlr, struct spi_device *spi,
					  struct spi_transfer *xfer)
{
	return ctlr->can_dma && ctlr->can_dma(ctlr, spi, xfer) &&
		(xfer->tx_sg_mapped || xfer->rx_sg_mapped);
}

/* Return the max entries we can fill into tx fifo */
static inline u32 dw_spi_tx_max(struct dw_spi *dws)
{
	u32 tx_room, rxtx_gap;

	tx_room = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);

	/*
	 * Another concern is about the tx/rx mismatch, we
	 * though to use (dws->fifo_len - rxflr - txflr) as
	 * one maximum value for tx, but it doesn't cover the
	 * data which is out of tx/rx fifo and inside the
	 * shift registers. So a control from sw point of
	 * view is taken.
	 */
	rxtx_gap = dws->fifo_len - (dws->rx_len - dws->tx_len);

	return min3((u32)dws->tx_len, tx_room, rxtx_gap);
}

/* Return the max entries we should read out of rx fifo */
static inline u32 dw_spi_rx_max(struct dw_spi *dws)
{
	return min_t(u32, dws->rx_len, dw_readl(dws, DW_SPI_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
	u32 max = dw_spi_tx_max(dws);
	u32 txw = 0;

	while (max--) {
		if (dws->tx) {
			if (dws->n_bytes == 1)
				txw = *(u8 *)(dws->tx);
			else if (dws->n_bytes == 2)
				txw = *(u16 *)(dws->tx);
			else
				txw = *(u32 *)(dws->tx);

			dws->tx += dws->n_bytes;
		}
		dw_write_io_reg(dws, DW_SPI_DR, txw);
		--dws->tx_len;
	}
}

static void dw_reader(struct dw_spi *dws)
{
	u32 max = dw_spi_rx_max(dws);
	u32 rxw;

	while (max--) {
		rxw = dw_read_io_reg(dws, DW_SPI_DR);
		if (dws->rx) {
			if (dws->n_bytes == 1)
				*(u8 *)(dws->rx) = rxw;
			else if (dws->n_bytes == 2)
				*(u16 *)(dws->rx) = rxw;
			else
				*(u32 *)(dws->rx) = rxw;

			dws->rx += dws->n_bytes;
		}
		--dws->rx_len;
	}
}

static irqreturn_t dw_spi_transfer_handler(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	if (dw_spi_check_status(dws, false)) {
		spi_finalize_current_transfer(dws->host);
		return IRQ_HANDLED;
	}

	/*
	 * Read data from the Rx FIFO every time we've got a chance executing
	 * this method. If there is nothing left to receive, terminate the
	 * procedure. Otherwise adjust the Rx FIFO Threshold level if it's a
	 * final stage of the transfer. By doing so we'll get the next IRQ
	 * right when the leftover incoming data is received.
	 */
	dw_reader(dws);
	if (!dws->rx_len) {
		dw_spi_mask_intr(dws, 0xff);
		spi_finalize_current_transfer(dws->host);
	} else if (dws->rx_len <= dw_readl(dws, DW_SPI_RXFTLR)) {
		dw_writel(dws, DW_SPI_RXFTLR, dws->rx_len - 1);
	}

	/*
	 * Send data out if Tx FIFO Empty IRQ is received. The IRQ will be
	 * disabled after the data transmission is finished so not to
	 * have the TXE IRQ flood at the final stage of the transfer.
	 */
	if (irq_status & DW_SPI_INT_TXEI) {
		dw_writer(dws);
		if (!dws->tx_len)
			dw_spi_mask_intr(dws, DW_SPI_INT_TXEI);
	}

	return IRQ_HANDLED;
}

static void dw_spi_irq_setup(struct dw_spi *dws)
{
	u16 level;
	u8 imask;

	/*
	 * Originally Tx and Rx data lengths match. Rx FIFO Threshold level
	 * will be adjusted at the final stage of the IRQ-based SPI transfer
	 * execution so not to lose the leftover of the incoming data.
	 */
	level = min_t(unsigned int, dws->fifo_len / 2, dws->tx_len);
	dw_writel(dws, DW_SPI_TXFTLR, level);
	dw_writel(dws, DW_SPI_RXFTLR, level - 1);

	dws->transfer_handler = dw_spi_transfer_handler;

	imask = DW_SPI_INT_TXEI | DW_SPI_INT_TXOI | DW_SPI_INT_RXUI |
		DW_SPI_INT_RXOI | DW_SPI_INT_RXFI;
	dw_spi_umask_intr(dws, imask);
}

static void _goog_spi_transfer_delay_ns(struct dw_spi_mmio *dwsmmio, u32 ns)
{
	if (!ns)
		return;
	if (ns <= NSEC_PER_USEC) {
		ndelay(ns);
	} else {
		u32 us = DIV_ROUND_UP(ns, NSEC_PER_USEC);

		if (us <= 10)
			udelay(us);
		else
			usleep_range(us, us + DIV_ROUND_UP(us, 10));
	}
}

static int goog_spi_delay_exec(struct dw_spi_mmio *dwsmmio,
			       struct spi_delay *_delay,
			       struct spi_transfer *xfer)
{
	int delay;

	might_sleep();

	if (!_delay)
		return -EINVAL;

	delay = spi_delay_to_ns(_delay, xfer);
	if (delay < 0)
		return delay;

	_goog_spi_transfer_delay_ns(dwsmmio, delay);

	return 0;
}

static int goog_dw_spi_poll_transfer(struct dw_spi *dws,
				     struct spi_transfer *transfer)
{
	struct spi_delay delay;
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	u16 nbits;
	int ret;
	u64 lp_cnt = 0;
	u64 max_lp_cnt;

	delay.unit = SPI_DELAY_UNIT_SCK;
	nbits = dws->n_bytes * BITS_PER_BYTE;

	max_lp_cnt = (u64)(transfer->len) << SPI_MAX_LOOP_CNT_FACTOR;
	if (max_lp_cnt > SPI_MAX_LOOP_CNT_LIMIT)
		max_lp_cnt = SPI_MAX_LOOP_CNT_LIMIT;

	do {
		dw_writer(dws);

		delay.value = nbits * (dws->rx_len - dws->tx_len);
		goog_spi_delay_exec(dwsmmio, &delay, transfer);

		dw_reader(dws);

		ret = dw_spi_check_status(dws, true);
		if (ret)
			return ret;

		++lp_cnt;
	} while (lp_cnt <= max_lp_cnt && dws->rx_len && atomic_read(&dwsmmio->alive));

	if (dws->rx_len != 0) {
		dev_err(&dws->host->dev,
			"%s: fail to read entire rx buf! %u bytes left (xfer_len=%u bytes loop_iter=%llu)\n",
			__func__, dws->rx_len * dws->n_bytes, transfer->len, lp_cnt);
		return -EIO;
	}

	return 0;
}

static int goog_dw_spi_transfer_one(struct spi_controller *host,
				    struct spi_device *spi,
				    struct spi_transfer *transfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(host);
	struct dw_spi_cfg cfg = {
		.tmode = DW_SPI_CTRLR0_TMOD_TR,
		.dfs = transfer->bits_per_word,
		.freq = transfer->speed_hz,
	};
	int ret;

	dws->dma_mapped = 0;
	dws->n_bytes = roundup_pow_of_two(BITS_TO_BYTES(transfer->bits_per_word));
	dws->tx = (void *)transfer->tx_buf;
	dws->tx_len = transfer->len / dws->n_bytes;
	dws->rx = transfer->rx_buf;
	dws->rx_len = dws->tx_len;

	/* Ensure the data above is visible for all CPUs */
	smp_mb();

	dw_spi_enable_chip(dws, 0);

	dw_spi_update_config(dws, spi, &cfg);

	transfer->effective_speed_hz = dws->current_freq;

	/* Check if current transfer is a DMA transaction */
	dws->dma_mapped = spi_xfer_is_dma_mapped(host, spi, transfer);

	/* For poll mode just disable all interrupts */
	dw_spi_mask_intr(dws, 0xff);

	if (dws->dma_mapped) {
		ret = dws->dma_ops->dma_setup(dws, transfer);
		if (ret)
			return ret;
	}

	dw_spi_enable_chip(dws, 1);

	if (dws->dma_mapped)
		return dws->dma_ops->dma_transfer(dws, transfer);
	else if (dws->irq == IRQ_NOTCONNECTED)
		return goog_dw_spi_poll_transfer(dws, transfer);

	dw_spi_irq_setup(dws);

	return 1;
}

/*
 * dws->dma_chan_busy is set before the dma transfer starts, callback for rx
 * channel will clear a corresponding bit.
 */
static void goog_dw_spi_dma_rx_done(void *arg)
{
	struct dw_spi *dws = arg;

	clear_bit(DW_SPI_RX_BUSY, &dws->dma_chan_busy);
	if (test_bit(DW_SPI_TX_BUSY, &dws->dma_chan_busy))
		return;

	complete(&dws->dma_completion);
}

/*
 * dws->dma_chan_busy is set before the dma transfer starts, callback for tx
 * channel will clear a corresponding bit.
 */
static void goog_dw_spi_dma_tx_done(void *arg)
{
	struct dw_spi *dws = arg;

	clear_bit(DW_SPI_TX_BUSY, &dws->dma_chan_busy);
	if (test_bit(DW_SPI_RX_BUSY, &dws->dma_chan_busy))
		return;

	complete(&dws->dma_completion);
}

static int goog_dw_spi_dma_wait(struct dw_spi_mmio *dwsmmio, struct dw_spi *dws,
				unsigned int len, u32 speed)
{
	unsigned long long ms;

	ms = len * MSEC_PER_SEC * BITS_PER_BYTE;
	do_div(ms, speed);
	ms += ms + 200;

	if (ms > GOOG_SPI_DMA_TIMEOUT_MAX_MS)
		ms = GOOG_SPI_DMA_TIMEOUT_MAX_MS;

	ms = wait_for_completion_timeout(&dws->dma_completion,
					 msecs_to_jiffies(ms));

	if (ms == 0) {
		dev_err(&dws->host->cur_msg->spi->dev,
			"DMA transaction timed out\n");
		return -ETIMEDOUT;
	} else if (!atomic_read(&dwsmmio->alive)) {
		dev_err(&dws->host->cur_msg->spi->dev,
			"DMA xfer interrupted by AOSS SSR\n");
		return -EIO;
	}

	return 0;
}

static inline bool goog_dw_spi_dma_rx_busy(struct dw_spi *dws)
{
	return !!(dw_readl(dws, DW_SPI_SR) & DW_SPI_SR_RF_NOT_EMPT);
}

static inline bool goog_dw_spi_dma_tx_busy(struct dw_spi *dws)
{
	return !(dw_readl(dws, DW_SPI_SR) & DW_SPI_SR_TF_EMPT);
}

static int goog_dw_spi_dma_submit_tx(struct dw_spi *dws, struct scatterlist *sgl,
				     unsigned int nents)
{
	struct dma_async_tx_descriptor *txdesc;
	dma_cookie_t cookie;
	int ret;

	txdesc = dmaengine_prep_slave_sg(dws->txchan, sgl, nents,
					 DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		return -ENOMEM;

	txdesc->callback = goog_dw_spi_dma_tx_done;
	txdesc->callback_param = dws;

	cookie = dmaengine_submit(txdesc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(dws->txchan);
		return ret;
	}

	set_bit(DW_SPI_TX_BUSY, &dws->dma_chan_busy);

	return 0;
}

static int goog_dw_spi_dma_submit_rx(struct dw_spi *dws, struct scatterlist *sgl,
				     unsigned int nents)
{
	struct dma_async_tx_descriptor *rxdesc;
	dma_cookie_t cookie;
	int ret;

	rxdesc = dmaengine_prep_slave_sg(dws->rxchan, sgl, nents,
					 DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		return -ENOMEM;

	rxdesc->callback = goog_dw_spi_dma_rx_done;
	rxdesc->callback_param = dws;

	cookie = dmaengine_submit(rxdesc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(dws->rxchan);
		return ret;
	}

	set_bit(DW_SPI_RX_BUSY, &dws->dma_chan_busy);

	return 0;
}

static int goog_dw_spi_dma_transfer_all(struct dw_spi *dws,
					struct spi_transfer *xfer)
{
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	int ret;

	/* Submit the DMA Tx transfer */
	ret = goog_dw_spi_dma_submit_tx(dws, xfer->tx_sg.sgl, xfer->tx_sg.nents);
	if (ret)
		goto err_clear_dmac;

	/* Submit the DMA Rx transfer if required */
	if (xfer->rx_buf) {
		ret = goog_dw_spi_dma_submit_rx(dws, xfer->rx_sg.sgl,
					   xfer->rx_sg.nents);
		if (ret)
			goto err_clear_dmac;

		/* rx must be started before tx due to spi instinct */
		dma_async_issue_pending(dws->rxchan);
	}

	dma_async_issue_pending(dws->txchan);

	ret = goog_dw_spi_dma_wait(dwsmmio, dws, xfer->len, xfer->effective_speed_hz);

err_clear_dmac:
	dw_writel(dws, DW_SPI_DMACR, 0);

	return ret;
}

/*
 * In case if at least one of the requested DMA channels doesn't support the
 * hardware accelerated SG list entries traverse, the DMA driver will most
 * likely work that around by performing the IRQ-based SG list entries
 * resubmission. That might and will cause a problem if the DMA Tx channel is
 * recharged and re-executed before the Rx DMA channel. Due to
 * non-deterministic IRQ-handler execution latency the DMA Tx channel will
 * start pushing data to the SPI bus before the Rx DMA channel is even
 * reinitialized with the next inbound SG list entry. By doing so the DMA Tx
 * channel will implicitly start filling the DW APB SSI Rx FIFO up, which while
 * the DMA Rx channel being recharged and re-executed will eventually be
 * overflown.
 *
 * In order to solve the problem we have to feed the DMA engine with SG list
 * entries one-by-one. It shall keep the DW APB SSI Tx and Rx FIFOs
 * synchronized and prevent the Rx FIFO overflow. Since in general the tx_sg
 * and rx_sg lists may have different number of entries of different lengths
 * (though total length should match) let's virtually split the SG-lists to the
 * set of DMA transfers, which length is a minimum of the ordered SG-entries
 * lengths. An ASCII-sketch of the implemented algo is following:
 *                  xfer->len
 *                |___________|
 * tx_sg list:    |___|____|__|
 * rx_sg list:    |_|____|____|
 * DMA transfers: |_|_|__|_|__|
 *
 * Note in order to have this workaround solving the denoted problem the DMA
 * engine driver should properly initialize the max_sg_burst capability and set
 * the DMA device max segment size parameter with maximum data block size the
 * DMA engine supports.
 */

static int goog_dw_spi_dma_transfer_one(struct dw_spi *dws,
					struct spi_transfer *xfer)
{
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct scatterlist *tx_sg = NULL, *rx_sg = NULL, tx_tmp, rx_tmp;
	unsigned int tx_len = 0, rx_len = 0;
	unsigned int base, len;
	int ret;

	sg_init_table(&tx_tmp, 1);
	sg_init_table(&rx_tmp, 1);

	for (base = 0; base < xfer->len; base += len) {
		/* Fetch next Tx DMA data chunk */
		if (!tx_len) {
			tx_sg = !tx_sg ? &xfer->tx_sg.sgl[0] : sg_next(tx_sg);
			sg_dma_address(&tx_tmp) = sg_dma_address(tx_sg);
			tx_len = sg_dma_len(tx_sg);
		}

		/* Fetch next Rx DMA data chunk */
		if (!rx_len) {
			rx_sg = !rx_sg ? &xfer->rx_sg.sgl[0] : sg_next(rx_sg);
			sg_dma_address(&rx_tmp) = sg_dma_address(rx_sg);
			rx_len = sg_dma_len(rx_sg);
		}

		len = min(tx_len, rx_len);

		sg_dma_len(&tx_tmp) = len;
		sg_dma_len(&rx_tmp) = len;

		/* Submit DMA Tx transfer */
		ret = goog_dw_spi_dma_submit_tx(dws, &tx_tmp, 1);
		if (ret)
			break;

		/* Submit DMA Rx transfer */
		ret = goog_dw_spi_dma_submit_rx(dws, &rx_tmp, 1);
		if (ret)
			break;

		/* Rx must be started before Tx due to SPI instinct */
		dma_async_issue_pending(dws->rxchan);

		dma_async_issue_pending(dws->txchan);

		/*
		 * Here we only need to wait for the DMA transfer to be
		 * finished since SPI controller is kept enabled during the
		 * procedure this loop implements and there is no risk to lose
		 * data left in the Tx/Rx FIFOs.
		 */
		ret = goog_dw_spi_dma_wait(dwsmmio, dws, len, xfer->effective_speed_hz);
		if (ret)
			break;

		reinit_completion(&dws->dma_completion);

		sg_dma_address(&tx_tmp) += len;
		sg_dma_address(&rx_tmp) += len;
		tx_len -= len;
		rx_len -= len;
	}

	dw_writel(dws, DW_SPI_DMACR, 0);

	return ret;
}

static int goog_dw_spi_dma_wait_tx_done(struct dw_spi *dws,
					struct spi_transfer *xfer)
{
	int retry = DW_SPI_WAIT_RETRIES;
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct spi_delay delay;
	u32 nents;

	nents = dw_readl(dws, DW_SPI_TXFLR);
	delay.unit = SPI_DELAY_UNIT_SCK;
	delay.value = nents * dws->n_bytes * BITS_PER_BYTE;

	while (goog_dw_spi_dma_tx_busy(dws) && retry--)
		goog_spi_delay_exec(dwsmmio, &delay, xfer);

	if (retry < 0) {
		dev_err(&dws->host->dev, "Tx hanged up\n");
		return -EIO;
	}

	return 0;
}

static int goog_dw_spi_dma_wait_rx_done(struct dw_spi *dws)
{
	int retry = DW_SPI_WAIT_RETRIES;
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct spi_delay delay;
	unsigned long ns, us;
	u32 nents;

	/*
	 * It's unlikely that DMA engine is still doing the data fetching, but
	 * if it's let's give it some reasonable time. The timeout calculation
	 * is based on the synchronous APB/SSI reference clock rate, on a
	 * number of data entries left in the Rx FIFO, times a number of clock
	 * periods normally needed for a single APB read/write transaction
	 * without PREADY signal utilized (which is true for the DW APB SSI
	 * controller).
	 */
	nents = dw_readl(dws, DW_SPI_RXFLR);
	ns = 4U * NSEC_PER_SEC / dws->max_freq * nents;
	if (ns <= NSEC_PER_USEC) {
		delay.unit = SPI_DELAY_UNIT_NSECS;
		delay.value = ns;
	} else {
		us = DIV_ROUND_UP(ns, NSEC_PER_USEC);
		delay.unit = SPI_DELAY_UNIT_USECS;
		delay.value = clamp_val(us, 0, USHRT_MAX);
	}

	while (goog_dw_spi_dma_rx_busy(dws) && retry--)
		goog_spi_delay_exec(dwsmmio, &delay, NULL);

	if (retry < 0) {
		dev_err(&dws->host->dev, "Rx hanged up\n");
		return -EIO;
	}

	return 0;
}

static int goog_dw_spi_dma_transfer(struct dw_spi *dws, struct spi_transfer *xfer)
{
	unsigned int nents;
	int ret;

	nents = max(xfer->tx_sg.nents, xfer->rx_sg.nents);

	/*
	 * Execute normal DMA-based transfer (which submits the Rx and Tx SG
	 * lists directly to the DMA engine at once) if either full hardware
	 * accelerated SG list traverse is supported by both channels, or the
	 * Tx-only SPI transfer is requested, or the DMA engine is capable to
	 * handle both SG lists on hardware accelerated basis.
	 */
	if (!dws->dma_sg_burst || !xfer->rx_buf || nents <= dws->dma_sg_burst)
		ret = goog_dw_spi_dma_transfer_all(dws, xfer);
	else
		ret = goog_dw_spi_dma_transfer_one(dws, xfer);
	if (ret)
		return ret;

	if (dws->host->cur_msg->status == -EINPROGRESS) {
		ret = goog_dw_spi_dma_wait_tx_done(dws, xfer);
		if (ret)
			return ret;
	}

	if (xfer->rx_buf && dws->host->cur_msg->status == -EINPROGRESS)
		ret = goog_dw_spi_dma_wait_rx_done(dws);

	return ret;
}

static int spi_google_init(struct platform_device *pdev,
			   struct dw_spi_mmio *dwsmmio)
{
	int num_cs_gpios;

	if (device_property_present(&pdev->dev, "spi-dma-16B-align-only"))
		dwsmmio->quirks |= SPI_DMA_16B_ALIGN_ONLY;

	if (device_property_present(&pdev->dev, "google,spi-gpio-cs-with-cpol-high")) {
		num_cs_gpios = gpiod_count(&pdev->dev, "cs");
		if (num_cs_gpios > 0)
			dwsmmio->quirks |= GOOGLE_SPI_GPIO_CS_WITH_CPOL_HIGH;
		else
			dev_warn(&pdev->dev,
				 "google,spi-gpio-cs-with-cpol-high set without GPIO based CS\n");
	}

	dw_spi_dma_setup_generic(&dwsmmio->dws);
	dwsmmio->quirks |= GOOGLE_SPI_OVERWRITE_DMA_TX_BURST;

	if (dwsmmio->dws.dma_ops) {
		memcpy(&dwsmmio->dma_ops_modified,
		       dwsmmio->dws.dma_ops,
		       sizeof(struct dw_spi_dma_ops));

		dwsmmio->dma_ops_modified.dma_transfer = goog_dw_spi_dma_transfer;

		if (dwsmmio->quirks & SPI_DMA_16B_ALIGN_ONLY)
			dwsmmio->dma_ops_modified.can_dma = spi_google_can_dma;

		dwsmmio->dws.dma_ops = &dwsmmio->dma_ops_modified;
	}

	if (dwsmmio->quirks & GOOGLE_SPI_GPIO_CS_WITH_CPOL_HIGH) {
		pinctrl_pm_select_sleep_state(&pdev->dev);
		spi_google_set_idle_cpol_high(&pdev->dev, dwsmmio);
		pinctrl_pm_select_default_state(&pdev->dev);
	}

	dwsmmio->dws.set_cs = spi_google_set_cs;
	spi_google_debugfs_init(pdev, dwsmmio);

	return 0;
}

static int dw_spi_mmio_setup(struct device *dev,
			     struct dw_spi_mmio *dwsmmio)
{
	int ret;

	ret = clk_set_rate(dwsmmio->clk, MAX_INPUT_SERIAL_CLOCK_HZ);
	if (ret)
		return ret;

	dwsmmio->dws.max_freq = clk_get_rate(dwsmmio->clk);

	ret = clk_prepare_enable(dwsmmio->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dwsmmio->pclk);
	if (ret)
		return ret;

	ret = reset_control_assert(dwsmmio->rstc);
	if (ret)
		return ret;

	if (dwsmmio->max_freq_at_probe != 0 && dwsmmio->max_freq_at_probe != dwsmmio->dws.max_freq)
		dev_warn(dev, "cannot restore serial clock: want %u Hz, got %u Hz\n",
			 dwsmmio->max_freq_at_probe,
			 dwsmmio->dws.max_freq);

	return reset_control_deassert(dwsmmio->rstc);
}

static int dw_spi_mmio_probe(struct platform_device *pdev)
{
	int (*init_func)(struct platform_device *pdev,
			 struct dw_spi_mmio *dwsmmio);
	struct dw_spi_mmio *dwsmmio;
	struct resource *mem;
	struct dw_spi *dws;
	bool ext_power_control;
	int ret;
	int num_cs;

	spi_dw_mmio_trace_init(pdev);
	trace_spi_dw_mmio_probe(&pdev->dev);

	dwsmmio = devm_kzalloc(&pdev->dev, sizeof(struct dw_spi_mmio),
			       GFP_KERNEL);
	if (!dwsmmio)
		return -ENOMEM;

	dwsmmio->dev = &pdev->dev;
	dws = &dwsmmio->dws;

	/* Get basic io resource and map it */
	dws->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(dws->regs))
		return PTR_ERR(dws->regs);

	dws->paddr = mem->start;

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0)
		return dws->irq; /* -ENXIO */

	irq_set_status_flags(dws->irq, IRQ_DISABLE_UNLAZY);
	if (device_property_read_bool(&pdev->dev, "polling-busy-wait-mode"))
		dws->irq = IRQ_NOTCONNECTED;

	dwsmmio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dwsmmio->clk))
		return PTR_ERR(dwsmmio->clk);

	/* Optional clock needed to access the registers */
	dwsmmio->pclk = devm_clk_get_optional(&pdev->dev, "pclk");
	if (IS_ERR(dwsmmio->pclk)) {
		ret = PTR_ERR(dwsmmio->pclk);
		goto out_clk;
	}

	/* find an optional reset controller */
	dwsmmio->rstc = devm_reset_control_get_optional_exclusive(&pdev->dev, "spi");
	if (IS_ERR(dwsmmio->rstc)) {
		ret = PTR_ERR(dwsmmio->rstc);
		goto out_clk;
	}

	ret = dw_spi_mmio_setup(&pdev->dev, dwsmmio);
	if (ret)
		goto out;

	dev_info(&pdev->dev, "input serial clock set to %d Hz\n", dws->max_freq);
	dwsmmio->max_freq_at_probe = dws->max_freq;
	dws->bus_num = pdev->id;

	if (device_property_read_u32(&pdev->dev, "reg-io-width",
				     &dws->reg_io_width))
		dws->reg_io_width = 4;

	num_cs = 4;

	device_property_read_u32(&pdev->dev, "num-cs", &num_cs);

	dws->num_cs = num_cs;

	device_property_read_u32(&pdev->dev, "google,cs-clock-delay-us", &dwsmmio->cs_delay);

	init_func = device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func(pdev, dwsmmio);
		if (ret)
			goto out;
	}

	dwsmmio->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(dwsmmio->pinctrl)) {
		dwsmmio->cli_state = pinctrl_lookup_state(dwsmmio->pinctrl, "cli");
		if (IS_ERR(dwsmmio->cli_state))
			dwsmmio->cli_state = NULL;
	} else {
		dwsmmio->pinctrl = NULL;
	}

	/* We must set drvdata before enabling pm_runtime. We have to expect
	 * for our suspend and resume callbacks to be called as soon as
	 * pm_runtime is enabled, and those functions reference drvdata.
	 */
	platform_set_drvdata(pdev, dwsmmio);

	/* Increment PM usage to avoid possible spurious runtime suspend */
	pm_runtime_get_noresume(&pdev->dev);

	WARN_ON(pm_runtime_enabled(&pdev->dev));
	ext_power_control = of_property_read_bool(pdev->dev.of_node,
						  "external-power-control");
	if (!ext_power_control) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
						 RPM_AUTOSUSPEND_DELAY_MS);
		pm_runtime_use_autosuspend(&pdev->dev);
	}
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		goto out;

	/* Initialize cur_rx_dma_dev and cur_tx_dma_dev to avoid NULL pointer
	 * dereference in __spi_unmap_msg if the first transfer mapping fails.
	 * b/507193229
	 */
	if (dws->host) {
		struct device *dma_dev = dws->host->dma_map_dev ?
					 dws->host->dma_map_dev : dws->host->dev.parent;
		dws->host->cur_rx_dma_dev = dma_dev;
		dws->host->cur_tx_dma_dev = dma_dev;
	}

	if (dws->host->can_dma && dwsmmio->quirks & GOOGLE_SPI_OVERWRITE_DMA_TX_BURST) {
		dws->txburst = dws->fifo_len / 2;
		dw_writel(dws, DW_SPI_DMATDLR, dws->txburst);
	}

	platform_set_drvdata(pdev, dwsmmio);

	/* Google callbacks instead of default ones */
	dwsmmio->dws.host->transfer_one = goog_dw_spi_transfer_one;
	atomic_set(&dwsmmio->alive, 1);

	if (of_property_read_bool(pdev->dev.of_node, "aoc-ssr-capable")) {
		dwsmmio->aoss_ssr_nb.notifier_call = aoss_ssr_notifier;
		INIT_DELAYED_WORK(&dwsmmio->aoss_ssr_work, spi_google_aoss_ssr_work_fn);
		aoss_ssr_add_notifier(&dwsmmio->aoss_ssr_nb);
	}

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	if (ext_power_control)
		pm_runtime_suspend(&pdev->dev);

	return 0;

out:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	clk_disable_unprepare(dwsmmio->pclk);
out_clk:
	clk_disable_unprepare(dwsmmio->clk);
	reset_control_assert(dwsmmio->rstc);

	return ret;
}

static void dw_spi_mmio_remove(struct platform_device *pdev)
{
	struct dw_spi_mmio *dwsmmio = platform_get_drvdata(pdev);

	debugfs_remove_recursive(dwsmmio->debugfs);
	trace_spi_dw_mmio_remove(&pdev->dev);

	dw_spi_remove_host(&dwsmmio->dws);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	clk_disable_unprepare(dwsmmio->pclk);
	clk_disable_unprepare(dwsmmio->clk);
	reset_control_assert(dwsmmio->rstc);
	aoss_ssr_remove_notifier(&dwsmmio->aoss_ssr_nb);
	disable_delayed_work_sync(&dwsmmio->aoss_ssr_work);
}

#if IS_ENABLED(CONFIG_PM)
/**
 * dw_spi_mmio_runtime_suspend - disable controller,
 * gate ip clock and apb clocks
 * and reconfigure pins to GPIO open drain
 * @dev: pointer to spi device
 */
static int dw_spi_mmio_runtime_suspend(struct device *dev)
{
	struct dw_spi_mmio *dwsmmio = dev_get_drvdata(dev);
	struct dw_spi *dws = &dwsmmio->dws;
	int ret;

	ret = pinctrl_pm_select_sleep_state(dev);
	if (ret)
		return ret;

	atomic_set(&dwsmmio->alive, 0);

	disable_irq(dws->irq);
	dw_spi_shutdown_chip(dws);
	clk_disable_unprepare(dwsmmio->pclk);
	clk_disable_unprepare(dwsmmio->clk);
	reset_control_assert(dwsmmio->rstc);

	trace_spi_dw_mmio_runtime_suspend(dev);

	return 0;
}

static int dw_spi_mmio_runtime_resume(struct device *dev)
{
	struct dw_spi_mmio *dwsmmio = dev_get_drvdata(dev);
	struct dw_spi *dws = &dwsmmio->dws;
	int ret;

	if (dwsmmio->lcm_virt_addr) {
		iowrite32(0, dwsmmio->lcm_virt_addr + 0xdd0);
		iowrite32(2, dwsmmio->lcm_virt_addr + 0xdd4);
		iowrite32(1, dwsmmio->lcm_virt_addr + 0xdd0);
	}

	ret = dw_spi_mmio_setup(dev, dwsmmio);
	if (ret)
		return ret;

	if (dwsmmio->cli_state) {
		ret = pinctrl_select_state(dwsmmio->pinctrl, dwsmmio->cli_state);
		if (ret)
			return ret;
	}

	dws->current_freq = 0;
	dws->cur_rx_sample_dly = 0;

	if (dwsmmio->quirks & GOOGLE_SPI_GPIO_CS_WITH_CPOL_HIGH)
		spi_google_set_idle_cpol_high(dev, dwsmmio);

	atomic_set(&dwsmmio->alive, 1);

	dw_spi_reset_chip(dws);
	enable_irq(dws->irq);

	if (dws->host->can_dma) {
		dw_writel(dws, DW_SPI_DMATDLR, dws->txburst);
		dw_writel(dws, DW_SPI_DMARDLR, dws->rxburst - 1);
	}

	trace_spi_dw_mmio_runtime_resume(dev);

	return pinctrl_pm_select_default_state(dev);
}
#endif /* CONFIG_PM */

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int dw_spi_mmio_suspend(struct device *dev)
{
	struct dw_spi_mmio *dwsmmio = dev_get_drvdata(dev);
	struct dw_spi *dws = &dwsmmio->dws;
	int ret;

	ret = spi_controller_suspend(dws->host);
	if (ret)
		return ret;

	return pm_runtime_force_suspend(dev);
}

static int dw_spi_mmio_resume(struct device *dev)
{
	struct dw_spi_mmio *dwsmmio = dev_get_drvdata(dev);
	struct dw_spi *dws = &dwsmmio->dws;
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	return spi_controller_resume(dws->host);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dw_spi_mmio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_spi_mmio_suspend, dw_spi_mmio_resume)
	SET_RUNTIME_PM_OPS(dw_spi_mmio_runtime_suspend, dw_spi_mmio_runtime_resume, NULL)
};

static const struct of_device_id dw_spi_mmio_of_match[] = {
	{ .compatible = "snps,dw-apb-ssi", .data = dw_spi_pssi_init},
	{ .compatible = "mscc,ocelot-spi", .data = dw_spi_mscc_ocelot_init},
	{ .compatible = "mscc,jaguar2-spi", .data = dw_spi_mscc_jaguar2_init},
	{ .compatible = "amazon,alpine-dw-apb-ssi", .data = dw_spi_alpine_init},
	{ .compatible = "renesas,rzn1-spi", .data = dw_spi_pssi_init},
	{ .compatible = "snps,dwc-ssi-1.01a", .data = dw_spi_hssi_init},
	{ .compatible = "intel,keembay-ssi", .data = dw_spi_intel_init},
	{ .compatible = "intel,thunderbay-ssi", .data = dw_spi_intel_init},
		{
		.compatible = "intel,mountevans-imc-ssi",
		.data = dw_spi_mountevans_imc_init,
	},
	{ .compatible = "microchip,sparx5-spi", dw_spi_mscc_sparx5_init},
	{ .compatible = "canaan,k210-spi", dw_spi_canaan_k210_init},
	{ .compatible = "amd,pensando-elba-spi", .data = dw_spi_elba_init},
	{ .compatible = "google,spi", .data = spi_google_init },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, dw_spi_mmio_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id dw_spi_mmio_acpi_match[] = {
	{"HISI0173", (kernel_ulong_t)dw_spi_pssi_init},
	{},
};
MODULE_DEVICE_TABLE(acpi, dw_spi_mmio_acpi_match);
#endif

static struct platform_driver dw_spi_mmio_driver = {
	.probe		= dw_spi_mmio_probe,
	.remove_new	= dw_spi_mmio_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = dw_spi_mmio_of_match,
		.acpi_match_table = ACPI_PTR(dw_spi_mmio_acpi_match),
		.pm = &dw_spi_mmio_pm_ops,
	},
};
module_platform_driver(dw_spi_mmio_driver);

MODULE_AUTHOR("Jean-Hugues Deschenes <jean-hugues.deschenes@octasic.com>");
MODULE_DESCRIPTION("Memory-mapped I/O interface driver for DW SPI Core");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SPI_DW_CORE);
