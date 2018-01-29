#ifndef _NET_FLOW_KEYS_H
#define _NET_FLOW_KEYS_H

struct flow_keys {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	u8 ip_proto;
};

#if IS_ENABLED(CONFIG_NET_SCHED)
extern bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow);
#endif
#endif
