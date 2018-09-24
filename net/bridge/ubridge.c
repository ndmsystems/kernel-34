#define DRV_NAME		"ubridge"
#define DRV_VERSION		"1.1"
#define DRV_DESCRIPTION	"Tiny bridge driver"
#define DRV_COPYRIGHT	"(C) 2012-2016 NDM Systems Inc. <ap@ndmsystems.com>"

/*
 * UBR_UC_SYNC - allow sync unicast list for slave device.
 * Note: usbnet devices usually not implements unicast list.
 */
/* #define UBR_UC_SYNC */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_bridge.h>
#include <linux/netfilter_bridge.h>
#include <net/ip.h>
#include <net/arp.h>
#include <../net/8021q/vlan.h>
#include <net/fast_vpn.h>
#include "ubridge_private.h"

#if IS_ENABLED(CONFIG_IPV6)
#include <linux/ipv6.h>
#include <net/if_inet6.h>
#endif

#if IS_ENABLED(CONFIG_RA_HW_NAT)
#include <../ndm/hw_nat/ra_nat.h>
#endif

#define EBM_ETH_TYPE 0x6120

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)) && !defined(ether_addr_copy)
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) && !defined(eth_broadcast_addr)
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) */

#define MAC_FORCED		0

#define vlan_group_for_each_dev(grp, i, dev) \
	for ((i) = 0; i < VLAN_N_VID; i++) \
		if (((dev) = vlan_group_get_device((grp), \
							    (i) % VLAN_N_VID)))

static LIST_HEAD(ubr_list);

static int ubr_dev_ioctl(struct net_device *, struct ifreq *, int);
static int ubr_set_mac_addr_force(struct net_device *dev, void *p);

static inline struct ubr_private *ubr_priv_get_rcu(
	const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler_data);
}

static inline bool is_netdev_rawip(struct net_device *netdev)
{
	return (is_tuntap(netdev) && is_tuntap_tun(netdev)) ||
			(netdev->type == ARPHRD_NONE);
}

static rx_handler_result_t ubr_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct ubr_private *ubr = ubr_priv_get_rcu(skb->dev);
	struct br_cpu_netstats *ustats;

	if (ubr == NULL)
		return RX_HANDLER_PASS;

#ifdef DEBUG
	pr_info("%s: packet %s -> %s\n",
		__func__, ubr->slave_dev->name, ubr->dev->name);
#endif

	ustats = this_cpu_ptr(ubr->stats);

	if (likely(skb->protocol != htons(EBM_ETH_TYPE)
#if IS_ENABLED(CONFIG_FAST_NAT)
		&& !SWNAT_KA_CHECK_MARK(skb)
#endif
#if IS_ENABLED(CONFIG_RA_HW_NAT)
		&& !FOE_SKB_IS_KEEPALIVE(skb)
#endif
		)) {
		u64_stats_update_begin(&ustats->syncp);
		ustats->rx_packets++;
		ustats->rx_bytes += skb->len;
		u64_stats_update_end(&ustats->syncp);
	}

	skb->dev = ubr->dev;
	skb->pkt_type = PACKET_HOST;

	dst_release(skb_dst(skb));
	skb_dst_set(skb, NULL);

	if (is_netdev_rawip(ubr->slave_dev)) {
		struct iphdr *iph;
		struct ethhdr *eth;

		if (unlikely((skb_headroom(skb) < ETH_HLEN)
			|| skb_shared(skb)
			|| skb_cloned(skb))) {
			struct sk_buff *skb2 = skb_realloc_headroom(skb, ETH_HLEN);

			consume_skb(skb);
			if (!skb2) {
				ubr->dev->stats.rx_dropped++;

				return RX_HANDLER_CONSUMED;
			}

			skb = skb2;
		}

		iph = (struct iphdr *)skb->data;

		skb_push(skb, ETH_HLEN);

		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);

		eth = (struct ethhdr *)skb->data;

		ether_addr_copy(eth->h_dest, ubr->dev->dev_addr);

		if (iph->version == 6) {
#if IS_ENABLED(CONFIG_IPV6)
			struct ipv6hdr *ip6h = (struct ipv6hdr *)iph;

			if (skb->len < ETH_HLEN + sizeof(struct ipv6hdr)) {
				kfree_skb(skb);
				ubr->dev->stats.rx_errors++;

				return RX_HANDLER_CONSUMED;
			}

			if (ipv6_addr_is_multicast(&ip6h->daddr))
				ipv6_eth_mc_map(&ip6h->daddr, eth->h_dest);

			eth->h_proto = htons(ETH_P_IPV6);
#else
			kfree_skb(skb);
			ubr->dev->stats.rx_errors++;

			return RX_HANDLER_CONSUMED;
#endif
		} else if (iph->version == 4) {
			if (ipv4_is_lbcast(iph->daddr))
				eth_broadcast_addr(eth->h_dest);
			else if (ipv4_is_multicast(iph->daddr))
				ip_eth_mc_map(iph->daddr, eth->h_dest);

			eth->h_proto = htons(ETH_P_IP);
		} else {
			/* Something wierd... */

			consume_skb(skb);
			ubr->dev->stats.rx_dropped++;

			return RX_HANDLER_CONSUMED;
		}

		ether_addr_copy(eth->h_source, ubr->dev->dev_addr);

		skb->protocol = eth_type_trans(skb, ubr->dev);
	}

	netif_receive_skb(skb);

	return RX_HANDLER_CONSUMED;
}

