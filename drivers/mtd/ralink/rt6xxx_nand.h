#ifndef _RT6XXX_NAND_H
#define _RT6XXX_NAND_H

#include <linux/mtd/mtd.h>
#include <asm/rt2880/rt_mmap.h>

//#define RANFC_DEBUG

#define ECC_NO_ERR			0
#define ECC_ONE_BIT_ERR			-6
#define ECC_DATA_ERR			-7
#define ECC_CODE_ERR			-8
#define ECC_NFC_CONFLICT		-9

#define NFC_BASE			RALINK_NAND_CTRL_BASE
#define NFC_CTRL			(NFC_BASE + 0x00)
#define NFC_CONF			(NFC_BASE + 0x04)
#define NFC_CMD1			(NFC_BASE + 0x08)
#define NFC_CMD2			(NFC_BASE + 0x0c)
#define NFC_CMD3			(NFC_BASE + 0x10)
#define NFC_ADDR			(NFC_BASE + 0x14)
#define NFC_DATA			(NFC_BASE + 0x18)
#define NFC_STATUS			(NFC_BASE + 0x20)
#define NFC_INT_EN			(NFC_BASE + 0x24)
#define NFC_INT_ST			(NFC_BASE + 0x28)
#define NFC_CTRLII			(NFC_BASE + 0x2c)

#define NFC_ECC_BASE			RALINK_NANDECC_CTRL_BASE
#define NFC_ECC				(NFC_ECC_BASE + 0x00)
#define NFC_ECCII			(NFC_ECC_BASE + 0x04)
#define NFC_ECCIII			(NFC_ECC_BASE + 0x08)
#define NFC_ECCIV			(NFC_ECC_BASE + 0x0c)
#define NFC_ECC_ST			(NFC_ECC_BASE + 0x10)
#define NFC_ECC_STII			(NFC_ECC_BASE + 0x14)
#define NFC_ECC_STIII			(NFC_ECC_BASE + 0x18)
#define NFC_ECC_STIV			(NFC_ECC_BASE + 0x1c)
#define NFC_ADDRII			(NFC_ECC_BASE + 0x20)

enum _int_stat {
	INT_ST_ND_DONE		= 1<<0,
	INT_ST_TX_BUF_RDY	= 1<<1,
	INT_ST_RX_BUF_RDY	= 1<<2,
	INT_ST_ECC_ERR		= 1<<3,
	INT_ST_TX_TRAS_ERR	= 1<<4,
	INT_ST_RX_TRAS_ERR	= 1<<5,
	INT_ST_TX_KICK_ERR	= 1<<6,
	INT_ST_RX_KICK_ERR	= 1<<7
};

/* Status bits */
#define NAND_STATUS_FAIL		0x01
#define NAND_STATUS_FAIL_N1		0x02
#define NAND_STATUS_TRUE_READY		0x20
#define NAND_STATUS_READY		0x40
#define NAND_STATUS_WP			0x80

typedef enum {
	FL_READY,
	FL_READING,
	FL_WRITING,
	FL_ERASING,
	FL_SYNCING,
	FL_CACHEDPRG,
	FL_PM_SUSPENDED,
} nand_state_t;

/*************************************************************/

typedef enum _ra_flags {
	FLAG_NONE	= 0,
	FLAG_ECC_EN	= (1<<0),
	FLAG_USE_GDMA	= (1<<1),
	FLAG_VERIFY	= (1<<2),
} RA_FLAGS;

#define BBTTAG_BITS		2
#define BBTTAG_BITS_MASK	((1u << BBTTAG_BITS) - 1)
enum BBT_TAG {
	BBT_TAG_UNKNOWN = 0, //2'b01
	BBT_TAG_GOOD	= 3, //2'b11
	BBT_TAG_BAD	= 2, //2'b10
	BBT_TAG_RES	= 1, //2'b01
};

struct nand_opcode {
	const int type;
	const int read1;
	const int read2;
	const int readB;
	const int readoob;
	const int pageprog1;
	const int pageprog2;
	const int writeoob;
	const int erase1;
	const int erase2;
	const int status;
	const int reset;
};

struct nand_info {
	const int mfr_id;
	const int dev_id;
	const char *name;
	const int numchips;
	const int chip_shift;
	const int page_shift;
	const int erase_shift;
	const int oob_shift;
	const int badblockpos;
	const int opcode_type;
};

struct ra_nand_chip {
	struct nand_info *flash;
	struct mutex hwcontrol;
	struct mutex *controller;
	struct nand_ecclayout *oob;
	struct nand_opcode *opcode;
	int state;
	unsigned int buffers_page;
	unsigned char *buffers;		//[CFG_PAGESIZE + CFG_PAGE_OOBSIZE];
	unsigned char *readback_buffers;
	unsigned char *bbt;
};

#endif /* _RT6XXX_NAND_H */
