/*
 * (C) 2018 Ilya Ponetaev <i.ponetaev@ndmsystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation (or any later at your option).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/atomic.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/netlink.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/sock.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_dryrun.h>

#define DRYRUN_SKB_SIZE_FILTER	(sizeof(struct iphdr) + sizeof(struct tcphdr))
#define OKFN_ACCEPT_RESULT		0x4e444d20 /* { 'N', 'D', 'M', ' ' } */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya Ponetaev <i.ponetaev@ndmsystems.com>");
MODULE_DESCRIPTION("nfdryrun: Netfilter dry-run traversal infrastructure");

static int nfnl_dryrun_test_finish(struct sk_buff *skb)
{
	return OKFN_ACCEPT_RESULT;
}

static int
nfnl_dryrun_test(struct sock *nfnl, struct sk_buff *skb,
	     const struct nlmsghdr *nlh, const struct nlattr * const tb[])
{
	struct net_device *dev;
	struct sk_buff *test_skb, *skb2;
	struct iphdr *iph;
	struct tcphdr *tcph;
	int hook_res = 0;
	struct nlmsghdr *nnlh;
	struct nfgenmsg *nfmsg;
	__be32 new_dst;
	__be16 new_dst_port;
	int err = 0;

	if (!tb[NFDRYRUN_IFNAME] ||
		!tb[NFDRYRUN_SRC_V4] ||
		!tb[NFDRYRUN_DST_V4] ||
		!tb[NFDRYRUN_DST_PORT])
		return -EINVAL;

	dev = dev_get_by_name(&init_net, nla_data(tb[NFDRYRUN_IFNAME]));

	if (!dev)
		return -ENOENT;

	test_skb = __netdev_alloc_skb(dev, DRYRUN_SKB_SIZE_FILTER, GFP_KERNEL);

	if (!test_skb) {
		dev_put(dev);
		return -ENOMEM;
	}

	skb_reset_network_header(test_skb);
	iph = (struct iphdr *)skb_put(test_skb, sizeof(*iph));
	skb_pull(test_skb, sizeof(*iph));
	skb_reset_transport_header(test_skb);
	tcph = (struct tcphdr *)skb_put(test_skb, sizeof(*tcph));
	skb_push(test_skb, sizeof(*iph));

	test_skb->pkt_type = PACKET_HOST;
	test_skb->protocol = htons(ETH_P_IP);
	test_skb->ip_summed = CHECKSUM_NONE;
	test_skb->csum = 0;

	memset(iph, 0, sizeof(*iph));

	/* build ip header */
	iph->version	=	4;
	iph->ihl		=	sizeof(*iph) >> 2;
	iph->frag_off	=	0x0;
	iph->protocol	=	IPPROTO_TCP;
	iph->tos		=	0;
	iph->daddr		=	nla_get_u32(tb[NFDRYRUN_DST_V4]);
	iph->saddr		=	nla_get_u32(tb[NFDRYRUN_SRC_V4]);
	iph->ttl		=	IPDEFTTL;
	iph->tot_len	=	DRYRUN_SKB_SIZE_FILTER;
	iph->id			=	0;
	iph->check		=	0;
	iph->check		=	ip_fast_csum((unsigned char *)iph, iph->ihl);

	memset(tcph, 0, sizeof(*tcph));

	/* build tcp header */
	tcph->source	=	htons(54321); /* Random value */
	tcph->dest		=	nla_get_u16(tb[NFDRYRUN_DST_PORT]);
	tcph->seq		=	0;
	tcph->ack_seq	=	0;
	tcph->doff		=	sizeof(*tcph) >> 2;
	tcph->syn		=	1;
	tcph->window	=	htons(13600);
	tcph->urg_ptr	=	0;
	tcph->check		=	0;
	tcph->check		=	tcp_v4_check(test_skb->len, iph->saddr, iph->daddr,
							csum_partial(tcph, tcph->doff << 2, skb->csum));

	new_dst = iph->daddr;
	new_dst_port = tcph->dest;

	preempt_disable();
	local_bh_disable();
	hook_res = NF_HOOK(
		NFPROTO_IPV4, NF_INET_PRE_ROUTING, test_skb, test_skb->dev, NULL,
		nfnl_dryrun_test_finish);
	local_bh_enable();
	preempt_enable();

	if (hook_res != OKFN_ACCEPT_RESULT) {
		dev_put(dev);
		goto exit;
	}

	if (skb_dst(test_skb) == NULL) {
		int error;

		preempt_disable();
		local_bh_disable();
		error = ip_route_input_noref(test_skb, iph->daddr, iph->saddr,
					       iph->tos, test_skb->dev);
		local_bh_enable();
		preempt_enable();

		if (unlikely(error)) {
			dev_kfree_skb(test_skb);
			dev_put(dev);
			return err;
		}
	}

	preempt_disable();
	local_bh_disable();
	hook_res = NF_HOOK(
		NFPROTO_IPV4, NF_INET_LOCAL_IN, test_skb, test_skb->dev, NULL,
		nfnl_dryrun_test_finish);
	local_bh_enable();
	preempt_enable();

	new_dst = iph->daddr;
	new_dst_port = tcph->dest;

	dev_put(dev);

	if (hook_res == OKFN_ACCEPT_RESULT)
		kfree_skb(test_skb);

exit:
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);

	if (skb2 == NULL)
		return -EMSGSIZE;

	nnlh = nlmsg_put(skb2, NETLINK_CB(skb).pid,
			nlh->nlmsg_seq, (NFNL_SUBSYS_DRYRUN << 8) | NFNL_MSG_DRYRUN_TEST,
			sizeof(*nfmsg), 0);

	if (nnlh == NULL) {
		nlmsg_cancel(skb2, nnlh);
		kfree_skb(skb2);
		return -EMSGSIZE;
	}

	nfmsg = nlmsg_data(nnlh);
	nfmsg->nfgen_family = NFPROTO_IPV4;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = 0;

	if (nla_put_u8(skb2, NFDRYRUN_FILTER_RES,
			hook_res == OKFN_ACCEPT_RESULT) ||
		nla_put_be32(skb2, NFDRYRUN_DST_V4, new_dst) ||
		nla_put_be16(skb2, NFDRYRUN_DST_PORT, new_dst_port)) {
		nlmsg_cancel(skb2, nnlh);
		kfree_skb(skb2);
		return -EMSGSIZE;
	}

	nlmsg_end(skb2, nnlh);

	err = netlink_unicast(nfnl, skb2, NETLINK_CB(skb).pid, MSG_DONTWAIT);

	return err > 0 ? 0 : err;
}