int ubr_update_stats(struct net_device *dev, unsigned long rxbytes,
	unsigned long rxpackets, unsigned long txbytes, unsigned long txpackets)
{
	struct ubr_private *ubr = netdev_priv(dev);
	struct br_cpu_netstats *ustats;

	if (!is_ubridge(dev) || ubr == NULL)
		return -EINVAL;

	ustats = this_cpu_ptr(ubr->stats);

	u64_stats_update_begin(&ustats->syncp);
	ustats->rx_packets += rxpackets;
	ustats->rx_bytes += rxbytes;
	ustats->tx_packets += txpackets;
	ustats->tx_bytes += txbytes;
	u64_stats_update_end(&ustats->syncp);

	return 0;
}
EXPORT_SYMBOL(ubr_update_stats);

static int ubr_init(struct net_device *dev)
{
	struct ubr_private *ubr = netdev_priv(dev);

	ubr->stats = netdev_alloc_pcpu_stats(struct br_cpu_netstats);
	if (!ubr->stats)
		return -ENOMEM;

	return 0;
}

static int ubr_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int ubr_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static void ubr_send_arp_reply(struct sk_buff *skb,
			    struct ubr_private *ubr)
{
	const struct ethhdr *eth_src = (struct ethhdr *)eth_hdr(skb);
	const struct arphdr *arph_src =
		(struct arphdr *)((u8 *)eth_src + ETH_HLEN);
	struct sk_buff *skb2;
	u8 *p_src;
	unsigned int data_len;
	__be32 source_ip, target_ip;

	data_len = ETH_HLEN + arp_hdr_len(ubr->dev);
	if (skb->len < data_len) {
		ubr->dev->stats.tx_errors++;
		return;
	}

	p_src = (u8 *)(arph_src + 1);
	source_ip = *((__be32 *)(p_src + ETH_ALEN));
	target_ip = *((__be32 *)(p_src + 2 * ETH_ALEN + sizeof(u32)));

	if (arph_src->ar_op != htons(ARPOP_REQUEST) ||
	    ipv4_is_multicast(target_ip) ||
	    target_ip == 0)
		return;

	skb2 = arp_create(ARPOP_REPLY, ETH_P_ARP,
			  source_ip,	/* Set target IP as source IP address */
			  ubr->dev,
			  target_ip,	/* Set sender IP as target IP address */
			  eth_src->h_source,
			  NULL,		/* Set sender MAC as interface MAC */
			  p_src);	/* Set target MAC as source MAC */

	if (skb2 == NULL)
		return;

