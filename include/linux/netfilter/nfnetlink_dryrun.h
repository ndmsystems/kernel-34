#ifndef _NFNL_DRYRUN_H_
#define _NFNL_DRYRUN_H_

enum nfnl_dryrun_msg_types {
	NFNL_MSG_DRYRUN_TEST,
	NFNL_MSG_DRYRUN_MAX
};

enum nfnl_dryrun_type {
	NFDRYRUN_UNSPEC,
	NFDRYRUN_SRC_V4,
	NFDRYRUN_DST_V4,
	NFDRYRUN_DST_PORT,
	NFDRYRUN_IFNAME,
	NFDRYRUN_FILTER_RES,
	__NFDRYRUN_MAX
};
#define NFDRYRUN_MAX (__NFDRYRUN_MAX - 1)

#ifdef __KERNEL__

#endif /* __KERNEL__ */

#endif /* _NFNL_DRYRUN_H_ */
