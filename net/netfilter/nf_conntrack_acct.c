/* Accouting handling for netfilter. */

/*
 * (C) 2008 Krzysztof Piotr Oledzki <ole@ans.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netfilter.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_acct.h>

#include <net/fast_vpn.h>

#if IS_ENABLED(CONFIG_RA_HW_NAT)
#include <../ndm/hw_nat/ra_nat.h>
#endif

static bool nf_ct_acct __read_mostly = 1;

#if 0
module_param_named(acct, nf_ct_acct, bool, 0644);
MODULE_PARM_DESC(acct, "Enable connection tracking flow accounting.");

/* #ifdef CONFIG_SYSCTL */
static struct ctl_table acct_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_acct",
		.data		= &init_net.ct.sysctl_acct,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{}
};
#endif /* CONFIG_SYSCTL */

unsigned int
seq_print_acct(struct seq_file *s, const struct nf_conn *ct, int dir)
{
	struct nf_conn_counter *acct;

	acct = nf_conn_acct_find(ct);
	if (!acct)
		return 0;

	return seq_printf(s, "packets=%llu bytes=%llu ",
			  (unsigned long long)atomic64_read(&acct[dir].packets),
			  (unsigned long long)atomic64_read(&acct[dir].bytes));
};
EXPORT_SYMBOL_GPL(seq_print_acct);

static struct nf_ct_ext_type acct_extend __read_mostly = {
	.len	= sizeof(struct nf_conn_counter[IP_CT_DIR_MAX]),
	.align	= __alignof__(struct nf_conn_counter[IP_CT_DIR_MAX]),
	.id	= NF_CT_EXT_ACCT,
};

#if 0
static int nf_conntrack_acct_init_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(acct_sysctl_table, sizeof(acct_sysctl_table),
			GFP_KERNEL);
	if (!table)
		goto out;

	table[0].data = &net->ct.sysctl_acct;

	net->ct.acct_sysctl_header = register_net_sysctl_table(net,
			nf_net_netfilter_sysctl_path, table);
	if (!net->ct.acct_sysctl_header) {
		printk(KERN_ERR "nf_conntrack_acct: can't register to sysctl.\n");
		goto out_register;
	}
	return 0;

out_register:
	kfree(table);
out:
	return -ENOMEM;
}

static void nf_conntrack_acct_fini_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = net->ct.acct_sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->ct.acct_sysctl_header);
	kfree(table);
}
#else
static int nf_conntrack_acct_init_sysctl(struct net *net)
{
	return 0;
}

static void nf_conntrack_acct_fini_sysctl(struct net *net)
{
}
#endif

static unsigned int do_ipv4_conntrack_acct(unsigned int hooknum,
				      struct sk_buff *skb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	if (unlikely( 0
#if IS_ENABLED(CONFIG_FAST_NAT)
		|| SWNAT_KA_CHECK_MARK(skb)
#endif
#if IS_ENABLED(CONFIG_RA_HW_NAT)
		|| FOE_SKB_IS_KEEPALIVE(skb)
#endif
	)) {
		return NF_ACCEPT;
	}

	{
		struct nf_conn *ct;
		struct nf_conn_counter *acct;
		enum ip_conntrack_info ctinfo;

		ct = nf_ct_get(skb, &ctinfo);

		if (unlikely(ct == NULL)) {
			return NF_ACCEPT;
		}

		acct = nf_conn_acct_find(ct);

		if (likely(acct != NULL)) {
			atomic64_inc(&acct[CTINFO2DIR(ctinfo)].packets);
			atomic64_add(skb->len, &acct[CTINFO2DIR(ctinfo)].bytes);
		}
	}

	return NF_ACCEPT;
}

#ifdef CONFIG_IPV6
static unsigned int do_ipv6_conntrack_acct(unsigned int hooknum,
				      struct sk_buff *skb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	if (unlikely( 0
#if IS_ENABLED(CONFIG_RA_HW_NAT)
		|| FOE_SKB_IS_KEEPALIVE(skb)
#endif
	)) {
		return NF_ACCEPT;
	}

	{
		struct nf_conn *ct;
		struct nf_conn_counter *acct;
		enum ip_conntrack_info ctinfo;

		ct = nf_ct_get(skb, &ctinfo);

		if (unlikely(ct == NULL)) {
			return NF_ACCEPT;
		}

		acct = nf_conn_acct_find(ct);

		if (likely(acct != NULL)) {
			atomic64_inc(&acct[CTINFO2DIR(ctinfo)].packets);
			atomic64_add(skb->len, &acct[CTINFO2DIR(ctinfo)].bytes);
		}
	}

	return NF_ACCEPT;
}
#endif

static struct nf_hook_ops conntrack_acct_ops[] __read_mostly = {
	{
		.hook		= do_ipv4_conntrack_acct,
		.owner		= THIS_MODULE,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_ACCT,
	},
	{
		.hook		= do_ipv4_conntrack_acct,
		.owner		= THIS_MODULE,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_FORWARD,
		.priority	= NF_IP_PRI_CONNTRACK_ACCT,
	},
#ifdef CONFIG_IPV6
	{
		.hook		= do_ipv6_conntrack_acct,
		.owner		= THIS_MODULE,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_CONNTRACK_ACCT,
	},
	{
		.hook		= do_ipv6_conntrack_acct,
		.owner		= THIS_MODULE,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_FORWARD,
		.priority	= NF_IP6_PRI_CONNTRACK_ACCT,
	},
#endif
};

int nf_conntrack_acct_init(struct net *net)
{
	int ret;

	net->ct.sysctl_acct = nf_ct_acct;

	if (net_eq(net, &init_net)) {
		ret = nf_ct_extend_register(&acct_extend);
		if (ret < 0) {
			printk(KERN_ERR "nf_conntrack_acct: Unable to register extension\n");
			goto out_extend_register;
		}
	}

	ret = nf_conntrack_acct_init_sysctl(net);
	if (ret < 0)
		goto out_sysctl;

	ret = nf_register_hooks(conntrack_acct_ops,
				ARRAY_SIZE(conntrack_acct_ops));
	if (ret < 0) {
		printk(KERN_ERR "nf_conntrack_acct: can't register accounting hooks\n");
		goto out_acct_opts;
	}

	return 0;

out_acct_opts:
	nf_conntrack_acct_fini_sysctl(net);

out_sysctl:
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&acct_extend);
out_extend_register:
	return ret;
}

void nf_conntrack_acct_fini(struct net *net)
{
	nf_unregister_hooks(conntrack_acct_ops,
		ARRAY_SIZE(conntrack_acct_ops));

	nf_conntrack_acct_fini_sysctl(net);
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&acct_extend);
}