	skb_reset_mac_header(skb2);
	skb_pull_inline(skb2, ETH_HLEN);

	netif_receive_skb(skb2);
}

static netdev_tx_t ubr_xmit(struct sk_buff *skb,
			    struct net_device *dev)
{
	struct ubr_private *ubr = netdev_priv(dev);
	struct net_device *slave_dev = ubr->slave_dev;
	struct br_cpu_netstats *ustats;

	if (!slave_dev) {
		dev_kfree_skb(skb);
		return -ENOTCONN;
	}

	if (is_netdev_rawip(slave_dev)) {
		struct ethhdr *eth = (struct ethhdr *)skb->data;
		unsigned int maclen = 0;

		switch (ntohs(eth->h_proto)) {
		case ETH_P_IP:
		case ETH_P_IPV6:
			maclen = ETH_HLEN;
			break;
		case ETH_P_ARP:
			skb_reset_mac_header(skb);
			ubr_send_arp_reply(skb, ubr);
			consume_skb(skb);
			return NET_XMIT_SUCCESS;
		default:
			break;
		}

		if (maclen == 0) {
			/* Unsupported protocols, silently send packets to void */
			consume_skb(skb);
			dev->stats.tx_dropped++;

			return NET_XMIT_SUCCESS;
		}

		if (!(dev->flags & IFF_PROMISC) &&
			!ether_addr_equal(eth->h_dest, dev->dev_addr) &&
			!is_multicast_ether_addr(eth->h_dest) &&
			!is_broadcast_ether_addr(eth->h_dest)) {
			/* Packet is not for us, silently send it to void */
			consume_skb(skb);
			dev->stats.tx_dropped++;

			return NET_XMIT_SUCCESS;
		}

		skb_pull_inline(skb, maclen);
		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
	}

	ustats = this_cpu_ptr(ubr->stats);

	if (likely(skb->protocol != htons(EBM_ETH_TYPE)
#if IS_ENABLED(CONFIG_FAST_NAT)
		&& !SWNAT_KA_CHECK_MARK(skb)
#endif
#if IS_ENABLED(CONFIG_RA_HW_NAT)
		&& !FOE_SKB_IS_KEEPALIVE(skb)
#endif
		)) {
		u64_stats_update_begin(&ustats->syncp);
		ustats->tx_packets++;
		ustats->tx_bytes += skb->len;
		u64_stats_update_end(&ustats->syncp);
	}

	skb->dev = slave_dev;
	return dev_queue_xmit(skb);
}

static struct rtnl_link_stats64 *ubr_get_stats64(struct net_device *dev,
						struct rtnl_link_stats64 *stats)
{
	struct ubr_private *ubr = netdev_priv(dev);
	struct br_cpu_netstats tmp, sum = { 0 };
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		unsigned int start;
		const struct br_cpu_netstats *ustats = per_cpu_ptr(ubr->stats, cpu);

		do {
			start = u64_stats_fetch_begin_bh(&ustats->syncp);
			tmp.tx_bytes   = ustats->tx_bytes;
			tmp.tx_packets = ustats->tx_packets;
			tmp.rx_bytes   = ustats->rx_bytes;
			tmp.rx_packets = ustats->rx_packets;
		} while (u64_stats_fetch_retry_bh(&ustats->syncp, start));

		sum.tx_bytes   += tmp.tx_bytes;
		sum.tx_packets += tmp.tx_packets;
		sum.rx_bytes   += tmp.rx_bytes;
		sum.rx_packets += tmp.rx_packets;
	}

	stats->tx_bytes   = sum.tx_bytes;
	stats->tx_packets = sum.tx_packets;
	stats->rx_bytes   = sum.rx_bytes;
	stats->rx_packets = sum.rx_packets;

	stats->tx_errors  = dev->stats.tx_errors;
	stats->tx_dropped = dev->stats.tx_dropped;
	stats->rx_errors  = dev->stats.rx_errors;
	stats->rx_dropped = dev->stats.rx_dropped;

	return stats;
}

