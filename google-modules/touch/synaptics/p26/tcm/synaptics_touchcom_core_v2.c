// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics TouchComm C library
 *
 * Copyright (C) 2017-2024 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file synaptics_touchcom_core_v2.c
 *
 * This file implements the TouchComm version 2 command-response protocol
 */

#include "synaptics_touchcom_core_dev.h"

#define BITS_IN_MESSAGE_HEADER (MESSAGE_HEADER_SIZE * 8)

#define HOST_PRIMARY (0)

#define COMMAND_RETRY_TIMES (5)

#define CHECK_PACKET_CRC

/** Header of TouchComm v2 Message Packet */
struct tcm_v2_message_header {
	union {
		struct {
			unsigned char code;
			unsigned char length[2];
			unsigned char byte3;
		};
		unsigned char data[MESSAGE_HEADER_SIZE];
	};
};

/* Declaration for the processing of v2 command */
static int syna_tcm_v2_execute_cmd_request(struct tcm_dev *tcm_dev,
	unsigned char command, unsigned char *payload,
	unsigned int payload_length);


/**
 * @brief   Terminate the command processing
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *
 * @return
 *    void.
 */
static void syna_tcm_v2_terminate(struct tcm_dev *tcm_dev)
{
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_completion = &tcm_msg->cmd_completion;

	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_BUSY)
		return;

	LOGI("Terminate the processing of command %02X\n\n", tcm_msg->command);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_TERMINATED);
	syna_pal_completion_complete(cmd_completion);
}

/**
 * @brief   Set up the capability of message reading and writing.
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *    [ in] wr_size: the max. size for each write
 *    [ in] rd_size: the max. size for each read
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_set_up_max_rw_size(struct tcm_dev *tcm_dev,
		unsigned int wr_size, unsigned int rd_size)
{
	int retval;
	unsigned char data[2] = { 0 };

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	/* apply the max write size */
	tcm_dev->max_wr_size = wr_size;
	LOGD("Set max write length to %d bytes\n", tcm_dev->max_wr_size);

	/* apply the max read size */
	if (rd_size != tcm_dev->max_rd_size) {
		tcm_dev->max_rd_size = rd_size;

		data[0] = (unsigned char)tcm_dev->max_rd_size;
		data[1] = (unsigned char)(tcm_dev->max_rd_size >> 8);

		retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
				CMD_TCM2_SET_MAX_READ_LENGTH,
				data,
				sizeof(data));
		if (retval < 0) {
			LOGE("Fail to set max read size\n");
			return retval;
		}
	}
	LOGD("Set max read length to %d bytes\n", tcm_dev->max_rd_size);

	return 0;
}
/**
 * @brief   Check the max size of message reading and writing
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_check_max_rw_size(struct tcm_dev *tcm_dev)
{
	unsigned int possible_rd_size = 0;
	unsigned int rd_size = 0;
	unsigned int wr_size = 0;
	struct tcm_identification_info *id_info;
	unsigned int build_id = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	id_info = &tcm_dev->id_info;
	build_id = syna_pal_le4_to_uint(id_info->build_id);

	if (build_id == 0) {
		LOGE("Invalid identify report stored\n");
		return -ERR_INVAL;
	}

	rd_size = syna_pal_le2_to_uint(id_info->max_read_size);
	wr_size = syna_pal_le2_to_uint(id_info->max_write_size);
#ifdef VERSION_2_LEGACY_FW
	possible_rd_size = rd_size;
#else
	possible_rd_size = syna_pal_le2_to_uint(id_info->current_read_size);
#endif

	rd_size = min(rd_size, possible_rd_size);

	if ((wr_size == 0) || (rd_size == 0)) {
		LOGE("Invalid max read:%d or write:%d size\n",
			rd_size, wr_size);
		return -ERR_INVAL;
	}

	/* check the max write size between the identify report and the platform's settings */
	if (wr_size != tcm_dev->max_wr_size) {
		if (tcm_dev->max_wr_size == 0)
			wr_size = tcm_dev->max_wr_size;
		else
			wr_size = min(wr_size, tcm_dev->max_wr_size);
	}

	/* check the max read size between the identify report and the platform's settings */
	if (rd_size != tcm_dev->max_rd_size) {
		if (tcm_dev->max_rd_size != 0)
			rd_size = min(rd_size, tcm_dev->max_rd_size);
	}

	return syna_tcm_v2_set_up_max_rw_size(tcm_dev, wr_size, rd_size);
}

/**
 * @brief   Parse the identification info packet and get the essential info
 *
 * @param
 *    [ in] tcm_dev:  the TouchComm device handle
 *    [ in] data:     data buffer
 *    [ in] size:     size of given data buffer
 *    [ in] data_len: length of actual data
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_parse_idinfo(struct tcm_dev *tcm_dev,
	unsigned char *data, unsigned int size, unsigned int data_len)
{
	int retval;
	unsigned int build_id = 0;
	struct tcm_identification_info *id_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!data) || (data_len == 0)) {
		LOGE("Invalid given data buffer\n");
		return -ERR_INVAL;
	}

	id_info = &tcm_dev->id_info;

	retval = syna_pal_mem_cpy((unsigned char *)id_info,
			sizeof(struct tcm_identification_info),
			data,
			size,
			min(sizeof(*id_info), data_len));
	if (retval < 0) {
		LOGE("Fail to copy identification info\n");
		return retval;
	}

	build_id = syna_pal_le4_to_uint(id_info->build_id);

	if (tcm_dev->packrat_number != build_id)
		tcm_dev->packrat_number = build_id;

	LOGI("TCM Fw mode: 0x%02x\n", id_info->mode);

	tcm_dev->dev_mode = id_info->mode;

	return 0;
}

/**
 * @brief   Handle the TouchCom report read in by the read_message(),
 *          and copy the data from internal buffer.in to internal buffer.report.
 *
 *          In addition, invoke the corresponding callback function if registered.
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *
 * @return
 *    void.
 */
