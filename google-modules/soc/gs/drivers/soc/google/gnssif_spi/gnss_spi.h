/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022, Samsung Electronics.
 *
 */

#ifndef __GNSS_SPI_H__
#define __GNSS_SPI_H__

#define DEFAULT_SPI_RX_SIZE	64
#define MAX_SPI_RX_SIZE	SZ_2K
#define DEFAULT_SPI_TX_SIZE	SZ_4K
#define SPI_BITS_PER_WORD	32

extern int gnss_spi_send(char *buff, unsigned int size, char *recv_buff);
extern int gnss_spi_recv(char *buff, unsigned int size);

#endif /* __GNSS_SPI_H__ */
