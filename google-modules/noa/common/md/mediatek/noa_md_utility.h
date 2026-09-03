#ifndef __NOA_MD_UTILITY_H__
#define __NOA_MD_UTILITY_H__

#include <linux/if_link.h>
#include <linux/skbuff.h>

#include "noa_md.h"
#include "noa_md_trace.h"

void noa_md_dump_ndev_info(struct net_device *netdev);
int noa_md_get_ndev_info(
        struct net_device *netdev, char* strbuff, ssize_t buf_size);

#endif /* __NOA_MD_UTILITY_H__ */