static void syna_tcm_v2_dispatch_report(struct tcm_dev *tcm_dev)
{
	int retval;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_completion_t *cmd_completion = NULL;
	unsigned char report_code;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_completion = &tcm_msg->cmd_completion;

	report_code = tcm_msg->status_report_code;

	if (tcm_msg->payload_length == 0) {
		tcm_dev->report_buf.data_length = 0;
		goto exit;
	}

	/* store the received report into the internal buffer.report */
	syna_tcm_buf_lock(&tcm_dev->report_buf);

	retval = syna_tcm_buf_alloc(&tcm_dev->report_buf,
			tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.report\n");
		syna_tcm_buf_unlock(&tcm_dev->report_buf);
		goto exit;
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_pal_mem_cpy(tcm_dev->report_buf.buf,
			tcm_dev->report_buf.buf_size,
			&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
			tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
			tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to copy payload to buf_report\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_tcm_buf_unlock(&tcm_dev->report_buf);
		goto exit;
	}

	tcm_dev->report_buf.data_length = tcm_msg->payload_length;

	syna_tcm_buf_unlock(&tcm_msg->in);
	syna_tcm_buf_unlock(&tcm_dev->report_buf);

	if (report_code == REPORT_IDENTIFY) {
		/* parse the identify report */
		syna_tcm_buf_lock(&tcm_msg->in);
		retval = syna_tcm_v2_parse_idinfo(tcm_dev,
				&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
				tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
				tcm_msg->payload_length);
		if (retval < 0) {
			LOGE("Fail to parse identification data\n");
			syna_tcm_buf_unlock(&tcm_msg->in);
			return;
		}
		syna_tcm_buf_unlock(&tcm_msg->in);

		/* in case, the identify info packet is resulted from the command */
		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_BUSY) {
			switch (tcm_msg->command) {
			case CMD_TCM2_GET_REPORT:
				ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
				syna_pal_completion_complete(cmd_completion);
				goto exit;
			case CMD_RESET:
				LOGD("Reset by command 0x%02X\n", tcm_msg->command);
				ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
				syna_pal_completion_complete(cmd_completion);
				goto exit;
			case CMD_REBOOT_TO_DISPLAY_ROM_BOOTLOADER:
			case CMD_REBOOT_TO_ROM_BOOTLOADER:
			case CMD_RUN_BOOTLOADER_FIRMWARE:
			case CMD_RUN_APPLICATION_FIRMWARE:
			case CMD_ENTER_PRODUCTION_TEST_MODE:
			case CMD_ROMBOOT_RUN_BOOTLOADER_FIRMWARE:
				ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
				syna_pal_completion_complete(cmd_completion);
				goto exit;
			default:
				if (tcm_dev->testing_purpose) {
					ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
					syna_pal_completion_complete(cmd_completion);
				} else {
					LOGI("Unexpected 0x%02X report received\n", REPORT_IDENTIFY);
					ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
					syna_pal_completion_complete(cmd_completion);
				}
				break;
			}
		}
	}

	if (tcm_msg->command == CMD_TCM2_GET_REPORT) {
		LOGD("Report %2X received\n", report_code);
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
		syna_pal_completion_complete(cmd_completion);
	}

	/* handle the report by callbacks */
	if (tcm_dev->cb_report_dispatcher[report_code].cb) {
		syna_tcm_buf_lock(&tcm_dev->report_buf);
		tcm_dev->cb_report_dispatcher[report_code].cb(
				report_code,
				tcm_dev->report_buf.buf,
				tcm_dev->report_buf.data_length,
				tcm_dev->cb_report_dispatcher[report_code].private_data);
		syna_tcm_buf_unlock(&tcm_dev->report_buf);
	}

exit:
	return;
}

/**
 * @brief   Handle the response packet read in by the read_message(),
 *          and copy the data from internal buffer.in to internal buffer.resp.
 *
 *          Complete the completion event at the end of function.
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *
 * @return
 *    void.
 */