static void ubr_change_rx_flags(struct net_device *dev, int change)
{
	struct ubr_private *master_info = netdev_priv(dev);
	struct net_device *slave_dev = master_info->slave_dev;

	if (!slave_dev)
		return;

	if (is_netdev_rawip(slave_dev))
		return;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(slave_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);

	if (change & IFF_PROMISC)
		dev_set_promiscuity(slave_dev, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void ubr_set_rx_mode(struct net_device *dev)
{
	struct ubr_private *master_info = netdev_priv(dev);
	struct net_device *slave_dev = master_info->slave_dev;

	if (!slave_dev)
		return;

#ifdef UBR_UC_SYNC
	dev_uc_sync(slave_dev, dev);
#endif
	dev_mc_sync(slave_dev, dev);
}

static const struct net_device_ops ubr_netdev_ops = {
	.ndo_init		 = ubr_init,
	.ndo_open		 = ubr_open,
	.ndo_stop		 = ubr_stop,
	.ndo_start_xmit		 = ubr_xmit,
	.ndo_get_stats64	 = ubr_get_stats64,
	.ndo_do_ioctl		 = ubr_dev_ioctl,
	.ndo_change_rx_flags	 = ubr_change_rx_flags,
	.ndo_set_rx_mode	 = ubr_set_rx_mode,
	.ndo_set_mac_address	 = ubr_set_mac_addr_force,
};

static void ubr_dev_free(struct net_device *dev)
{
	struct ubr_private *ubr = netdev_priv(dev);

	free_percpu(ubr->stats);
	free_netdev(dev);
}

/* RTNL locked */
static int ubr_deregister(struct net_device *dev)
{
	struct ubr_private *ubr = netdev_priv(dev);

	dev_close(dev);

	if (ubr) {
		if (!list_empty(&ubr->list))
			list_del(&ubr->list);
		if (ubr->slave_dev)
			netdev_rx_handler_unregister(ubr->slave_dev);
	}

	unregister_netdevice(dev);

	return 0;
}

static int ubr_free_master(struct net *net, const char *name)
{
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = __dev_get_by_name(net, name);
	if (dev == NULL)
		ret =  -ENXIO; /* Could not find device */
	else if (dev->flags & IFF_UP)
		/* Not shutdown yet. */
		ret = -EBUSY;
	else
		ret = ubr_deregister(dev);
	rtnl_unlock();

	return ret;
}

static int ubr_set_mac_addr(
	struct net_device *master_dev, struct sockaddr *addr)
{
	struct sockaddr old_addr;
	struct vlan_info *vlan_info;

	ether_addr_copy(old_addr.sa_data, master_dev->dev_addr);
	ether_addr_copy(master_dev->dev_addr, addr->sa_data);

	/* Update all VLAN sub-devices' MAC address */
	vlan_info = rtnl_dereference(master_dev->vlan_info);
	if (vlan_info) {
		struct vlan_group *grp = &vlan_info->grp;
		struct net_device *vlan_dev;
		int i, err;

		vlan_group_for_each_dev(grp, i, vlan_dev) {
			struct sockaddr vaddr;

			/* Do not modify manually changed vlan MAC */
			if (compare_ether_addr(old_addr.sa_data, vlan_dev->dev_addr))
				continue;

			ether_addr_copy(vaddr.sa_data, addr->sa_data);
			vaddr.sa_family = vlan_dev->type;

			err = dev_set_mac_address(vlan_dev, &vaddr);
			if (err)
				netdev_dbg(vlan_dev, "can't set MAC for device %s (error %d)\n",
						vlan_dev->name, err);
		}
	}

	call_netdevice_notifiers(NETDEV_CHANGEADDR, master_dev);

	return 0;
}

static int ubr_set_mac_addr_force(struct net_device *dev, void *p)
{
	struct ubr_private *ubr0 = netdev_priv(dev);
	int ret = ubr_set_mac_addr(dev, p);

	if (!ret) {
		struct net_device *slave_dev = ubr0->slave_dev;

		set_bit(MAC_FORCED, &ubr0->flags);

		if (slave_dev && !is_netdev_rawip(slave_dev)) {

			if (compare_ether_addr(dev->dev_addr, slave_dev->dev_addr))
				dev_set_promiscuity(slave_dev, 1);
			else
				dev_set_promiscuity(slave_dev, -1);

		}
	}

	return ret;
}

static int ubr_alloc_master(const char *name)
{
	struct net_device *dev;
	struct ubr_private *ubr;
	int err = 0;

	dev = alloc_netdev(sizeof(struct ubr_private), name, ether_setup);
	if (!dev)
		return -ENOMEM;

	ubr = netdev_priv(dev);
	ubr->dev = dev;

	random_ether_addr(dev->dev_addr);

	dev->tx_queue_len	= 0; /* A queue is silly for a loopback device */
	dev->features		= NETIF_F_FRAGLIST
						| NETIF_F_HIGHDMA
						| NETIF_F_LLTX;
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;
	dev->netdev_ops		= &ubr_netdev_ops;
	dev->destructor		= ubr_dev_free;
	dev->priv_flags		|= IFF_UBRIDGE;

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		goto out;
	}

	netif_carrier_off(dev);

	rtnl_lock();
	list_add(&ubr->list, &ubr_list);
	rtnl_unlock();

out:
	return err;
}

static int ubr_atto_master(struct net_device *master_dev, int ifindex)
{
	struct net_device *dev1;
	struct ubr_private *ubr0 = netdev_priv(master_dev);
	unsigned mac_differ = 0;
#ifdef CONFIG_NET_NS
	struct net *net = master_dev->nd_net;
#else
	struct net *net = &init_net;
#endif
	int err = -ENODEV;
	int is_rawip = 0;

	if (ubr0->slave_dev != NULL)
		return -EBUSY;

	dev1 = __dev_get_by_index(net, ifindex);
	if (!dev1)
		goto out;

	if (is_netdev_rawip(dev1)) {
		is_rawip = 1;
	} else {
		if (!test_bit(MAC_FORCED, &ubr0->flags)) {
			struct sockaddr addr;

			ether_addr_copy(addr.sa_data, dev1->dev_addr);

			if (ubr_set_mac_addr(master_dev, &addr))
				pr_err("ubr_atto_master error setting MAC\n");
		}

		mac_differ = compare_ether_addr(master_dev->dev_addr, dev1->dev_addr);
	}

	ubr0->slave_dev = dev1;

	err = netdev_rx_handler_register(dev1, ubr_handle_frame, ubr0);
	if (err)
		goto out;

	if (!is_rawip) {
		if ((master_dev->flags & IFF_PROMISC) || mac_differ)
			dev_set_promiscuity(dev1, 1);

		if (master_dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(dev1, 1);
	}

	netif_carrier_on(master_dev);
	dev1->priv_flags |= IFF_UBRIDGE_PORT;

	err = 0;

out:
	return err;
}

static int ubr_detach(struct net_device *master_dev, int ifindex)
{
	struct net_device *dev1;
	struct ubr_private *ubr0 = netdev_priv(master_dev);
#ifdef CONFIG_NET_NS
	struct net *net = master_dev->nd_net;
#else
	struct net *net = &init_net;
#endif
	int err = -EINVAL;

	dev1 = __dev_get_by_index(net, ifindex);
	if (!dev1)
		goto out;

	if (ubr0->slave_dev != dev1)
		goto out;

	ubr0->slave_dev = NULL;

	netdev_rx_handler_unregister(dev1);

	if (!is_netdev_rawip(dev1)) {
		if (master_dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(dev1, -1);

		if (master_dev->flags & IFF_PROMISC)
			dev_set_promiscuity(dev1, -1);
	}

	dev1->priv_flags &= ~IFF_UBRIDGE_PORT;

	err = 0;

out:
	return err;
}

#define SHOW_BUF_MAX_LEN	4096

static long ubr_show(char *buf, long len)
{
	long written = 0;
	struct ubr_private *ubr_item;

	if (len == 0 || len > SHOW_BUF_MAX_LEN)
		len = SHOW_BUF_MAX_LEN;

	list_for_each_entry(ubr_item, &ubr_list, list) {
		written += snprintf(buf + written, len - written,
				"%-16s %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\t",
				ubr_item->dev->name, ubr_item->dev->dev_addr[0],
				ubr_item->dev->dev_addr[1], ubr_item->dev->dev_addr[2],
				ubr_item->dev->dev_addr[3], ubr_item->dev->dev_addr[4],
				ubr_item->dev->dev_addr[5]);
		if (written >= len - 2)
			break;

		if (ubr_item->slave_dev == NULL)
			written += sprintf(buf + written, "-\n");
		else
			written += snprintf(buf + written, len - written, "%s\n",
				ubr_item->slave_dev->name);
		if (written >= len - 1)
			break;
	}

	return written;
}

int ubr_ioctl_deviceless_stub(
	struct net *net, unsigned int cmd, void __user *uarg)
{
	char buf[IFNAMSIZ];

	switch (cmd) {
	case SIOCUBRADDBR:
	case SIOCUBRDELBR:
		if (copy_from_user(buf, uarg, IFNAMSIZ))
			return -EFAULT;

		buf[IFNAMSIZ-1] = 0;
		if (cmd == SIOCUBRADDBR)
			return ubr_alloc_master(buf);

		return ubr_free_master(net, buf);
	case SIOCUBRSHOW:
		{
			char *buf_;
			long res;
			struct {
				long len;
				char *buf;
			} args;

			if (copy_from_user(&args, uarg, sizeof(args)))
				return -EFAULT;
			buf_ = kzalloc(SHOW_BUF_MAX_LEN, GFP_KERNEL);
			if (buf_ == NULL)
				return -ENOMEM;
			res = ubr_show(buf_, args.len);
			if (copy_to_user(args.buf, buf_, res) ||
					copy_to_user(uarg, &res, sizeof(long))) {
				kfree(buf_);
				return -EFAULT;
			}
			kfree(buf_);
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

static int ubr_dev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	switch (cmd) {
	case SIOCBRADDIF:
		return ubr_atto_master(dev, rq->ifr_ifindex);

	case SIOCBRDELIF:
		return ubr_detach(dev, rq->ifr_ifindex);

	}
	return -EOPNOTSUPP;
}

static int ubr_dev_event(
		struct notifier_block *unused,
		unsigned long event,
		void *ptr)
{
	struct net_device *pdev = ptr;
	struct ubr_private *ubr;

	if (event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	rcu_read_lock();
	if (rcu_access_pointer(pdev->rx_handler) != ubr_handle_frame) {
		rcu_read_unlock();
		return NOTIFY_DONE;
	}

	ubr = ubr_priv_get_rcu(pdev);
	if (ubr && ubr->slave_dev) {
		/* delif */
		netdev_rx_handler_unregister(ubr->slave_dev);
		ubr->slave_dev = NULL;
	}

	rcu_read_unlock();
	return NOTIFY_DONE;
}

static struct notifier_block ubr_device_notifier = {
	.notifier_call  = ubr_dev_event,
};

static int __init ubridge_init(void)
{
	ubrioctl_set(ubr_ioctl_deviceless_stub);
	pr_info("ubridge: %s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	if (register_netdevice_notifier(&ubr_device_notifier))
		pr_err("%s: Error registering notifier\n", __func__);
	return 0;
}

static void __exit ubridge_exit(void)
{
	struct ubr_private *ubr, *tmp;

	unregister_netdevice_notifier(&ubr_device_notifier);
	ubrioctl_set(NULL);
	rtnl_lock();
	list_for_each_entry_safe(ubr, tmp, &ubr_list, list) {
		ubr_deregister(ubr->dev);
	}
	rtnl_unlock();

	pr_info("ubridge: driver unloaded\n");
}

module_init(ubridge_init);
module_exit(ubridge_exit);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

