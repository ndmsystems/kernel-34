#ifndef _XT_NDMMARK_H
#define _XT_NDMMARK_H

#include <linux/types.h>

/* Must be in-sync with ndm/Netfilter/Typedefs.h */

enum xt_ndmmark_list {
	XT_NDMMARK_EMPTY            = 0x0,
	XT_NDMMARK_IPSEC_INPUT      = 0x1,
	XT_NDMMARK_IPSEC_OUTPUT     = 0x2,
	XT_NDMMARK_DISCOVERY_DROP   = 0x3
};

struct xt_ndmmark_tginfo {
	__u8 mark, mask;
};

struct xt_ndmmark_mtinfo {
	__u8 mark, mask;
	__u8 invert;
};

#endif /* _XT_NDMMARK_H */
