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

void (*ppp_stat_add_tx_hook)(struct ppp_channel *chan, u32 add_pkt,
			     u32 add_bytes) = NULL;
EXPORT_SYMBOL(ppp_stat_add_tx_hook);

void (*ppp_stat_add_rx_hook)(struct ppp_channel *chan, u32 add_pkt,
			     u32 add_bytes) = NULL;
EXPORT_SYMBOL(ppp_stat_add_rx_hook);

#if IS_ENABLED(CONFIG_RA_HW_NAT)
void (*ppp_stats_update_hook)(struct net_device *dev,
			      u32 rx_bytes, u32 rx_packets,
			      u32 tx_bytes, u32 tx_packets) = NULL;
EXPORT_SYMBOL(ppp_stats_update_hook);

#if !defined(CONFIG_HNAT_V2)
void (*ppp_stat_block_hook)(struct net_device *dev, int is_block_rx) = NULL;
EXPORT_SYMBOL(ppp_stat_block_hook);
#endif
#endif

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

#if IS_ENABLED(CONFIG_FAST_NAT)
int (*fast_nat_hit_hook_func)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(fast_nat_hit_hook_func);

int (*fast_nat_bind_hook_func)(struct nf_conn *ct,
	enum ip_conntrack_info ctinfo,
	struct sk_buff *skb,
	u_int8_t protonum,
	struct nf_conntrack_l3proto *l3proto,
	struct nf_conntrack_l4proto *l4proto) = NULL;
EXPORT_SYMBOL(fast_nat_bind_hook_func);

int ipv4_fastnat_conntrack = 0;
EXPORT_SYMBOL(ipv4_fastnat_conntrack);

int (*fast_nat_bind_hook_ingress)(struct sk_buff * skb) = NULL;
EXPORT_SYMBOL(fast_nat_bind_hook_ingress);
#endif

#if IS_ENABLED(CONFIG_RA_HW_NAT)
int (*ra_sw_nat_hook_rx)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(ra_sw_nat_hook_rx);

int (*ra_sw_nat_hook_tx)(struct sk_buff *skb, int gmac_no) = NULL;
EXPORT_SYMBOL(ra_sw_nat_hook_tx);

void (*ppe_dev_register_hook)(struct net_device *dev) = NULL;
EXPORT_SYMBOL(ppe_dev_register_hook);

void (*ppe_dev_unregister_hook)(struct net_device *dev) = NULL;
EXPORT_SYMBOL(ppe_dev_unregister_hook);

void (*ppe_enable_hook)(int do_ppe_enable) = NULL;
EXPORT_SYMBOL(ppe_enable_hook);
#endif

#if defined(CONFIG_NTCE_MODULE)

unsigned int (*ntce_pass_pkt_func)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(ntce_pass_pkt_func);

void (*ntce_enq_pkt_hook_func)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(ntce_enq_pkt_hook_func);
#endif /* defined(CONFIG_NTCE_MODULE) */

