#ifndef SI_CODEX_CSR__SSWRP_GPU__GLUE_MBA_HOST_CSR_H_
#define SI_CODEX_CSR__SSWRP_GPU__GLUE_MBA_HOST_CSR_H_

// Header generated from:
// https://partnerdash.google.com/apps/siliconcodex/codex?a=90891426&projecttype=ENTRY_TYPE_SOC&project=rdo&label=RDO_A0_M5_V5_R3&entry=rdo:RDO_A0_M5_V5_R3%2Fsswrp_gpu:RDO_SSWRP_GPU_M4_V4_R17_D3_E3_C6%2Fcsr%2Fsswrp_gpu%2Fglue_mba_host_csr&pli=1

// <register>_address values represent addresses relative to the top-level
// containing chip. These values are equivalent to the "address" values
// displayed in the Codex UI.
// <register>_offset values represent addresses relative to the immediate
// containing chip, block, or section. These values are equivalent to the
// "offset" values displayed in the Codex UI.

#define GLUE_MBA_VERSION_address 0x640000
#define GLUE_MBA_VERSION_offset 0x0
#define GLUE_MBA_VERSION_mask 0xffffffff
#define GLUE_MBA_VERSION_read_mask 0xffffffff
#define GLUE_MBA_VERSION_write_mask 0x0
#define GLUE_MBA_VERSION__VERSION_offset 0
#define GLUE_MBA_VERSION__VERSION_mask 0xffffffff
#define GLUE_MBA_VERSION__VERSION_default 0x0

#define GPIO_STATUS_address 0x640030
#define GPIO_STATUS_offset 0x30
#define GPIO_STATUS_mask 0x3ff
#define GPIO_STATUS_read_mask 0x3ff
#define GPIO_STATUS_write_mask 0x0
#define GPIO_STATUS__GPIO_INPUT_REQ_offset 0
#define GPIO_STATUS__GPIO_INPUT_REQ_mask 0x1
#define GPIO_STATUS__GPIO_INPUT_REQ_default 0x0
#define GPIO_STATUS__GPIO_INPUT_ACK_offset 1
#define GPIO_STATUS__GPIO_INPUT_ACK_mask 0x2
#define GPIO_STATUS__GPIO_INPUT_ACK_default 0x0
#define GPIO_STATUS__GPIO_INPUT_DATA_offset 2
#define GPIO_STATUS__GPIO_INPUT_DATA_mask 0x3fc
#define GPIO_STATUS__GPIO_INPUT_DATA_default 0x0

#define INTR_GPU_MBA_ISR_address 0x640034
#define INTR_GPU_MBA_ISR_offset 0x34
#define INTR_GPU_MBA_ISR_mask 0x3
#define INTR_GPU_MBA_ISR_read_mask 0x3
#define INTR_GPU_MBA_ISR_write_mask 0x3
#define INTR_GPU_MBA_ISR__LOCAL_MBA_offset 0
#define INTR_GPU_MBA_ISR__LOCAL_MBA_mask 0x1
#define INTR_GPU_MBA_ISR__LOCAL_MBA_default 0x0
#define INTR_GPU_MBA_ISR__TPU_MBA_offset 1
#define INTR_GPU_MBA_ISR__TPU_MBA_mask 0x2
#define INTR_GPU_MBA_ISR__TPU_MBA_default 0x0

#define INTR_GPU_MBA_ISR_OVF_address 0x640038
#define INTR_GPU_MBA_ISR_OVF_offset 0x38
#define INTR_GPU_MBA_ISR_OVF_mask 0x3
#define INTR_GPU_MBA_ISR_OVF_read_mask 0x3
#define INTR_GPU_MBA_ISR_OVF_write_mask 0x3
#define INTR_GPU_MBA_ISR_OVF__LOCAL_MBA_offset 0
#define INTR_GPU_MBA_ISR_OVF__LOCAL_MBA_mask 0x1
#define INTR_GPU_MBA_ISR_OVF__LOCAL_MBA_default 0x0
#define INTR_GPU_MBA_ISR_OVF__TPU_MBA_offset 1
#define INTR_GPU_MBA_ISR_OVF__TPU_MBA_mask 0x2
#define INTR_GPU_MBA_ISR_OVF__TPU_MBA_default 0x0

#define INTR_GPU_MBA_IER_address 0x64003c
#define INTR_GPU_MBA_IER_offset 0x3c
#define INTR_GPU_MBA_IER_mask 0x3
#define INTR_GPU_MBA_IER_read_mask 0x3
#define INTR_GPU_MBA_IER_write_mask 0x3
#define INTR_GPU_MBA_IER__LOCAL_MBA_offset 0
#define INTR_GPU_MBA_IER__LOCAL_MBA_mask 0x1
#define INTR_GPU_MBA_IER__LOCAL_MBA_default 0x1
#define INTR_GPU_MBA_IER__TPU_MBA_offset 1
#define INTR_GPU_MBA_IER__TPU_MBA_mask 0x2
#define INTR_GPU_MBA_IER__TPU_MBA_default 0x1

#define INTR_GPU_MBA_IMR_address 0x640040
#define INTR_GPU_MBA_IMR_offset 0x40
#define INTR_GPU_MBA_IMR_mask 0x3
#define INTR_GPU_MBA_IMR_read_mask 0x3
#define INTR_GPU_MBA_IMR_write_mask 0x3
#define INTR_GPU_MBA_IMR__LOCAL_MBA_offset 0
#define INTR_GPU_MBA_IMR__LOCAL_MBA_mask 0x1
#define INTR_GPU_MBA_IMR__LOCAL_MBA_default 0x0
#define INTR_GPU_MBA_IMR__TPU_MBA_offset 1
#define INTR_GPU_MBA_IMR__TPU_MBA_mask 0x2
#define INTR_GPU_MBA_IMR__TPU_MBA_default 0x0

#define INTR_GPU_MBA_ITR_address 0x640044
#define INTR_GPU_MBA_ITR_offset 0x44
#define INTR_GPU_MBA_ITR_mask 0x3
#define INTR_GPU_MBA_ITR_read_mask 0x3
#define INTR_GPU_MBA_ITR_write_mask 0x3
#define INTR_GPU_MBA_ITR__LOCAL_MBA_offset 0
#define INTR_GPU_MBA_ITR__LOCAL_MBA_mask 0x1
#define INTR_GPU_MBA_ITR__LOCAL_MBA_default 0x0
#define INTR_GPU_MBA_ITR__TPU_MBA_offset 1
#define INTR_GPU_MBA_ITR__TPU_MBA_mask 0x2
#define INTR_GPU_MBA_ITR__TPU_MBA_default 0x0

#endif  // SI_CODEX_CSR__SSWRP_GPU__GLUE_MBA_HOST_CSR_H_
