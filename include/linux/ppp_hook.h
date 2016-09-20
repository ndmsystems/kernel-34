#ifndef _PPP_HOOK_H_
#define _PPP_HOOK_H_

#include <linux/types.h>

struct ppp_channel;
struct net_device;

extern void (*ppp_stat_add_tx_hook)(struct ppp_channel *chan, u32 add_pkt,
				    u32 add_bytes);

extern void (*ppp_stat_add_rx_hook)(struct ppp_channel *chan, u32 add_pkt,
				    u32 add_bytes);

#if IS_ENABLED(CONFIG_RA_HW_NAT)
extern void (*ppp_stats_update_hook)(struct net_device *dev,
				     u32 rx_bytes, u32 rx_packets,
				     u32 tx_bytes, u32 tx_packets);

#if !defined(CONFIG_HNAT_V2)
extern void (*ppp_stat_block_hook)(struct net_device *dev, int is_block_rx);
#endif
#endif

#endif

