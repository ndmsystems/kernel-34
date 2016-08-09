#define DRV_NAME		"ubridge"
#define DRV_VERSION		"1.1"
#define DRV_DESCRIPTION	"Tiny bridge driver"
#define DRV_COPYRIGHT	"(C) 2012-2016 NDM Systems Inc. <ap@ndmsystems.com>"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_bridge.h>
#include <linux/netfilter_bridge.h>
#include <../net/8021q/vlan.h>
#include "br_private.h"

#define MAC_FORCED		0

#define vlan_group_for_each_dev(grp, i, dev) \
	for ((i) = 0; i < VLAN_N_VID; i++) \
		if (((dev) = vlan_group_get_device((grp), \
							    (i) % VLAN_N_VID)))

static LIST_HEAD(ubr_list);

struct ubr_private {
	struct net_device		*slave_dev;
	struct br_cpu_netstats	stats;
	struct list_head		list;
	struct net_device		*dev;
	unsigned long			flags;
};

static int ubr_dev_ioctl(struct net_device *, struct ifreq *, int);
static int ubr_set_mac_addr_force(struct net_device *dev, void *p);

static inline struct ubr_private *ubr_priv_get_rcu(const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler_data);
}

static rx_handler_result_t ubr_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct ubr_private *ubr = ubr_priv_get_rcu(skb->dev);

	if (ubr == NULL)
		return RX_HANDLER_PASS;

#ifdef DEBUG
	printk(KERN_INFO "%s: packet %s -> %s\n", __func__, ubr->slave_dev->name, ubr->dev->name);
#endif

	skb->dev = ubr->dev;
	skb->pkt_type = PACKET_HOST;
	ubr->dev->last_rx = jiffies;

	ubr->stats.rx_packets++;
	ubr->stats.rx_bytes += skb->len;
	dst_release(skb_dst(skb));
	skb_dst_set(skb, NULL);

	netif_receive_skb(skb);
	return RX_HANDLER_CONSUMED;
}

static int ubr_open(struct net_device *master_dev)
{
	netif_start_queue(master_dev);
	return 0;
}

static int ubr_stop(struct net_device *master_dev)
{
	netif_stop_queue(master_dev);
	return 0;
}

static netdev_tx_t ubr_xmit(struct sk_buff *skb,
			    struct net_device *master_dev)
{
	struct ubr_private *master_info = netdev_priv(master_dev);
	struct net_device *slave_dev = master_info->slave_dev;

	if (!slave_dev) {
		dev_kfree_skb(skb);
		return -ENOTCONN;
	}

	master_info->stats.tx_packets++;
	master_info->stats.tx_bytes += skb->len;

	skb->dev = slave_dev;
	return dev_queue_xmit(skb);
}

static struct rtnl_link_stats64 *ubr_get_stats64(struct net_device *dev,
						struct rtnl_link_stats64 *stats)
{
	struct ubr_private *ubr = netdev_priv(dev);
	struct br_cpu_netstats *sum = &ubr->stats;

	memset(stats, 0, sizeof (*stats));
	if (unlikely(sum == NULL))
		return NULL;

	stats->tx_bytes   = sum->tx_bytes;
	stats->tx_packets = sum->tx_packets;
	stats->rx_bytes   = sum->rx_bytes;
	stats->rx_packets = sum->rx_packets;

	return stats;
}

void ubr_change_rx_flags(struct net_device *dev,
						int flags)
{
	int err = 0;

	if (flags & IFF_PROMISC) {
		struct ubr_private *master_info = netdev_priv(dev);
		struct net_device *slave_dev = master_info->slave_dev;

		netdev_dbg(dev, "%s promiscuous mode for ubridge\n",
				dev->flags & IFF_PROMISC? "Set": "Clear");

		if (slave_dev)
			err = dev_set_promiscuity(slave_dev, dev->flags & IFF_PROMISC? 1: -1);

		if (err < 0)
			printk(KERN_ERR "Error changing promiscuity\n");
	}
}

