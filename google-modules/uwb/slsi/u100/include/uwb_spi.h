/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_SPI_H__
#define __UWB_SPI_H__


#include "uwb.h"

#define DEFAULT_SPI_RX_SIZE 1024 /* 1KB */

enum uwb_spi_type {
	SPI_TYPE_FLASH = 1,
	SPI_TYPE_UCI = 2,
};

int uwb_spi_send(struct u100_ctx *u100_ctx, char *buff, unsigned int size, char *recv_buff);
int uwb_spi_recv(struct u100_ctx *u100_ctx, char *buff, unsigned int size);

void uwb_set_spi_type(enum uwb_spi_type spi_type);

void uwb_set_spi_speed(int speed);

int uwb_get_spi_speed(void);

#endif /* __UWB_SPI_H__ */