static const struct nla_policy nfnl_dryrun_test_policy[NFDRYRUN_MAX+1] = {
	[NFDRYRUN_SRC_V4] = { .type = NLA_U32 },
	[NFDRYRUN_DST_V4] = { .type = NLA_U32 },
	[NFDRYRUN_DST_PORT] = { .type = NLA_U16 },
	[NFDRYRUN_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ - 1 },
};

static const struct nfnl_callback nfnl_dryrun_cb[NFNL_MSG_DRYRUN_MAX] = {
	[NFNL_MSG_DRYRUN_TEST]		= { .call = nfnl_dryrun_test,
							.attr_count = NFDRYRUN_MAX,
							.policy = nfnl_dryrun_test_policy },
};

static const struct nfnetlink_subsystem nfnl_dryrun_subsys = {
	.name				= "dryrun",
	.subsys_id			= NFNL_SUBSYS_DRYRUN,
	.cb_count			= NFNL_MSG_DRYRUN_MAX,
	.cb					= nfnl_dryrun_cb,
};

MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_DRYRUN);

static int __init nfnl_dryrun_init(void)
{
	int ret;

	pr_info("nfnl_dryrun: registering with nfnetlink.\n");
	ret = nfnetlink_subsys_register(&nfnl_dryrun_subsys);
	if (ret < 0) {
		pr_err("nfnl_dryrun_init: cannot register with nfnetlink.\n");
		goto err_out;
	}
	return 0;
err_out:
	return ret;
}

static void __exit nfnl_dryrun_exit(void)
{
	pr_info("nfnl_dryrun: unregistering from nfnetlink.\n");
	nfnetlink_subsys_unregister(&nfnl_dryrun_subsys);
}

module_init(nfnl_dryrun_init);
module_exit(nfnl_dryrun_exit);