static const struct net_device_ops ubr_netdev_ops =
{
	.ndo_open = ubr_open,
	.ndo_stop = ubr_stop,
	.ndo_start_xmit = ubr_xmit,
	.ndo_get_stats64 = ubr_get_stats64,
	.ndo_do_ioctl = ubr_dev_ioctl,
	.ndo_change_rx_flags = ubr_change_rx_flags,
	.ndo_set_mac_address = ubr_set_mac_addr_force,
};

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
		ret =  -ENXIO; 	/* Could not find device */
	else if (dev->flags & IFF_UP)
		/* Not shutdown yet. */
		ret = -EBUSY;
	else
		ret = ubr_deregister(dev);
	rtnl_unlock();

	return ret;
}

static int ubr_set_mac_addr(struct net_device *master_dev, struct sockaddr *addr)
{
	struct sockaddr old_addr;
	struct vlan_info *vlan_info;

	memcpy(old_addr.sa_data, master_dev->dev_addr, ETH_ALEN);
	memcpy(master_dev->dev_addr, addr->sa_data, ETH_ALEN);

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

			memcpy(vaddr.sa_data, addr->sa_data, ETH_ALEN);
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
		set_bit(MAC_FORCED, &ubr0->flags);

		if (ubr0->slave_dev) {

			if (compare_ether_addr(dev->dev_addr, ubr0->slave_dev->dev_addr))
				dev_set_promiscuity(ubr0->slave_dev, 1);
			else
				dev_set_promiscuity(ubr0->slave_dev, -1);

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
	dev->netdev_ops = &ubr_netdev_ops;
	dev->destructor		= free_netdev;

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		dev = ERR_PTR(err);
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
	unsigned mac_differ;
#ifdef CONFIG_NET_NS
	struct net *net = master_dev->nd_net;
#else
	struct net *net = &init_net;
#endif
	int err = -ENODEV;

	if (ubr0->slave_dev != NULL)
		return -EBUSY;

	dev1 = __dev_get_by_index(net, ifindex);
	if (!dev1)
		goto out;

	if (!test_bit(MAC_FORCED, &ubr0->flags)) {
		struct sockaddr addr;
		memcpy(addr.sa_data, dev1->dev_addr, ETH_ALEN);

		if (ubr_set_mac_addr(master_dev, &addr))
			printk(KERN_ERR "ubr_atto_master error setting MAC\n");
	}

	mac_differ = compare_ether_addr(master_dev->dev_addr, dev1->dev_addr);

	ubr0->slave_dev = dev1;

	err = netdev_rx_handler_register(dev1, ubr_handle_frame, ubr0);
	if (err) {
		goto out;
	}

	if (master_dev->flags & IFF_PROMISC || mac_differ)
		dev_set_promiscuity(dev1, 1);

	netif_carrier_on(master_dev);
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

	if (master_dev->flags & IFF_PROMISC)
		dev_set_promiscuity(dev1, -1);

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
		written += snprintf(buf + written, len - written, "%-16s %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\t",
				ubr_item->dev->name, ubr_item->dev->dev_addr[0], ubr_item->dev->dev_addr[1],
				ubr_item->dev->dev_addr[2], ubr_item->dev->dev_addr[3], ubr_item->dev->dev_addr[4],
				ubr_item->dev->dev_addr[5]);
		if (written >= len - 2)
			break;

		if (ubr_item->slave_dev == NULL)
			written += sprintf(buf + written, "-\n");
		else
			written += snprintf(buf + written, len - written, "%s\n", ubr_item->slave_dev->name);
		if (written >= len - 1)
			break;
	}

	return written;
}

int ubr_ioctl_deviceless_stub(struct net *net, unsigned int cmd, void __user *uarg)
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
			buf_ = kmalloc(SHOW_BUF_MAX_LEN, GFP_KERNEL);
			if (buf_ == NULL)
				return -ENOMEM;
			memset(buf_, 0, SHOW_BUF_MAX_LEN);
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
	printk(KERN_INFO "ubridge: %s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	if (register_netdevice_notifier(&ubr_device_notifier))
		printk(KERN_ERR "%s: Error registering notifier\n", __func__);
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

	printk(KERN_INFO "ubridge: driver unloaded\n");
}

module_init(ubridge_init);
module_exit(ubridge_exit);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

