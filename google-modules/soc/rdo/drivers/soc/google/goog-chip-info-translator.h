// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google Chip Info Driver - Translator functions
 *
 * Copyright (c) 2024-2025 Google LLC
 */

/*
 * Steps to add a translator:
 * 1. Implement a translator callback followed the type of `goog_chip_info_translator_cb`.
 * 2. Add a new entry to `translator_list` with name and pointer.
 */
static int goog_chip_info_dvfs_translator(struct goog_chip_info_data *data,
					  struct goog_chip_info_feature *feature,
					  u64 value, int index, char *buffer, int max_size)
{
	int size;

	if (feature->type == GOOG_CHIP_INFO_LGA_A0_DVFS && index == LGA_A0_DVFS_DVFS_REV)
		return 0;

	if (feature->type == GOOG_CHIP_INFO_LGA_B0_DVFS && index == LGA_B0_DVFS_DVFS_REV)
		return 0;

	if (feature->type == GOOG_CHIP_INFO_MBU_A0_DVFS && index == MBU_A0_DVFS_DVFS_REV)
		return 0;

	if (value == 0)
		size = snprintf(buffer, max_size, "disabled");
	else
		size = snprintf(buffer, max_size, "%llu mV", value * 5 + 300);

	return size;
}

static inline int goog_chip_info_ids_get_decimal_num_mbu_a0(int index)
{
	switch (index) {
	case MBU_A0_IDS_IDS_AMB_LOGIC_RT:
	case MBU_A0_IDS_IDS_AUR_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU0_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU1_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU2_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU2_SRAM_RT:
	case MBU_A0_IDS_IDS_DSU_LOGIC_RT:
	case MBU_A0_IDS_IDS_GMC0123_LOGIC_RT:
	case MBU_A0_IDS_IDS_GPU_INFRA_MM_AUR_SRAM_RT:
	case MBU_A0_IDS_IDS_GPU_LOGIC_RT:
	case MBU_A0_IDS_IDS_INFRA_LOGIC_RT:
	case MBU_A0_IDS_IDS_MM_LOGIC_RT:
	case MBU_A0_IDS_IDS_TPU_LOGIC_RT:
		return 3;
	case MBU_A0_IDS_IDS_AOSSAON_LOGIC_RT:
	case MBU_A0_IDS_IDS_AOSSOD_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU1_SRAM_RT:
	case MBU_A0_IDS_IDS_DSU_SRAM_RT:
	case MBU_A0_IDS_IDS_HSION_LOGIC_RT:
	case MBU_A0_IDS_IDS_STBY_DPA_SRAM_RT:
	case MBU_A0_IDS_IDS_STBY_STBYS_SECACC_LOGIC_RT:
	case MBU_A0_IDS_IDS_TPU_SRAM_RT:
	case MBU_A0_IDS_IDS_TPU_SSWRP_LOGIC_RT:
		return 4;
	case MBU_A0_IDS_IDS_GMC0123_SRAM_RT:
	case MBU_A0_IDS_IDS_HSIOS_LOGIC_RT:
		return 5;
	case MBU_A0_IDS_IDS_AOSSGSM_LOGIC_RT:
	case MBU_A0_IDS_IDS_CPU0_SRAM_RT:
		return 6;
	case MBU_A0_IDS_IDS_AMB_SRAM_RT:
		return 7;
	}

	return -ENOENT;
}

