#ifndef __INCLUDE_LINUX_ETH_SWITCH_HOOKS_H
#define __INCLUDE_LINUX_ETH_SWITCH_HOOKS_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct net_device;

typedef bool eth_switch_iface_fn(const struct net_device *const dev);

typedef int eth_switch_map_mc_mac_fn(const struct net_device *const dev,
				     const u8 *const uc_mac,
				     const u8 *const mc_mac);

typedef int eth_switch_unmap_mc_mac_fn(const struct net_device *const dev,
				       const u8 *const uc_mac,
				       const u8 *const mc_mac);

typedef int eth_switch_set_wan_port_fn(const unsigned char port);

extern rwlock_t eth_switch_lock;

#define ETH_SWITCH_DECLARE_HOOK(name)					\
extern eth_switch_##name##_fn *eth_switch_##name##_hook;		\
									\
static inline eth_switch_##name##_fn *					\
eth_switch_##name##_hook_get(void)					\
{									\
	read_lock_bh(&eth_switch_lock);					\
									\
	return eth_switch_##name##_hook;				\
}									\
									\
static inline void							\
eth_switch_##name##_hook_put(void)					\
{									\
	read_unlock_bh(&eth_switch_lock);				\
}									\
									\
static inline void							\
eth_switch_##name##_hook_set(eth_switch_##name##_fn *name##_hook)	\
{									\
	write_lock_bh(&eth_switch_lock);				\
	eth_switch_##name##_hook = name##_hook;				\
	write_unlock_bh(&eth_switch_lock);				\
}

ETH_SWITCH_DECLARE_HOOK(iface)
ETH_SWITCH_DECLARE_HOOK(map_mc_mac)
ETH_SWITCH_DECLARE_HOOK(unmap_mc_mac)
ETH_SWITCH_DECLARE_HOOK(set_wan_port)

#endif /* __INCLUDE_LINUX_ETH_SWITCH_HOOKS_H */

