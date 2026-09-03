/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Google LLC
 */

#ifndef _GOOG_OTP_MAP_H_
#define _GOOG_OTP_MAP_H_

#define LCS_MAJOR_HTEST 0x0000
#define LCS_MAJOR_OPEN 0x0005
#define LCS_MAJOR_DEV 0x00A5
#define LCS_MAJOR_PROD 0x0055

/* this value indicate the max number of fields to be fetched and combined */
#define MAX_REG_INFO 2

struct reg_info {
	u32 offset;
	u8 first_bit;
	u8 last_bit;
	u8 shift;
};

struct goog_chip_info_config {
	const char *name;
	struct reg_info reg_infos[MAX_REG_INFO];
	bool is_big_endian;
};

struct goog_chip_info_descriptor {
	const char *name;
	u64 value;
};

struct goog_chip_info_feature;
struct goog_chip_info_data;

typedef int (*goog_chip_info_translator_cb_t)(struct goog_chip_info_data *data,
					      struct goog_chip_info_feature *feature,
					      u64 value, int index, char *buffer, int max_size);

struct translator {
	char *name;
	goog_chip_info_translator_cb_t callback;
};

struct goog_chip_info_feature {
	struct device *dev;
	void __iomem *base;
	const char *name;
	int type;
	struct goog_chip_info_descriptor *descriptor;
	goog_chip_info_translator_cb_t translator_cb;
	int nr_descriptor_entries;
	struct device_attribute dev_attr;
	bool is_serial_codes;
	bool is_device_table;
	bool is_lcs_state;
	bool is_rto;
	bool is_visible_in_prod;
};

#define MAX_CHIP_INFO_FEATURES 16
#define MAX_SERIAL_CODE_WORDS 4
#define MAX_CHIP_INFO_BASES 4
struct goog_chip_info_data {
	int nr_features;
	struct goog_chip_info_feature features[MAX_CHIP_INFO_FEATURES];
	int nr_priv_features;
	struct goog_chip_info_feature priv_features[MAX_CHIP_INFO_FEATURES];
	void __iomem *bases[MAX_CHIP_INFO_BASES];
	int nr_bases;
	int lcs_state;
	int rto;
	u32 serial_codes[MAX_SERIAL_CODE_WORDS];
};
#endif /* _GOOG_OTP_MAP__H_ */