static void syna_tcm_v2_dispatch_response(struct tcm_dev *tcm_dev)
{
	int retval;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_completion = &tcm_msg->cmd_completion;

	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_BUSY)
		return;

	if (tcm_msg->status_report_code == STATUS_NO_REPORT_AVAILABLE) {
		tcm_dev->resp_buf.data_length = 0;
		goto exit;
	}

	tcm_msg->response_code = tcm_msg->status_report_code;

	if (tcm_msg->response_code == STATUS_ACK)
		return;

	if (tcm_msg->payload_length == 0) {
		tcm_dev->resp_buf.data_length = 0;
		goto exit;
	}

	/* store the received report into the temporary buffer */
	syna_tcm_buf_lock(&tcm_dev->resp_buf);

	retval = syna_tcm_buf_alloc(&tcm_dev->resp_buf,
		tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.resp\n");
		syna_tcm_buf_unlock(&tcm_dev->resp_buf);
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
		goto exit;
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_pal_mem_cpy(tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
			tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
			tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to copy payload to internal resp_buf\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_tcm_buf_unlock(&tcm_dev->resp_buf);
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
		goto exit;
	}

	tcm_dev->resp_buf.data_length = tcm_msg->payload_length;

	syna_tcm_buf_unlock(&tcm_msg->in);

	if (tcm_msg->command == CMD_IDENTIFY) {
		retval = syna_tcm_v2_parse_idinfo(tcm_dev,
			tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			tcm_dev->resp_buf.data_length);
		if (retval < 0) {
			LOGE("Fail to parse identify packet from resp_buf\n");
			syna_tcm_buf_unlock(&tcm_dev->resp_buf);
			goto exit;
		}
	}

	syna_tcm_buf_unlock(&tcm_dev->resp_buf);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);

exit:
	syna_pal_completion_complete(cmd_completion);
}

/**
 * @brief   Read in a TouchCom packet from device.
 *          Meanwhile, check the CRC to ensure a valid message.
 *
 * @param
 *    [ in] tcm_dev:    the TouchComm device handle
 *    [ in] rd_length:  number of reading bytes;
 *                     '0' means to read the message header only
 *    [out] buf:        pointer to a buffer which is stored the retrieved data
 *    [out] buf_size:   size of the buffer pointed
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_read(struct tcm_dev *tcm_dev, unsigned int rd_length,
	unsigned char **buf, unsigned int *buf_size)
{
	int retval;
	struct tcm_v2_message_header *header;
	int max_rd_size;
	int xfer_len;
#ifndef VERSION_2_LEGACY_FW
	unsigned char seq;
#endif
	unsigned char crc6 = 0;
	unsigned short crc16 = 0xFFFF;
	struct tcm_message_data_blob *tcm_msg = NULL;
#ifdef CHECK_PACKET_CRC
	unsigned int header_len;
#endif

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	max_rd_size = tcm_dev->max_rd_size;

	/* continued packet crc if containing payload data */
	if (rd_length > 0)
		xfer_len = rd_length + TCM_MSG_CRC_LENGTH;
	else
		xfer_len = rd_length;
	xfer_len += sizeof(struct tcm_v2_message_header);

	if ((max_rd_size != 0) && (xfer_len > max_rd_size)) {
		LOGE("Invalid xfer length, len: %d, max_rd_size: %d\n",
			xfer_len, max_rd_size);
		tcm_msg->status_report_code = STATUS_INVALID;
		return -ERR_INVAL;
	}

	syna_tcm_buf_lock(&tcm_msg->temp);

	/* allocate the internal temp buffer */
	retval = syna_tcm_buf_alloc(&tcm_msg->temp, xfer_len);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.temp\n");
		goto exit;
	}
	/* read data from the bus */
	retval = syna_tcm_read(tcm_dev,
			tcm_msg->temp.buf,
			xfer_len);
	if (retval < 0) {
		LOGE("Fail to read from device\n");
		goto exit;
	}

	header = (struct tcm_v2_message_header *)tcm_msg->temp.buf;

	/* check header crc always */
	crc6 = syna_tcm_crc6(header->data, BITS_IN_MESSAGE_HEADER);
	if (crc6 != 0) {
		LOGE("Invalid header crc: 0x%02x\n", (header->byte3 & 0x3f));
		tcm_msg->status_report_code = STATUS_PACKET_CORRUPTED;
		retval = 0;
		goto exit;
	}
#ifndef VERSION_2_LEGACY_FW
	seq = (tcm_msg->seq_toggle & 0x01);
	if ((((header->byte3) >> 6) & 0x01) != seq) {
		LOGE("Mismatched sequence number, expected:%d\n", seq);
		tcm_msg->status_report_code = STATUS_PACKET_CORRUPTED;
		retval = 0;
		goto exit;
	}
#endif

#ifdef CHECK_PACKET_CRC
	/* check payload crc */
	header_len = syna_pal_le2_to_uint(header->length);
	if ((rd_length > 0) && (header_len > 0)) {
		tcm_msg->crc_bytes = (unsigned short)syna_pal_le2_to_uint(
			&tcm_msg->temp.buf[xfer_len - 2]);
		crc16 = syna_tcm_crc16(&tcm_msg->temp.buf[0],
				xfer_len, 0xFFFF);
		if (crc16 != 0) {
			LOGE("Invalid payload crc: %04X\n",
				tcm_msg->crc_bytes);

			tcm_msg->status_report_code =
				STATUS_PACKET_CORRUPTED;
			goto exit;
		}
	}
#endif

	tcm_msg->status_report_code = header->code;

	tcm_msg->payload_length = syna_pal_le2_to_uint(header->length);

	if (tcm_msg->status_report_code != STATUS_IDLE)
		LOGD("Status code: 0x%02x, length: %d (%02x %02x %02x %02x)\n",
			tcm_msg->status_report_code, tcm_msg->payload_length,
			header->data[0], header->data[1], header->data[2],
			header->data[3]);

	*buf = tcm_msg->temp.buf;
	*buf_size = tcm_msg->temp.buf_size;

