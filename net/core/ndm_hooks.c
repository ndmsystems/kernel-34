#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <linux/skbuff.h>

struct ppp_channel;
struct net_device;
struct new_mc_streams;

int (*ppp_chan_stats_switch_get_hook)(struct ppp_channel *chan);
EXPORT_SYMBOL(ppp_chan_stats_switch_get_hook);

void (*ppp_stat_add_tx_hook)(struct ppp_channel *chan, u32 add_pkt,
			     u32 add_bytes);
EXPORT_SYMBOL(ppp_stat_add_tx_hook);

void (*ppp_stat_add_rx_hook)(struct ppp_channel *chan, u32 add_pkt,
			     u32 add_bytes);
EXPORT_SYMBOL(ppp_stat_add_rx_hook);

int (*ppp_stats_switch_get_hook)(struct net_device *dev);
EXPORT_SYMBOL(ppp_stats_switch_get_hook);

void (*ppp_stats_switch_set_hook)(struct net_device *dev, int on);
EXPORT_SYMBOL(ppp_stats_switch_set_hook);

void (*ppp_stats_update_hook)(struct net_device *dev,
			      u32 rx_bytes, u32 rx_packets,
			      u32 tx_bytes, u32 tx_packets);
EXPORT_SYMBOL(ppp_stats_update_hook);

int (*go_swnat)(struct sk_buff * skb, u8 origin) = NULL;
EXPORT_SYMBOL(go_swnat);

void (*prebind_from_fastnat)(struct sk_buff * skb,
	u32 orig_saddr, u16 orig_sport, struct nf_conn * ct,
	enum ip_conntrack_info ctinfo) = NULL;
EXPORT_SYMBOL(prebind_from_fastnat);

void (*prebind_from_l2tptx)(struct sk_buff * skb, struct sock * sock,
	u16 l2w_tid, u16 l2w_sid, u16 w2l_tid, u16 w2l_sid,
	u32 saddr, u32 daddr, u16 sport, u16 dport) = NULL;
EXPORT_SYMBOL(prebind_from_l2tptx);

void (*prebind_from_pptptx)(struct sk_buff * skb,
	struct iphdr * iph_int, struct sock *sock, u32 saddr, u32 daddr) = NULL;
EXPORT_SYMBOL(prebind_from_pptptx);

void (*prebind_from_pppoetx)(struct sk_buff * skb, struct sock *sock,
	u16 sid) = NULL;
EXPORT_SYMBOL(prebind_from_pppoetx);

void (*prebind_from_raeth)(struct sk_buff * skb) = NULL;
EXPORT_SYMBOL(prebind_from_raeth);

void (*prebind_from_usb_mac)(struct sk_buff * skb) = NULL;
EXPORT_SYMBOL(prebind_from_usb_mac);

void (*swnat_add_stats_l2tp)(u32 saddr, u32 daddr, u16 sport, u16 dport,
	u32 sent_bytes, u32 sent_packets, u32 recv_bytes, u32 recv_packets) = NULL;
EXPORT_SYMBOL(swnat_add_stats_l2tp);

void (*prebind_from_mc_preroute)(struct sk_buff * skb) = NULL;
EXPORT_SYMBOL(prebind_from_mc_preroute);

void (*prebind_from_mc_output)(struct sk_buff * skb, u8 origin) = NULL;
EXPORT_SYMBOL(prebind_from_mc_output);

void (*update_mc_streams)(struct new_mc_streams * streams_list) = NULL;
EXPORT_SYMBOL(update_mc_streams);

int (*pppoe_pthrough)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(pppoe_pthrough);

int (*ipv6_pthrough)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(ipv6_pthrough);

int (*vpn_pthrough)(struct sk_buff *skb, int in) = NULL;
EXPORT_SYMBOL(vpn_pthrough);

int (*vpn_pthrough_setup)(uint32_t sip, int add) = NULL;
EXPORT_SYMBOL(vpn_pthrough_setup);

int (*l2tp_input)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(l2tp_input);

int (*pptp_input)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(pptp_input);

#if defined(CONFIG_FAST_NAT) || defined(CONFIG_FAST_NAT_MODULE)

int (*fast_nat_hit_hook_func)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(fast_nat_hit_hook_func);

int (*fast_nat_bind_hook_func)(struct nf_conn *ct,
	enum ip_conntrack_info ctinfo,
	struct sk_buff *skb,
	u_int8_t protonum,
	struct nf_conntrack_l3proto *l3proto,
	struct nf_conntrack_l4proto *l4proto) = NULL;
EXPORT_SYMBOL(fast_nat_bind_hook_func);

int ipv4_fastnat_conntrack = 1;
EXPORT_SYMBOL(ipv4_fastnat_conntrack);
#endif /* defined(CONFIG_FAST_NAT) || defined(CONFIG_FAST_NAT_MODULE) */

int (*ra_sw_nat_hook_rx) (struct sk_buff * skb) = NULL;
EXPORT_SYMBOL(ra_sw_nat_hook_rx);

int (*ra_sw_nat_hook_tx) (struct sk_buff * skb, int gmac_no) = NULL;
EXPORT_SYMBOL(ra_sw_nat_hook_tx);
