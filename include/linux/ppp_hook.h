#ifndef _PPP_HOOK_H_
#define _PPP_HOOK_H_

#include <linux/types.h>

struct ppp_channel;
struct net_device;

#if IS_MODULE(CONFIG_PPP) || !IS_ENABLED(CONFIG_PPP)

extern void (*ppp_stat_add_tx_hook)(struct ppp_channel *pch,
				    u32 add_pkts,
				    u32 add_bytes);

extern void (*ppp_stat_add_rx_hook)(struct ppp_channel *pch,
				    u32 add_pkts,
				    u32 add_bytes);

extern void (*ppp_stat_reset_hook)(struct net_device *dev);

#if IS_ENABLED(CONFIG_RA_HW_NAT)
extern void (*ppp_stats_update_hook)(struct net_device *dev,
				     u32 rx_bytes, u32 rx_packets,
				     u32 tx_bytes, u32 tx_packets);
#if !defined(CONFIG_HNAT_V2)
extern void (*ppp_stat_block_hook)(struct net_device *dev, int is_block_rx);
#endif
#endif

/* ppp stat helpers */

static inline void ppp_stat_add_tx(struct ppp_channel *pch,
				   u32 add_pkts,
				   u32 add_bytes)
{
	typeof(ppp_stat_add_tx_hook) pfunc;

	rcu_read_lock();
	pfunc = rcu_dereference(ppp_stat_add_tx_hook);
	if (pfunc)
		pfunc(pch, add_pkts, add_bytes);
	rcu_read_unlock();
}

static inline void ppp_stat_add_rx(struct ppp_channel *pch,
				   u32 add_pkts,
				   u32 add_bytes)
{
	typeof(ppp_stat_add_rx_hook) pfunc;

	rcu_read_lock();
	pfunc = rcu_dereference(ppp_stat_add_rx_hook);
	if (pfunc)
		pfunc(pch, add_pkts, add_bytes);
	rcu_read_unlock();
}

static inline void ppp_stats_reset(struct net_device *dev)
{
	typeof(ppp_stat_reset_hook) pfunc;

	rcu_read_lock();
	pfunc = rcu_dereference(ppp_stat_reset_hook);
	if (pfunc)
		pfunc(dev);
	rcu_read_unlock();
}

#if IS_ENABLED(CONFIG_RA_HW_NAT)
static inline void ppp_stats_update(struct net_device *dev,
				    u32 rx_bytes, u32 rx_packets,
				    u32 tx_bytes, u32 tx_packets)
{
	typeof(ppp_stats_update_hook) pfunc;

	rcu_read_lock();
	pfunc = rcu_dereference(ppp_stats_update_hook);
	if (pfunc)
		pfunc(dev, rx_bytes, rx_packets, tx_bytes, tx_packets);
	rcu_read_unlock();
}

#if !defined(CONFIG_HNAT_V2)
static inline void ppp_stat_block(struct net_device *dev, int is_block_rx)
{
	typeof(ppp_stat_block_hook) pfunc;

	rcu_read_lock();
	pfunc = rcu_dereference(ppp_stat_block_hook);
	if (pfunc)
		pfunc(dev, is_block_rx);
	rcu_read_unlock();
}
#endif
#endif

#else /* IS_MODULE(CONFIG_PPP) || !IS_ENABLED(CONFIG_PPP) */

void ppp_stat_add_tx(struct ppp_channel *pch, u32 add_pkts, u32 add_bytes);
void ppp_stat_add_rx(struct ppp_channel *pch, u32 add_pkts, u32 add_bytes);
void ppp_stats_reset(struct net_device *dev);
#if IS_ENABLED(CONFIG_RA_HW_NAT)
void ppp_stats_update(struct net_device *dev,
			u32 rx_bytes, u32 rx_packets,
			u32 tx_bytes, u32 tx_packets);
#if !defined(CONFIG_HNAT_V2)
void ppp_stat_block(struct net_device *dev, int is_block_rx);
#endif
#endif

#endif /* IS_MODULE(CONFIG_PPP) || !IS_ENABLED(CONFIG_PPP) */

#endif