exit:
	syna_tcm_buf_unlock(&tcm_msg->temp);

	return retval;
}

/**
 * @brief   Assemble the TouchCom v2 packet and send the packet to device.
 *
 * @param
 *    [ in] tcm_dev:     the TouchComm device handle
 *    [ in] command:     command code
 *    [ in] payload:     data payload if any
 *    [ in] payload_len: length of data payload if have any
 *    [ in] message_len: length of total message, which should be equal to or large than the payload length
 *    [ in] resend:      flag for re-sending the packet
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_write(struct tcm_dev *tcm_dev, unsigned char command,
	unsigned char *payload, unsigned int payload_len,
	unsigned int message_len, bool resend)
{
	int retval;
	struct tcm_v2_message_header *header;
	unsigned char bits = BITS_IN_MESSAGE_HEADER - 6;
	int xfer_len;
	unsigned short crc16;
	struct tcm_message_data_blob *tcm_msg = NULL;
	int offset;
	bool do_predict = false;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;

	if (!tcm_msg) {
		LOGE("Invalid tcm message blob\n");
		return -ERR_INVAL;
	}

	/* predict reading is applied only when getting the report */
	do_predict = (command == CMD_TCM2_GET_REPORT);

	if (payload_len > 0)
		xfer_len = payload_len + TCM_MSG_CRC_LENGTH;
	else
		xfer_len = payload_len;

	xfer_len += sizeof(struct tcm_v2_message_header);

	syna_tcm_buf_lock(&tcm_msg->out);

	/* allocate the internal out buffer */
	retval = syna_tcm_buf_alloc(&tcm_msg->out, xfer_len);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.out\n");
		goto exit;
	}

	/* construct packet header */
	header = (struct tcm_v2_message_header *)tcm_msg->out.buf;

	if (!resend)
		tcm_msg->seq_toggle++;

	header->code = command;
	header->length[0] = (unsigned char)message_len;
	header->length[1] = (unsigned char)(message_len >> 8);
	header->byte3 = ((HOST_PRIMARY & 0x01) << 7);
	header->byte3 |= ((tcm_msg->seq_toggle & 0x01) << 6);
	header->byte3 |= syna_tcm_crc6(header->data, bits);

	if (message_len != payload_len) {
		LOGD("Command packet: %02x %02x %02x %02x, length in header:%d, actual payload size:%d\n",
			header->data[0], header->data[1], header->data[2], header->data[3],
			message_len, payload_len);
	} else {
		LOGD("Command packet: %02x %02x %02x %02x, length:%d\n",
			header->data[0], header->data[1], header->data[2], header->data[3], message_len);
	}

	/* copy payload, if any */
	if (payload_len > 0) {
		retval = syna_pal_mem_cpy(
				&tcm_msg->out.buf[MESSAGE_HEADER_SIZE],
				tcm_msg->out.buf_size - MESSAGE_HEADER_SIZE,
				payload,
				payload_len,
				payload_len);
		if (retval < 0) {
			LOGE("Fail to copy payload data\n");
			goto exit;
		}

		/* append payload crc */
		offset = MESSAGE_HEADER_SIZE + payload_len;
		crc16 = syna_tcm_crc16(&tcm_msg->out.buf[0], offset, 0xFFFF);
		tcm_msg->out.buf[offset] = (unsigned char)((crc16 >> 8) & 0xFF);
		tcm_msg->out.buf[offset + 1] = (unsigned char)(crc16 & 0xFF);
	}

	/* write command packet to the bus */
	retval = syna_tcm_write(tcm_dev,
			tcm_msg->out.buf,
			xfer_len);
	if (retval < 0) {
		LOGE("Fail to write to device\n");
		goto exit;
	}

	/* update the length for predict reading */
	if ((tcm_msg->predict_reads) && do_predict) {
		tcm_msg->predict_length = min(tcm_msg->payload_length,
			tcm_dev->max_rd_size - MESSAGE_HEADER_SIZE - 2);
	} else {
		tcm_msg->predict_length = 0;
	}

