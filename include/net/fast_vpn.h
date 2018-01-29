#ifndef __FAST_VPN_H_
#define __FAST_VPN_H_

#include <linux/list.h>

#define FAST_VPN_ACTION_SETUP		1
#define FAST_VPN_ACTION_RELEASE		0

#define FAST_VPN_RES_OK			1
#define FAST_VPN_RES_SKIPPED	0

/* SWNAT section */

#define SWNAT_ORIGIN_RAETH		0x10
#define SWNAT_ORIGIN_RT2860		0x20
#define SWNAT_ORIGIN_USB_MAC	0x30

#define SWNAT_CB_OFFSET		46

#define SWNAT_FNAT_MARK		0x01
#define SWNAT_PPP_MARK		0x02
#define SWNAT_KA_MARK		0x04

/* FNAT mark */

#define SWNAT_FNAT_SET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = SWNAT_FNAT_MARK; \
} while (0);

#define SWNAT_FNAT_CHECK_MARK(skb_) \
	((skb_)->cb[SWNAT_CB_OFFSET] == SWNAT_FNAT_MARK)

#define SWNAT_FNAT_RESET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = 0; \
} while (0);

/* End of FNAT mark */

/* PPP mark */

#define SWNAT_PPP_SET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = SWNAT_PPP_MARK; \
} while (0);

#define SWNAT_PPP_CHECK_MARK(skb_) \
	((skb_)->cb[SWNAT_CB_OFFSET] == SWNAT_PPP_MARK)

#define SWNAT_PPP_RESET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = 0; \
} while (0);

/* End of PPP mark */

/* KA mark */

#define SWNAT_KA_SET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = SWNAT_KA_MARK; \
} while (0);

#define SWNAT_KA_CHECK_MARK(skb_) \
	((skb_)->cb[SWNAT_CB_OFFSET] == SWNAT_KA_MARK)

#define SWNAT_KA_RESET_MARK(skb_) \
do { \
	(skb_)->cb[SWNAT_CB_OFFSET] = 0; \
} while (0);


/* End of KA mark */

/* List of new MC streams */

struct new_mc_streams {
	u32 group_addr;
	struct net_device * out_dev;
	u32 handled;

	struct list_head list;
};


#endif //__FAST_VPN_H_
