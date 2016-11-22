#ifndef _XT_NDMMARK_H
#define _XT_NDMMARK_H

#include <linux/types.h>

struct xt_ndmmark_tginfo {
	__u8 mark, mask;
};

struct xt_ndmmark_mtinfo {
	__u8 mark, mask;
	__u8 invert;
};

#endif /* _XT_NDMMARK_H */