exit:
	syna_tcm_buf_unlock(&tcm_msg->out);

	return retval;
}
/**
 * @brief   Continuously read in the remaining data payload until the end of data.
 *
 * @param
 *    [ in] tcm_dev:  the TouchComm device handle
 *    [ in] length:   remaining data length in bytes
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_continued_read(struct tcm_dev *tcm_dev,
	unsigned int length)
{
	int retval;
	unsigned char *tmp_buf;
	unsigned int tmp_buf_size;
	int retry_cnt = 0;
	unsigned int idx;
	unsigned int offset;
	unsigned int chunks;
	unsigned int chunk_space;
	unsigned int xfer_length;
	unsigned int total_length;
	unsigned int remaining_length;
	unsigned char command;
	struct tcm_message_data_blob *tcm_msg = NULL;
	struct tcm_v2_message_header *header;
	bool need_ack_cmd;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;

	/* continued read packet contains the header and its payload */
	total_length = MESSAGE_HEADER_SIZE + tcm_msg->payload_length;

	remaining_length = length;

	offset = tcm_msg->payload_length - length;

	syna_tcm_buf_lock(&tcm_msg->in);

	/* extend the internal buf_in if needed */
	retval = syna_tcm_buf_realloc(&tcm_msg->in,
			total_length + 1);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf_in\n");
		goto exit;
	}

	/* available space for payload = total chunk size - header - crc */
	chunk_space = tcm_dev->max_rd_size;
	if (chunk_space == 0)
		chunk_space = remaining_length;
	else
		chunk_space -= (MESSAGE_HEADER_SIZE + TCM_MSG_CRC_LENGTH);

	chunks = syna_pal_ceil_div(remaining_length, chunk_space);
	chunks = chunks == 0 ? 1 : chunks;

	offset += MESSAGE_HEADER_SIZE;

	/* send CMD_ACK for a continued read */
	command = CMD_TCM2_ACK;

	for (idx = 0; idx < chunks; idx++) {
retry:
#ifdef VERSION_2_LEGACY_FW
		need_ack_cmd = true;
#else
		need_ack_cmd = ((idx > 0) || (offset > MESSAGE_HEADER_SIZE));
#endif
		if (need_ack_cmd || (retry_cnt > 0)) {
			retval = syna_tcm_v2_write(tcm_dev,
					command,
					NULL,
					0,
					0,
					(retry_cnt > 0));
			if (retval < 0) {
				LOGE("Fail to send ACK in continued read\n");
				goto exit;
			}
		}

		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		/* read in the requested size of data */
		retval = syna_tcm_v2_read(tcm_dev,
				xfer_length,
				&tmp_buf,
				&tmp_buf_size);
		if (retval < 0) {
			LOGE("Fail to read %d bytes from device\n",
					xfer_length);
			goto exit;
		}

		/* If see an error, retry the previous read transaction
		 * Send RETRY instead of CMD_ACK
		 */
		if (tcm_msg->status_report_code == STATUS_PACKET_CORRUPTED) {
			if (retry_cnt > COMMAND_RETRY_TIMES) {
				LOGE("Continued read packet corrupted\n");
				goto exit;
			}

			retry_cnt += 1;

			LOGW("Read corrupted, retry %d\n", retry_cnt);
			goto retry;
		}

		retry_cnt = 0;
		command = CMD_TCM2_ACK;

		/* append data from temporary buffer to in_buf */
		syna_tcm_buf_lock(&tcm_msg->temp);

		/* copy data from internal buffer.temp to buffer.in */
		retval = syna_pal_mem_cpy(&tcm_msg->in.buf[offset],
				tcm_msg->in.buf_size - offset,
				&tmp_buf[MESSAGE_HEADER_SIZE],
				tmp_buf_size - MESSAGE_HEADER_SIZE,
				xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy payload to internal buf_in\n");
			syna_tcm_buf_unlock(&tcm_msg->temp);
			goto exit;
		}

		syna_tcm_buf_unlock(&tcm_msg->temp);

		remaining_length -= xfer_length;

		offset += xfer_length;
	}

	header = (struct tcm_v2_message_header *)tcm_msg->in.buf;
	tcm_msg->payload_length = syna_pal_le2_to_uint(header->length);

	tcm_msg->in.data_length = offset;
	retval = 0;

exit:
	syna_tcm_buf_unlock(&tcm_msg->in);

	return retval;
}

