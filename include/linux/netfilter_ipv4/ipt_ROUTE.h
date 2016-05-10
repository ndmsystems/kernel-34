/* Header file for iptables ipt_ROUTE target
 *
 * (C) 2002 by Cedric de Launois <delaunois@info.ucl.ac.be>
 *
 * This software is distributed under GNU GPL v2, 1991
 */
#ifndef _IPT_ROUTE_H_target
#define _IPT_ROUTE_H_target

struct ipt_route_target_info {
	char      oif[IFNAMSIZ];      /* Output Interface Name */
	char      iif[IFNAMSIZ];      /* Input Interface Name  */
	u_int32_t gw;                 /* IP address of gateway */
	u_int8_t  flags;
};

/* Values for "flags" field */
#define IPT_ROUTE_CONTINUE        0x01
#define IPT_ROUTE_TEE             0x02

#endif /*_IPT_ROUTE_H_target*/
