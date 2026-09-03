#ifndef GOOGLE_DPA_RING_SERVICE_PROXY_DEFS_H
#define GOOGLE_DPA_RING_SERVICE_PROXY_DEFS_H

#define GOOGLE_DPA_UNKNOWN_RING ((0))
#define GOOGLE_DPA_RING_MAX ((42U))
#define GOOGLE_DPA_RING_BUFFER_POOL_MAX ((3U))

struct google_dpa_ring {
	uint64_t base;
	uint64_t dpa_base;
	uint32_t write;
	uint32_t read;
	uint32_t ctrl;
	uint32_t len;
} __attribute__((packed, aligned(4)));

struct google_dpa_ring_shared_info {
	uint32_t num;
	struct google_dpa_ring entries[GOOGLE_DPA_RING_MAX];
} __attribute__((packed, aligned(4)));

struct google_dpa_ring_regs {
	u64 base;
	u64 len;
	u64 max_item;
	u64 read;
	u64 write;
	u64 dpa_base;
};
#endif /* GOOGLE_DPA_RING_SERVICE_PROXY_DEFS_H */
