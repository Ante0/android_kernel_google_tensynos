// NOLINTBEGIN
#ifndef __NOA_MODEM_FW_RING_H__
#define __NOA_MODEM_FW_RING_H__

#include "ncp_modem_data.h"
#include "sys_common.h"

#define NOA_FW_RING_SIZE 512

#ifndef linux
struct modem_fw_ring_plat_params {
	int (*md_fw_input_ring_activate)(int32_t);
	int (*md_fw_input_ring_deactivate)(int32_t);
	int (*md_fw_output_ring_activate)(int32_t);
	int (*md_fw_output_ring_deactivate)(int32_t);
};
extern int modem_fw_ring_plat_init(struct modem_fw_ring_plat_params *params);
#endif
extern const struct modem_fw_ring_ops *modem_fw_ring_get_tx_ops(void);
extern const struct modem_fw_ring_ops *modem_fw_ring_get_rx_ops(void);

extern int modem_fw_register_nep_interrupt(struct noa_md_fw *md_fw);
extern void modem_fw_free_nep_interrupt(struct noa_md_fw *md_fw);

#endif /* __NOA_MODEM_FW_RING_H__ */
// NOLINTEND
