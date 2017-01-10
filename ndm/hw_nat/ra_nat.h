/*
    Module Name:
    ra_nat.h

    Abstract:

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
    Name        Date            Modification logs
    Steven Liu  2006-10-06      Initial version
*/

#ifndef _RA_NAT_WANTED
#define _RA_NAT_WANTED

#include <linux/ip.h>
#include <linux/ipv6.h>

typedef struct {
	uint16_t MAGIC_TAG;
#if defined(CONFIG_RALINK_MT7620)
	uint32_t FOE_Entry:14;
	uint32_t CRSN:5;
	uint32_t SPORT:3;
	uint32_t ALG:10;
#elif defined(CONFIG_RALINK_MT7621) || defined(CONFIG_ARCH_MT7623)
	uint32_t FOE_Entry:14;
	uint32_t CRSN:5;
	uint32_t SPORT:4;
	uint32_t ALG:9;
#else
#ifdef __BIG_ENDIAN
	uint32_t RESV2:4;
	uint32_t AIS:1;
	uint32_t SP:3;
	uint32_t AI:8;
	uint32_t ALG:1;
	uint32_t FVLD:1;
	uint32_t FOE_Entry:14;
#else
	uint32_t FOE_Entry:14;
	uint32_t FVLD:1;
	uint32_t ALG:1;
	uint32_t AI:8;
	uint32_t SP:3;
	uint32_t AIS:1;
	uint32_t RESV2:4;
#endif
#endif
}  __attribute__ ((packed)) PdmaRxDescInfo4;

/*
 * DEFINITIONS AND MACROS
 */

/*
 *    2bytes         4bytes
 * +-----------+-------------------+
 * | Magic Tag | RX/TX Desc info4  |
 * +-----------+-------------------+
 * |<------FOE Flow Info---------->|
 */
#define FOE_INFO_LEN			6
#define FOE_MAGIC_EXTIF			0x7274
#define FOE_MAGIC_PCI			FOE_MAGIC_EXTIF
#define FOE_MAGIC_WLAN			FOE_MAGIC_EXTIF
#define FOE_MAGIC_GE			0x7275
#define FOE_MAGIC_PPE			0x7276
#ifdef __BIG_ENDIAN
#define FOE_MAGIC_PPE_DWORD		0x7672ff3fUL	/* HNAT_V1: FVLD=0, HNAT_V2: FOE_Entry=0x3fff */
#else
#define FOE_MAGIC_PPE_DWORD		0x3fff7276UL	/* HNAT_V1: FVLD=0, HNAT_V2: FOE_Entry=0x3fff */
#endif

/* gmac_no for EXTIF offload (ra_sw_nat_hook_tx) */
#define GMAC_ID_MAGIC_EXTIF		0

/* choose one of them to keep HNAT related information in somewhere. */
//#define HNAT_USE_HEADROOM
//#define HNAT_USE_TAILROOM
#define HNAT_USE_SKB_CB

#if defined(HNAT_USE_HEADROOM)

#define IS_SPACE_AVAILABLED(skb)	((skb_headroom(skb) >= FOE_INFO_LEN) ? 1 : 0)
#define FOE_INFO_START_ADDR(skb)	(skb->head)

#elif defined(HNAT_USE_TAILROOM)

#define IS_SPACE_AVAILABLED(skb)	((skb_tailroom(skb) >= FOE_INFO_LEN) ? 1 : 0)
#define FOE_INFO_START_ADDR(skb)	(skb->end - FOE_INFO_LEN)

#elif defined(HNAT_USE_SKB_CB)

//change the position of skb_CB if necessary
#define CB_OFFSET			40
#define IS_SPACE_AVAILABLED(skb)	1
#define FOE_INFO_START_ADDR(skb)	(skb->cb + CB_OFFSET)

#endif

#define FOE_MAGIC_TAG(skb)		((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->MAGIC_TAG
#define FOE_ENTRY_NUM(skb)		((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->FOE_Entry
#define FOE_ALG(skb)			((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->ALG
#if defined(CONFIG_HNAT_V2)
#define FOE_ENTRY_VALID(skb)		(((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->FOE_Entry != 0x3fff)
#define FOE_AI(skb)			((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->CRSN
#define FOE_SP(skb)			((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->SPORT
#ifndef UN_HIT
#define UN_HIT 0x0D
#endif
#ifndef HIT_BIND_KEEPALIVE_DUP_OLD_HDR
#define HIT_BIND_KEEPALIVE_DUP_OLD_HDR 0x15
#endif 
#else
#define FOE_ENTRY_VALID(skb)		((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->FVLD
#define FOE_AI(skb)			((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->AI
#define FOE_SP(skb)			((PdmaRxDescInfo4 *)FOE_INFO_START_ADDR(skb))->SP
#ifndef UN_HIT
#define UN_HIT 0x93
#endif
#ifndef HIT_BIND_KEEPALIVE
#define HIT_BIND_KEEPALIVE 0x98
#endif
#endif

#define IS_MAGIC_TAG_VALID(skb) \
	((FOE_MAGIC_TAG(skb) == FOE_MAGIC_GE))

#define IS_DPORT_PPE_VALID(skb) \
	(*(uint32_t *)(FOE_INFO_START_ADDR(skb)) == FOE_MAGIC_PPE_DWORD)

/* mark flow need skipped from PPE */
#define FOE_ALG_SKIP(skb) \
	if (IS_SPACE_AVAILABLED(skb) && !FOE_ALG(skb) && IS_MAGIC_TAG_VALID(skb)) FOE_ALG(skb)=1

#define FOE_ALG_MARK(skb) \
	FOE_ALG_SKIP(skb)

#if defined(CONFIG_HNAT_V2)
#define FOE_SKB_IS_KEEPALIVE(skb) \
	(FOE_AI(skb) == HIT_BIND_KEEPALIVE_DUP_OLD_HDR)
#else
#define FOE_SKB_IS_KEEPALIVE(skb) \
	(FOE_AI(skb) == HIT_BIND_KEEPALIVE)
#endif

/* reset AI for local output flow */
#define FOE_AI_UNHIT(skb) \
	if (IS_SPACE_AVAILABLED(skb)) FOE_AI(skb)=UN_HIT

/* fast clear FoE Info (magic_tag,entry_num) */
#define DO_FAST_CLEAR_FOE(skb) \
	(*(uint32_t *)(FOE_INFO_START_ADDR(skb)) = 0U)

/* full clear FoE Info */
#define DO_FULL_CLEAR_FOE(skb) \
	(memset(FOE_INFO_START_ADDR(skb), 0, FOE_INFO_LEN))

/* fast fill FoE desc field */
#define DO_FILL_FOE_DESC(skb,desc) \
	(*(uint32_t *)(FOE_INFO_START_ADDR(skb)+2) = (uint32_t)(desc))

/* fast fill FoE desc to DPORT PPE (magic_tag,entry_num) */
#define DO_FILL_FOE_DPORT_PPE(skb) \
	(*(uint32_t *)(FOE_INFO_START_ADDR(skb)) = FOE_MAGIC_PPE_DWORD)

//////////////////////////////////////////////////////////////////////

extern int (*ra_sw_nat_hook_rx)(struct sk_buff *skb);
extern int (*ra_sw_nat_hook_tx)(struct sk_buff *skb, int gmac_no);
extern void (*ppe_dev_register_hook)(struct net_device *dev);
extern void (*ppe_dev_unregister_hook)(struct net_device *dev);
extern void (*ppe_enable_hook)(int do_ppe_enable);

//////////////////////////////////////////////////////////////////////

#endif