/**
 * @brief   Read in the immediate response packet from device.
 *          If containing payload data, call continued_read() to retrieve
 *          the whole packet.
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_get_immediate_response(struct tcm_dev *tcm_dev)
{
	int retval;
	struct tcm_v2_message_header *header;
	unsigned char *tmp_buf;
	unsigned int tmp_buf_size;
	struct tcm_message_data_blob *tcm_msg = NULL;
	unsigned int len = 0;
	unsigned int remaining = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;

	/* bus turnaround delay */
	syna_pal_sleep_us(tcm_msg->turnaround_time[0], tcm_msg->turnaround_time[1]);

	/* if predict read enabled, plus the predicted length
	 * and determine the length to read
	 */
	if ((tcm_msg->predict_reads) && (tcm_msg->predict_length > 0))
		len += tcm_msg->predict_length;

	/* read in the message header at first */
	retval = syna_tcm_v2_read(tcm_dev,
			len,
			&tmp_buf,
			&tmp_buf_size);
	if (retval < 0) {
		LOGE("Fail to read message header from device\n");
		return retval;
	}

	/* error out once the response packet is corrupted */
	if (tcm_msg->status_report_code == STATUS_PACKET_CORRUPTED)
		return 0;

	/* allocate the required space = header + payload */
	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_tcm_buf_alloc(&tcm_msg->in,
			MESSAGE_HEADER_SIZE + tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to reallocate memory for internal buf.in\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		return retval;
	}

	retval = syna_pal_mem_cpy(tcm_msg->in.buf,
			tcm_msg->in.buf_size,
			tmp_buf,
			tmp_buf_size,
			len + MESSAGE_HEADER_SIZE);
	if (retval < 0) {
		LOGE("Fail to copy data to internal buf_in\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		return retval;
	}

	syna_tcm_buf_unlock(&tcm_msg->in);

	/* read in payload, if any */
	remaining = tcm_msg->payload_length - len;
	if ((tcm_msg->payload_length) && (remaining > 0)) {
		retval = syna_tcm_v2_continued_read(tcm_dev, remaining);
		if (retval < 0) {
			LOGE("Fail to read in payload data, size: %d)\n",
				tcm_msg->payload_length);
			return retval;
		}
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	header = (struct tcm_v2_message_header *)tcm_msg->in.buf;

	tcm_msg->payload_length = syna_pal_le2_to_uint(header->length);
	tcm_msg->status_report_code = header->code;

	syna_tcm_buf_unlock(&tcm_msg->in);

	return retval;
}

/**
 * @brief   Process the message and write the message to device.
 *          In addition, the response to the command will be read in immediately.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] command:        command code
 *    [ in] payload:        data payload if any
 *    [ in] payload_length: length of payload in bytes
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_execute_cmd_request(struct tcm_dev *tcm_dev,
	unsigned char command, unsigned char *payload,
	unsigned int payload_length)
{
	int retval;
	unsigned int idx;
	unsigned int offset;
	unsigned int chunks;
	unsigned int xfer_length;
	unsigned int remaining_length;
	int retry_cnt = 0;
	unsigned int chunk_space;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	chunk_space = tcm_dev->max_wr_size;

	remaining_length = payload_length;

	/* available space for payload = total size - header - crc */
	if (chunk_space == 0)
		chunk_space = remaining_length;
	else
		chunk_space = chunk_space - (MESSAGE_HEADER_SIZE + 2);

	chunks = syna_pal_ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = 0;

	/* process the command message and handle the response
	 * to the command
	 */
	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;
retry:
		/* send command to device */
		command = (idx == 0) ? command : CMD_CONTINUE_WRITE;
		retval = syna_tcm_v2_write(tcm_dev,
				command,
				&payload[offset],
				xfer_length,
				remaining_length,
				(retry_cnt > 0));
		if (retval < 0) {
			LOGE("Fail to send command 0x%02x\n", command);
			goto exit;
		}

		/* get the response to the command immediately */
		retval = syna_tcm_v2_get_immediate_response(tcm_dev);
		if (retval < 0) {
			LOGE("Fail to get the response to command 0x%02x\n",
				command);
			goto exit;
		}

		/* check the response code */
		switch (tcm_msg->status_report_code) {
		case STATUS_NO_REPORT_AVAILABLE:
		case STATUS_OK:
		case STATUS_ACK:
			retry_cnt = 0;
			break;
		case STATUS_PACKET_CORRUPTED:
		case STATUS_RETRY_REQUESTED:
			retry_cnt += 1;
			break;
		default:
			/* go to next if returned status belongs to a report */
			if (tcm_msg->status_report_code >= REPORT_IDENTIFY)
				goto next;
			/* otherwise, unknown error */
			LOGE("Incorrect status code 0x%02x of command 0x%02x or 0x%02x\n",
				tcm_msg->status_report_code, command, tcm_msg->command);
			goto exit;
		}

		if (retry_cnt > 0) {
			if (command == CMD_RESET) {
				LOGE("Command CMD_RESET corrupted, exit\n");
				/* assume ACK and wait for interrupt assertion */
				tcm_msg->status_report_code = STATUS_ACK;
				goto exit;
			} else if (retry_cnt > COMMAND_RETRY_TIMES) {
				LOGE("Fal to run command 0x%02x due to message corrupted\n", command);
				retval = -ERR_TCMMSG;
				goto exit;
			}

			LOGN("Message corrupted at command 0x%02x, retry %d\n", command, retry_cnt);
			syna_pal_sleep_us(tcm_msg->retry_time[0], tcm_msg->retry_time[1]);

			goto retry;
		}
next:
		offset += xfer_length;

		remaining_length -= xfer_length;

		if (chunks > 1)
			syna_pal_sleep_us(tcm_msg->turnaround_time[0], tcm_msg->turnaround_time[1]);
	}

	/* process the retrieved packet */
	if (tcm_msg->status_report_code >= REPORT_IDENTIFY)
		syna_tcm_v2_dispatch_report(tcm_dev);
	else
		syna_tcm_v2_dispatch_response(tcm_dev);

exit:
	return retval;
}

/**
 * @brief   Acquire a TouchComm v2 packet from device.
 *
 * @param
 *    [ in] tcm_dev:            the TouchComm device handle
 *    [out] status_report_code: status code or report code received
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_read_message(struct tcm_dev *tcm_dev,
	unsigned char *status_report_code)
{
	int retval;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_mutex_t *rw_mutex = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	rw_mutex = &tcm_msg->rw_mutex;
	cmd_completion = &tcm_msg->cmd_completion;

	if (status_report_code)
		*status_report_code = STATUS_INVALID;

	tcm_msg->status_report_code = STATUS_IDLE;

	tcm_msg->crc_bytes = 0;

	syna_pal_mutex_lock(rw_mutex);

	/* request a command */
	retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
			CMD_TCM2_GET_REPORT,
			NULL,
			0);
	if (retval < 0) {
		LOGE("Fail to send command CMD_TCM2_GET_REPORT\n");

		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_BUSY) {
			ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
			syna_pal_completion_complete(cmd_completion);
		}
		goto exit;
	}

	/* duplicate the data to the external buffer */
	if (tcm_dev->cb_data_duplicator[tcm_msg->status_report_code].cb) {
		syna_tcm_buf_lock(&tcm_msg->in);
		tcm_dev->cb_data_duplicator[tcm_msg->status_report_code].cb(
			tcm_msg->status_report_code,
			&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
			tcm_msg->payload_length,
			tcm_dev->cb_data_duplicator[tcm_msg->status_report_code].private_data);
		syna_tcm_buf_unlock(&tcm_msg->in);
	}

	if (tcm_msg->status_report_code == STATUS_NO_REPORT_AVAILABLE)
		goto exit;

	/* process the retrieved packet */
	if (tcm_msg->status_report_code >= REPORT_IDENTIFY)
		syna_tcm_v2_dispatch_report(tcm_dev);
	else
		syna_tcm_v2_dispatch_response(tcm_dev);

	/* copy the status report code to caller */
	if (status_report_code)
		*status_report_code = tcm_msg->status_report_code;