static inline int goog_chip_info_ids_get_decimal_num_lga_a0(int index)
{
	switch (index) {
	case LGA_A0_IDS_IDS_AMB_LOGIC_RT:
	case LGA_A0_IDS_IDS_AUR_LOGIC_RT:
	case LGA_A0_IDS_IDS_CPU0_LOGIC_RT:
	case LGA_A0_IDS_IDS_CPU1_LOGIC_RT:
	case LGA_A0_IDS_IDS_CPU2_LOGIC_RT:
	case LGA_A0_IDS_IDS_GMC_LOGIC_RT:
	case LGA_A0_IDS_IDS_G3D_LOGIC_RT:
	case LGA_A0_IDS_IDS_INF_LOGIC_RT:
	case LGA_A0_IDS_IDS_MM_LOGIC_RT:
	case LGA_A0_IDS_IDS_TPU_LOGIC_RT:
	case LGA_A0_IDS_IDS_TPU_SRAM_RT:
		return 3;
	case LGA_A0_IDS_IDS_AOC_LOGIC_RT:
	case LGA_A0_IDS_IDS_AOC_SRAM_RT:
	case LGA_A0_IDS_IDS_AUR_SRAM_RT:
	case LGA_A0_IDS_IDS_CPU1_SRAM_RT:
	case LGA_A0_IDS_IDS_CPU2_SRAM_RT:
	case LGA_A0_IDS_IDS_CPU0_SRAM_RT:
	case LGA_A0_IDS_IDS_HSION_LOGIC_RT:
	case LGA_A0_IDS_IDS_HSIOS_LOGIC_RT:
	case LGA_A0_IDS_IDS_SLC_SRAM_RT:
		return 4;
	case LGA_A0_IDS_IDS_G3D_SRAM_RT:
	case LGA_A0_IDS_IDS_AMB_SRAM_RT:
		return 0;
	}

	return -ENOENT;
}

static int goog_chip_info_ids_get_decimal_num(struct goog_chip_info_feature *feature,
					      int index)
{
	if (feature->type == GOOG_CHIP_INFO_LGA_A0_IDS)
		return goog_chip_info_ids_get_decimal_num_lga_a0(index);
	else if (feature->type == GOOG_CHIP_INFO_MBU_A0_IDS)
		return goog_chip_info_ids_get_decimal_num_mbu_a0(index);

	return -EINVAL;
}

static int goog_chip_info_ids_translator(struct goog_chip_info_data *data,
					 struct goog_chip_info_feature *feature,
					 u64 value, int index, char *buffer, int max_size)
{
	int size;
	int integer, decimal;
	int nr_dec_bits = goog_chip_info_ids_get_decimal_num(feature, index);

	if (nr_dec_bits < 0)
		return 0;

	integer = value >> nr_dec_bits;
	decimal = value & (BIT(nr_dec_bits) - 1);
	decimal = (10000000 * decimal) >> nr_dec_bits;

	size = snprintf(buffer, max_size, "%d.%07d mA", integer, decimal);

	return size;
}

static int goog_chip_info_asic_id_translator(struct goog_chip_info_data *data,
					     struct goog_chip_info_feature *feature,
					     u64 value, int index, char *buffer, int max_size)
{
	const char *chip_name = NULL;
	char version = 0;
	int ret = 0;

	if (feature->type == GOOG_CHIP_INFO_LGA_A0_GPCM_ASIC_ID) {
		if (value == 0x500)
			chip_name = "LGA A0";
		else if (value == 0x510)
			chip_name = "LGA B0";
	} else if (feature->type == GOOG_CHIP_INFO_MBU_A0_GPCM_ASIC_ID) {
		if (index == MBU_A0_GPCM_ASIC_ID_PROJ_ID && value == 0x6) {
			if (data->rto)
				chip_name = "MBU (RTO)";
			else
				chip_name = "MBU";
		} else if (index == MBU_A0_GPCM_ASIC_ID_MAJ_VER) {
			version = 'A' + (int)value;
		} else if (index == MBU_A0_GPCM_ASIC_ID_MIN_VER) {
			version = '0' + (int)value;
		}
	}

	if (chip_name)
		ret = snprintf(buffer, max_size, "%s", chip_name);
	else if (version)
		ret = snprintf(buffer, max_size, "%c", version);

	return ret;
}

/*
 * list of translator
 */
static const struct translator translator_list[] __initconst = {
	{"dvfs-translator", goog_chip_info_dvfs_translator},
	{"ids-translator", goog_chip_info_ids_translator},
	{"asic-id-translator", goog_chip_info_asic_id_translator},
	{},
};