exit:
	syna_pal_mutex_unlock(rw_mutex);

	return retval;
}
/**
 * @brief   A block function to wait for the ATTN assertion.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [ in] timeout:       timeout time waiting for the assertion
 * @return
 *    void.
 */
static void syna_tcm_v2_wait_for_attn(struct tcm_dev *tcm_dev, int timeout)
{
	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	/* if set, invoke the custom function to wait for the ATTN assertion;
	 * otherwise, wait for the completion event.
	 */
	if (tcm_dev->hw->ops_wait_for_attn)
		tcm_dev->hw->ops_wait_for_attn(tcm_dev->hw, timeout);
	else
		syna_pal_completion_wait_for(&tcm_dev->msg_data.cmd_completion, timeout);
}
/**
 * @brief   The entry of the command processing.
 *          Send a command and payload to the device.
 *          After that, the response of the command will also be read in
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] command:        TouchComm command
 *    [ in] payload:        data payload, if any
 *    [ in] payload_length: length of data payload, if any
 *    [out] resp_code:      response code returned
 *    [ in] resp_reading:   method to read in the response
 *                          a positive value presents the ms time delay for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_v2_write_message(struct tcm_dev *tcm_dev,
	unsigned char command, unsigned char *payload,
	unsigned int payload_length, unsigned char *resp_code,
	unsigned int resp_reading)
{
	int retval;
	unsigned int timeout = 0;
	int wait_ms = 0;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_mutex_t *cmd_mutex = NULL;
	syna_pal_mutex_t *rw_mutex = NULL;
	syna_pal_completion_t *cmd_completion = NULL;
	bool in_polling = false;
	bool irq_disabled = false;
	unsigned int cmd_response_timeout;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_mutex = &tcm_msg->cmd_mutex;
	rw_mutex = &tcm_msg->rw_mutex;
	cmd_completion = &tcm_msg->cmd_completion;

	if (resp_code)
		*resp_code = STATUS_INVALID;

	/* indicate which mode is used */
	in_polling = (resp_reading != CMD_RESPONSE_IN_ATTN);

	syna_pal_mutex_lock(cmd_mutex);

	syna_pal_mutex_lock(rw_mutex);

	ATOMIC_SET(tcm_dev->command_processing, 1);
	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_BUSY);

	/* reset the command completion */
	syna_pal_completion_reset(cmd_completion);

	tcm_msg->command = command;

	LOGD("Command: 0x%02x, payload size: %d\n", command, payload_length);

	/* disable irq in case of polling mode */
	if (in_polling)
		irq_disabled = (syna_tcm_enable_irq(tcm_dev, false) > 0);

	/* request a command execution */
	retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
			command,
			payload,
			payload_length);
	if (retval < 0) {
		LOGE("Fail to send command 0x%02x to device\n", command);
		syna_pal_mutex_unlock(rw_mutex);
		goto exit;
	}

	syna_pal_mutex_unlock(rw_mutex);

	/* process the command response either in polling or by ATTN */
	timeout = 0;
	wait_ms = (!in_polling) ? tcm_msg->command_timeout_time : resp_reading;
	cmd_response_timeout = tcm_msg->command_timeout_time;
	do {
		timeout += wait_ms;

		if (in_polling)
			syna_pal_sleep_ms(wait_ms);
		else
			syna_tcm_v2_wait_for_attn(tcm_dev, wait_ms);

		/* stop the processing if terminated */
		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_TERMINATED) {
			retval = 0;
			goto exit;
		}

		/* read in a message if the processing is not completed */
		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_BUSY) {
			retval = syna_tcm_v2_read_message(tcm_dev, NULL);
			if (retval < 0)
				continue;
		}

		/* break the loop once the response to command was ready */
		if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_BUSY)
			break;

	} while (timeout < tcm_msg->command_timeout_time);


	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_IDLE) {
		if (timeout >= cmd_response_timeout) {
			LOGE("Timed out wait for response of command 0x%02x (%dms)\n",
				command, timeout);
			retval = -ERR_TIMEDOUT;
		} else {
			LOGE("Fail to get valid response 0x%02x of command 0x%02x\n",
				tcm_msg->status_report_code, command);
			retval = -ERR_TCMMSG;
		}
		goto exit;
	}

	retval = 0;

exit:
	if (resp_code)
		*resp_code = tcm_msg->response_code;

	tcm_msg->command = CMD_NONE;

	/* recovery the irq only when running in polling mode
	 * and irq has been disabled previously
	 */
	if (in_polling && irq_disabled)
		syna_tcm_enable_irq(tcm_dev, true);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
	ATOMIC_SET(tcm_dev->command_processing, 0);

	syna_pal_mutex_unlock(cmd_mutex);

	return retval;
}
/**
 * @brief   Process the startup packet of TouchComm V2 firmware
 *
 *          For TouchComm v2 protocol, each packet must have a valid crc-6.
 *          If so, send an identify command to identify the device and complete
 *          the pre-initialization.
 *
 * @param
 *    [ in] tcm_dev: the TouchComm device handle
 *    [ in] bypass:  flag to bypass the detection
 *    [ in] do_reset: flag to issue a reset if falling down to error
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_v2_detect(struct tcm_dev *tcm_dev, bool bypass, bool do_reset)
{
	int retval;
	unsigned char *data;
	unsigned int info_size = (unsigned int)sizeof(struct tcm_identification_info);
	struct tcm_message_data_blob *tcm_msg = NULL;
	unsigned char resp_code = 0;
	unsigned short default_rc = 0x5a;
	unsigned char command;
#ifdef VERSION_2_LEGACY_FW
	unsigned int bytes_to_read;
	unsigned char temp[4] = { 0 };
#endif

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (bypass)
		goto set_ops;

	tcm_msg = &tcm_dev->msg_data;

	syna_pal_mutex_lock(&tcm_msg->rw_mutex);
	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_tcm_buf_alloc(&tcm_msg->in, info_size + MESSAGE_HEADER_SIZE);
	if (retval < 0) {
		LOGE("Fail to allocate memory for buf_in\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_pal_mutex_unlock(&tcm_msg->rw_mutex);
		return retval;
	}

	data = tcm_msg->in.buf;
#ifdef VERSION_2_LEGACY_FW
	bytes_to_read = MESSAGE_HEADER_SIZE;
	temp[0] = 0x07;
	retval = syna_tcm_write(tcm_dev, &temp[0], 1);
	if (retval < 0) {
		LOGE("Fail to write data directly\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_pal_mutex_unlock(&tcm_msg->rw_mutex);
		return retval;
	}

	retval = syna_tcm_read(tcm_dev, data, MESSAGE_HEADER_SIZE);
	if (retval < 0) {
		LOGE("Fail to retrieve start-up data from bus\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_pal_mutex_unlock(&tcm_msg->rw_mutex);
		return retval;
	}
	LOGD("start-up data %02x %02x %02x %02x ... (read %d bytes)\n",
		data[0], data[1], data[2], data[3], bytes_to_read);

	if (syna_tcm_crc6(data, BITS_IN_MESSAGE_HEADER) != 0) {
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_pal_mutex_unlock(&tcm_msg->rw_mutex);
		return -ERR_NODEV;
	}

#endif

	syna_tcm_buf_unlock(&tcm_msg->in);
	syna_pal_mutex_unlock(&tcm_msg->rw_mutex);

	/* the identify report should be the first packet at startup
	 * otherwise, send the command to identify
	 */
	retval = syna_tcm_v2_read_message(tcm_dev, &resp_code);
	if ((retval < 0) || (resp_code != REPORT_IDENTIFY)) {
		command = (do_reset) ? CMD_RESET : CMD_IDENTIFY;
		retval = syna_tcm_v2_write_message(tcm_dev, command,
				NULL, 0, &resp_code, tcm_msg->command_polling_time);
		if ((retval < 0) || (resp_code != REPORT_IDENTIFY)) {
			LOGE("Fail to identify at startup\n");
			return -ERR_TCMMSG;
		}
	}

	/* parse the identify info packet if needed */
	if (tcm_dev->dev_mode == MODE_UNKNOWN) {
		syna_tcm_buf_lock(&tcm_msg->in);
		retval = syna_tcm_v2_parse_idinfo(tcm_dev,
				(unsigned char *)&data[MESSAGE_HEADER_SIZE],
				tcm_msg->in.buf_size, info_size);
		syna_tcm_buf_unlock(&tcm_msg->in);
		if (retval < 0) {
			LOGE("Fail to parse identify report at startup\n");
			return -ERR_TCMMSG;
		}
	}

	/* set up the max. reading length at startup */
	retval = syna_tcm_v2_check_max_rw_size(tcm_dev);
	if (retval < 0) {
		LOGE("Fail to setup the max length to read/write\n");
		return -ERR_TCMMSG;
	}

	/* tcm v2 always has crc appended */
	tcm_dev->msg_data.has_crc = true;
	/* tcm v2 not support extra rc appending so far */
	tcm_dev->msg_data.has_extra_rc = false;
	tcm_dev->msg_data.rc_byte = (unsigned char)default_rc;

	LOGI("TouchCom v2 detected\n");
	LOGI("Support of message CRC(%s) and extra RC(%s)\n",
			(tcm_msg->has_crc) ? "yes" : "no",
			(tcm_msg->has_extra_rc) ? "yes" : "no");

set_ops:
	tcm_dev->read_message = syna_tcm_v2_read_message;
	tcm_dev->write_message = syna_tcm_v2_write_message;
	tcm_dev->set_max_rw_size = syna_tcm_v2_set_up_max_rw_size;
	tcm_dev->terminate = syna_tcm_v2_terminate;

	tcm_dev->msg_data.predict_length = 0;
	tcm_dev->protocol = TOUCHCOMM_V2;

	return 0;
}
