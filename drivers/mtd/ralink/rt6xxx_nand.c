#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/mtd/partitions.h>

#include <ralink/ralink_gpio.h>

#include "rt6xxx_nand.h"
#ifdef RALINK_NAND_BMT
#include "rt6xxx_bmt.h"
#endif
#include "ralink-flash.h"

#include <asm/tc3162/tc3162.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
#define MTD_OPS_PLACE_OOB	MTD_OOB_PLACE
#define MTD_OPS_AUTO_OOB	MTD_OOB_AUTO
#define MTD_OPS_RAW		MTD_OOB_RAW
#endif

#define CONFIG_NUMCHIPS		1
#define RETRY_NUMBER		1000000
#define BLOCK_ALIGNED(a)	((a) & (blocksize - 1))

static const char *part_probes[] __initdata = { "ndmpart", NULL };

struct mtd_info *ranfc_mtd = NULL;

static int ranfc_bbt = 1;
static int ranfc_page = 0;
static int column_addr_cycle = 0;
static int row_addr_cycle = 0;
static int addr_cycle = 0;

#ifdef RALINK_NAND_BMT
#define BMT_BAD_BLOCK_INDEX_OFFSET	(1)
#define POOL_GOOD_BLOCK_PERCENT		8/100
#define SLAVE_IMAGE_OFFSET		0xf00000
#define SIZE_2KiB_BYTES			(2048)
#define SIZE_64iB_BYTES			(64)

extern int nand_logic_size;
static int bmt_pool_size = 0;
static bmt_struct *g_bmt = NULL;
static init_bbt_struct *g_bbt = NULL;
static char write_ops_dat[SIZE_2KiB_BYTES + SIZE_64iB_BYTES];

static int calc_bmt_pool_size(struct ra_nand_chip *ra);
#endif

module_param(ranfc_bbt, int, 0644);

#ifdef RANFC_DEBUG
static int ranfc_debug = 1;
module_param(ranfc_debug, int, 0644);

#define ra_dbg(args...) do { if (ranfc_debug) printk(args); } while(0)
#else
#define ra_dbg(args...)
#endif

#define ra_inl(addr)			(*(volatile unsigned int *)(addr))
#define ra_outl(addr,value)		(*(volatile unsigned int *)(addr) = (value))
#define ra_aor(addr,a_mask,o_value)	ra_outl(addr,(ra_inl(addr) & (a_mask)) | (o_value))
#define ra_and(addr,a_mask)		ra_aor(addr,a_mask,0)
#define ra_or(addr,o_value)		ra_aor(addr,-1,o_value)

#define CLEAR_INT_STATUS()		ra_outl(NFC_INT_ST, ra_inl(NFC_INT_ST))
#define NFC_TRANS_DONE()		(ra_inl(NFC_INT_ST) & INT_ST_ND_DONE)

/* NAND chips support table */
#include "rt6xxx_nand_table.h"

/*************************************************************
 * nfc functions 
 *************************************************************/
static int nfc_wait_ready(bool allow_reset);
static int nfc_read_page(struct ra_nand_chip *ra, unsigned char *buf, int page, int flags);
static int nfc_write_page(struct ra_nand_chip *ra, unsigned char *buf, int page, int flags);

/**
 * reset nand chip
 */
static int
nfc_chip_reset(void)
{
	int status;
	unsigned int hwconf, nfc_conf;

	nfc_conf = 0x0101;

	/* read bootstrap NAND page info */
	if (addr_cycle == 0) {
		hwconf = ra_inl(RALINK_SYSCTL_BASE + 0x8c);
		if (hwconf & (1 << 19))
			nfc_conf |= (0x5 << 16);
		else
			nfc_conf |= (0x4 << 16);
	} else {
		nfc_conf |= (addr_cycle << 16);
	}

	// reset nand flash
	ra_outl(NFC_CMD1, 0xff);
	ra_outl(NFC_CONF, nfc_conf);

	status = nfc_wait_ready(false);
	if (status & NAND_STATUS_FAIL)
		printk(KERN_ERR "%s: fail!\n", __func__);

	return (int)(status & NAND_STATUS_FAIL);
}

/**
 * clear NFC and flash chip.
 */
static int
nfc_all_reset(void)
{
	long retry;

	ra_dbg("%s: \n", __func__);

	/* clear data buffer */
	ra_or(NFC_CTRL, 0x02);
	ra_and(NFC_CTRL, ~0x02);

	CLEAR_INT_STATUS();

	retry = RETRY_NUMBER;
	while ((ra_inl(NFC_INT_ST) & 0x02) != 0x02 && retry--)
		;

	if (retry <= 0) {
		printk(KERN_ERR "%s: clean buffer fail!\n", __func__);
		return -1;
	}

	retry = RETRY_NUMBER;
	while ((ra_inl(NFC_STATUS) & 0x1) != 0x0 && retry--)
		;

	/* reset nand chip */
	nfc_chip_reset();

	return 0;
}

/** NOTICE: only called by nfc_wait_ready().
 * @return -1, nfc can not get transction done 
 * @return 0, ok.
 */
static int
_nfc_read_status(char *status)
{
	unsigned int int_st, nfc_st, chip_st;
	long retry;

	//fixme, should we check nfc status?
	CLEAR_INT_STATUS();

	ra_outl(NFC_CMD1, 0x70);
	ra_outl(NFC_CONF, 0x0101 | (1 << 20));

	/* FIXME,
	 * 1. since we have no wired ready signal, directly 
	 * calling this function is not gurantee to read right status under ready state.
	 * 2. the other side, we can not determine how long to become ready, this timeout retry is nonsense.
	 * 3. SUGGESTION: call nfc_read_status() from nfc_wait_ready(),
	 * that is aware about caller (in sementics) and has snooze plused nfc ND_DONE.
	 */
	retry = RETRY_NUMBER;
	do {
		nfc_st = ra_inl(NFC_STATUS);
		int_st = ra_inl(NFC_INT_ST);
	} while (!(int_st & INT_ST_RX_BUF_RDY) && retry--);

	if (!(int_st & INT_ST_RX_BUF_RDY)) {
		printk(KERN_ERR "%s: NFC fail, int_st(%x), nfc:%x, reset nfc and flash.\n",
			__func__, int_st, nfc_st);
		*status = NAND_STATUS_FAIL;
		return -1;
	}

	chip_st = ra_inl(NFC_DATA);
#ifdef __BIG_ENDIAN
	chip_st >>= 24;
#endif
	*status = (char)(chip_st & 0xff);

	return 0;
}


/**
 * @return !0, chip protect.
 * @return 0, chip not protected.
 */
static inline int
nfc_check_wp(struct ra_nand_chip *ra)
{
	/* Check the WP bit */
	return !(ra_inl(NFC_CTRL) & 0x01);
}

/*
 * @return !0, chip ready.
 * @return 0, chip busy.
 */
static inline int
nfc_device_ready(void)
{
	/* Check the ready  */
	return (ra_inl(NFC_STATUS) & 0x04);
}

static int
nfc_read_id(void)
{
	long retry;
	unsigned int ret_data = 0;

	CLEAR_INT_STATUS();

	ra_outl(NFC_CMD1, 0x90);
	ra_outl(NFC_CONF, 0x410101);

	retry = RETRY_NUMBER;
	while (retry > 0) {
		if (ra_inl(NFC_INT_ST) & INT_ST_RX_BUF_RDY) {
			ret_data = ra_inl(NFC_DATA);
			break;
		}
		retry--;
	}

	if (retry <= 0) {
		printk(KERN_ERR "%s: read id fail \n", __func__);
		return -1;
	}

	return (int)ret_data;
}

/**
 * generic function to get data from flash.
 * @return data length reading from flash.
 */
static int
_ra_nand_pull_data(unsigned char *buf, int len)
{
	unsigned char *p = buf;
	long retry;

	retry = RETRY_NUMBER;

	while (len > 0) {
		unsigned int int_st;

		int_st = ra_inl(NFC_INT_ST);
		if (int_st & INT_ST_RX_BUF_RDY) {
			unsigned int ret_data;
			int ret_size, iter;

			ret_data = ra_inl(NFC_DATA);
			ra_outl(NFC_INT_ST, INT_ST_RX_BUF_RDY);

			ret_size = min(len, 4);
			len -= ret_size;

			if (ret_size == 4) {
				*(unsigned int *)p = ret_data;
				p += 4;
			} else {
				for (iter = 0; iter < ret_size; iter++) {
#ifdef __BIG_ENDIAN
					*p++ = (ret_data >> (8 * (3 - iter))) & 0xff;
#else
					*p++ = (ret_data >> (8 * iter)) & 0xff;
#endif
				}
			}
			retry = RETRY_NUMBER;
		} else if (int_st & INT_ST_ND_DONE) {
			break;
		} else {
			if (retry-- < 0)
				break;
		}
	}

	return (p - buf);
}

/**
 * generic function to put data into flash.
 * @return data length writing into flash.
 */
static int
_ra_nand_push_data(unsigned char *buf, int len)
{
	unsigned char *p = buf;
	long retry;

	retry = RETRY_NUMBER;

	while (len > 0) {
		unsigned int int_st;

		int_st = ra_inl(NFC_INT_ST);
		if (int_st & INT_ST_TX_BUF_RDY) {
			unsigned int tx_data;
			int tx_size, iter;

			tx_size = min(len, 4);
			len -= tx_size;

			if (tx_size == 4) {
				tx_data = *(unsigned int *)p;
				p += tx_size;
			} else {
				tx_data = 0;
				for (iter = 0; iter < tx_size; iter++) {
					unsigned int tx_byte = *p++;
#ifdef __BIG_ENDIAN
					tx_data |= (tx_byte << (8 * (3 - iter)));
#else
					tx_data |= (tx_byte << (8 * iter));
#endif
				}
			}
			ra_outl(NFC_INT_ST, INT_ST_TX_BUF_RDY);
			ra_outl(NFC_DATA, tx_data);

			/* need test status before len loop breaked */
			if (len == 0) {
				udelay(1);
				int_st = ra_inl(NFC_INT_ST);
			}
			retry = RETRY_NUMBER;
		}

		if (int_st & INT_ST_ND_DONE) {
			break;
		} else {
			if (retry-- < 0) {
				ra_dbg("%s p:%p buf:%p \n", __func__, p, buf);
				break;
			}
		}
	}

	return (int)(p - buf);
}

static inline int
nfc_select_chip(struct ra_nand_chip *ra, int chipnr)
{
	if (!(chipnr < CONFIG_NUMCHIPS))
		return -1;
	return 0;
}

/** @return -1: chip_select fail
 *	    0 : both CE and WP==0 are OK
 * 	    1 : CE OK and WP==1
 */
static int
nfc_enable_chip(struct ra_nand_chip *ra, loff_t offs, int read_only)
{
	int chipnr = (int)(offs >> ra->flash->chip_shift);

	chipnr = nfc_select_chip(ra, chipnr);
	if (chipnr < 0)
		return -1;

	if (!read_only)
		return nfc_check_wp(ra);

	return 0;
}


/** wait nand chip becomeing ready and return queried status.
 * @param snooze: sleep time in ms unit before polling device ready.
 * @return status of nand chip
 * @return NAN_STATUS_FAIL if something unexpected.
 */
static int
nfc_wait_ready(bool allow_reset)
{
	long retry;
	char status = 0;

	retry = RETRY_NUMBER;
	while (!NFC_TRANS_DONE() && retry--)
		;

	if (!NFC_TRANS_DONE()) {
		printk(KERN_ERR "%s: no transaction done \n", __func__);
		return NAND_STATUS_FAIL;
	}

	retry = RETRY_NUMBER;
	while(!(status = nfc_device_ready()) && retry--)
		;

	if (status == 0) {
		printk(KERN_ERR "%s: no device ready.\n", __func__);
		return NAND_STATUS_FAIL;
	}

	retry = RETRY_NUMBER;
	while (retry--) {
		if (_nfc_read_status(&status) < 0) {
			if (allow_reset)
				nfc_all_reset();
			break;
		}
		if (status & NAND_STATUS_READY)
			break;
	}

	return status;
}

/**
 * return 0: erase OK
 * return -EIO: fail 
 */
static int
nfc_erase_block(struct ra_nand_chip *ra, int row_addr)
{
	unsigned long cmd1, cmd2, conf;
	char status;

	cmd1 = ra->opcode->erase1;
	cmd2 = ra->opcode->erase2;

	if (ra->flash->page_shift == SIZE_512iB_BIT)
		conf = 0x00513 | ((row_addr_cycle) << 16);
	else
		conf = 0x00503 | ((row_addr_cycle) << 16);

	ra_dbg("Erase CMD1:%2lx\n", cmd1);
	ra_dbg("Erase CMD2:%2lx\n", cmd2);
	ra_dbg("Erase BLOCK:%2x\n", row_addr);
	ra_dbg("CONFIG:%5lx\n", conf);

	// set NFC

	//fixme, should we check nfc status?
	CLEAR_INT_STATUS();

	ra_outl(NFC_CMD1, cmd1);
	ra_outl(NFC_CMD2, cmd2);
	ra_outl(NFC_ADDR, row_addr);
	ra_outl(NFC_CONF, conf);

	status = nfc_wait_ready(true);  //erase wait 3ms 
	if (status & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return -EIO;
	}

	return 0;
}

static inline int
_nfc_write_raw_data(struct ra_nand_chip *ra, int cmd1, int cmd3, unsigned long row_addr,
		    unsigned long column_addr, int conf, unsigned char *buf, int len, int flags)
{
	int ret;

	ra_dbg("In _nfc_write_raw_data function\n");
	ra_dbg("NFC_CMD1:%x\n", cmd1);
	ra_dbg("NFC_CMD3:%x\n", cmd3);
	ra_dbg("ROW_ADDR:%lx\n", row_addr);
	ra_dbg("COLUMN_ADDR:%lx\n", column_addr);
	ra_dbg("NFC_CONF:%x\n", conf);

	CLEAR_INT_STATUS();

	ra_outl(NFC_CMD1, cmd1);
	ra_outl(NFC_CMD3, cmd3);

	if (ra->flash->page_shift == SIZE_2KiB_BIT) {
		ra_outl(NFC_ADDR, (row_addr << (column_addr_cycle << 3)) | column_addr );
		ra_outl(NFC_ADDRII, ((row_addr >> (32 - (column_addr_cycle << 3)))));
	} else {
		ra_outl(NFC_ADDR, (row_addr << (column_addr_cycle << 3)) | column_addr );
	}

	ra_outl(NFC_CONF, conf);

	ret = _ra_nand_push_data(buf, len);
	if (ret != len) {
		ra_dbg("%s: ret:%x (%x) \n", __func__, ret, len);
		return NAND_STATUS_FAIL;
	}

	ret = nfc_wait_ready(true); //write wait 1ms
	if (ret & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return NAND_STATUS_FAIL;
	}

	return 0;
}

static inline int
_nfc_read_raw_data(struct ra_nand_chip *ra, int cmd1, int cmd2, unsigned long row_addr, 
		   unsigned long column_addr, int conf, unsigned char *buf, int len, int flags)
{
	int ret;

	ra_dbg("in _nfc_read_raw_data function\n");
	ra_dbg("NFC_CMD1: %x\n", cmd1);
	ra_dbg("cmd1: %x\n", cmd1);
	ra_dbg("row_addr: %lx\n", row_addr);
	ra_dbg("column_addr: %lx\n", column_addr);
	ra_dbg("NFC_ADDR: %lx\n", (row_addr << (column_addr_cycle << 3)) | column_addr );
	ra_dbg("NFC_ADDRII: %lx\n",((row_addr >> (32 - (column_addr_cycle << 3)))));
	ra_dbg("conf: %x\n", conf);
	ra_dbg("len : %x\n", len);

	CLEAR_INT_STATUS();
	ra_outl(NFC_CMD1, cmd1);

	if (ra->flash->page_shift == SIZE_2KiB_BIT) {
		ra_outl(NFC_CMD2, cmd2);
		ra_outl(NFC_ADDR, (row_addr << (column_addr_cycle << 3)) | column_addr );
		ra_outl(NFC_ADDRII, ((row_addr >> (32 - (column_addr_cycle << 3)))));
	} else {
		ra_outl(NFC_ADDR, (row_addr << (column_addr_cycle << 3)) | column_addr );
	}

	ra_outl(NFC_CONF, conf);

	ret = _ra_nand_pull_data(buf, len);
	if (ret != len) {
		ra_dbg("%s: ret:%x (%x) \n", __func__, ret, len);
		return NAND_STATUS_FAIL;
	}

	ret = nfc_wait_ready(true); //wait ready
	if (ret & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return NAND_STATUS_FAIL;
	}

	return 0;
}

/**
 * @return !0: fail
 * @return 0: OK
 */
static int
nfc_read_oob(struct ra_nand_chip *ra, int page, unsigned int offs, unsigned char *buf, int len, int flags)
{
	unsigned int cmd1 = 0, cmd2 = 0, conf = 0;
	unsigned long row_addr, column_addr;
	int status;
	int pages_perblock = 1u << (ra->flash->erase_shift - ra->flash->page_shift);

	// constrain of nfc read function 
	BUG_ON (offs >> ra->flash->oob_shift); //page boundry
	BUG_ON ((unsigned int)(((offs + len) >> ra->flash->oob_shift) + page) >
		((page + pages_perblock) & ~(pages_perblock-1))); //block boundry

	row_addr = page;
	column_addr = offs & ((1u << (column_addr_cycle << 3)) -1);

	if (ra->flash->page_shift == SIZE_512iB_BIT) {
		cmd1 = ra->opcode->readoob;
		conf = 0x000141| ((addr_cycle) << 16) | ((len) << 20);
	} else {
		cmd1 = ra->opcode->read1;
		cmd2 = ra->opcode->read2;
		conf = 0x000511| ((addr_cycle) << 16) | ((len) << 20);
		column_addr |= (1 << 11);
	}

	if (flags & FLAG_ECC_EN)
		conf |= (1 << 3);

	status = _nfc_read_raw_data(ra, cmd1, cmd2, row_addr, column_addr, conf, buf, len, flags);
	if (status & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return -EIO;
	}

	return 0; 
}

/**
 * @return !0: fail
 * @return 0: OK
 */
static int
nfc_write_oob(struct ra_nand_chip *ra, int page, unsigned int offs, unsigned char *buf, int len, int flags)
{
	unsigned int cmd1 = 0, cmd3=0, conf = 0;
	unsigned long row_addr, column_addr;
	int status;
	int pages_perblock = 1u << (ra->flash->erase_shift - ra->flash->page_shift);

	// constrain of nfc read function 
	BUG_ON(offs >> ra->flash->oob_shift); //page boundry
	BUG_ON((unsigned int)(((offs + len) >> ra->flash->oob_shift) + page) >
		((page + pages_perblock) & ~(pages_perblock-1))); //block boundry

	row_addr = page;
	column_addr = offs & ((1u << (column_addr_cycle << 3)) - 1);

	cmd1 = ra->opcode->writeoob;
	cmd3 = ra->opcode->pageprog2;

	if (ra->flash->page_shift == SIZE_512iB_BIT) {
		conf = 0x001243 | ((addr_cycle) << 16) | ((len) << 20);
	} else {
		conf = 0x001103 | ((addr_cycle) << 16) | ((len) << 20);
		column_addr |= (1 << 11);
	}

	// set NFC
	ra_dbg("%s: cmd1: %x, cmd3: %x row_addr: %lx, column_addr: %lx, conf: %x, len:%x\n", 
	       __func__, cmd1, cmd3, row_addr, column_addr, conf, len);

	status = _nfc_write_raw_data(ra, cmd1, cmd3, row_addr, column_addr, conf, buf, len, flags);
	if (status & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return -EIO;
	}

	return 0;
}

/*
for 512 byte/page
	return:
		ECC_NO_ERR: no error
		ECC_CODE_ERR: ECC code error
		ECC_DATA_ERR: more than 1 bit error, un-correctable
		ECC_ONE_BIT_ERR: 1 bit correctable error
		ECC_NFC_CONFLICT: software check result conflict with HW check result
*/
static int
nfc_ecc_err_handler(int page_index , unsigned char *ecc_from_oob,
		    unsigned char *ecc_from_nfc, unsigned long *error_byte_index,
		    unsigned long *error_bit_index)
{
	unsigned long old_ecc = 0;
	unsigned long new_ecc = 0;
	unsigned long ecc_rst = 0;
	int ecc_bit_index = 0;
	int ecc_bit1_cnt = 0;
	unsigned long temp = 0;

	memcpy((unsigned char *)&old_ecc + 1 , ecc_from_oob , 3);
	memcpy((unsigned char *)&new_ecc + 1 , ecc_from_nfc , 3);

	ecc_rst = old_ecc ^ new_ecc;

	if (ecc_rst == 0) {//no ecc error
		return ECC_NO_ERR;
	} else {
		for (ecc_bit_index = 0; ecc_bit_index< 24; ecc_bit_index++ ) {
			if ((ecc_rst & (1u << ecc_bit_index)) != 0)
				ecc_bit1_cnt++;
		}

		printk(KERN_WARNING "%s: ecc_rst=0x%08lx, ecc_bit1_cnt=%d\n",
			__func__, ecc_rst, ecc_bit1_cnt);

		if (ecc_bit1_cnt == 1) {//ECC code error
			return ECC_CODE_ERR;
		} else if (ecc_bit1_cnt != 12) {//more than 1 bit error, un-correctable
			printk(KERN_WARNING "more than one bit ECC error\n");
			return ECC_DATA_ERR;
		} else if (ecc_bit1_cnt == 12) {// 1 bit correctable error, get error bit
			temp = ra_inl(NFC_ECC_ST + page_index * 4);
			if (unlikely((temp & 0x1) == 0)) {
				printk(KERN_WARNING "ECC result conflict!\n");
				return ECC_NFC_CONFLICT;
			}
			*error_byte_index = ((temp >> 6) & 0x1ff);
			*error_bit_index = ((temp >> 2) & 0x7);
			printk(KERN_WARNING "correctable ECC error, error_byte_index=%lu, error_bit_index=%lu",
					*error_byte_index, *error_bit_index);
			return ECC_ONE_BIT_ERR;
		}
	}

	return ECC_NO_ERR;
}

/**
 * nfc_ecc_verify 
 return value:
 	0:    data OK or data correct OK
 	-1:  data ECC fail
 */
static int
nfc_ecc_verify(struct ra_nand_chip *ra, unsigned char *buf, int page, int mode)
{
	int ret, i, j, ecc_num;
	unsigned char *p, *e;
	unsigned long err_byte_index = 0;
	unsigned long err_bit_index = 0;
	int ecc_error_code = ECC_DATA_ERR;
	int ecc_ret = -1;

	/* 512 bytes data has a ecc value */
	int ecc_bytes, ecc_offset, ecc[4];
	unsigned char ecc_swap[3] = {0};
	unsigned char correct_byte = 0;

	if (mode == FL_WRITING) {
		p = ra->readback_buffers;
		ret = nfc_read_page(ra, ra->readback_buffers, page, FLAG_ECC_EN);
		if (ret == 0)
			goto ecc_check;

		//FIXME, double comfirm
		printk(KERN_WARNING "%s: read back fail, try again \n", __func__);
		ret = nfc_read_page(ra, ra->readback_buffers, page, FLAG_ECC_EN);
		if (ret != 0) {
			printk(KERN_WARNING "\t%s: read back fail agian \n", __func__);
			goto bad_block;
		}
	} else if (mode == FL_READING) {
		p = buf;
	} else {
		return -2;
	}

ecc_check:
	p += (1u << ra->flash->page_shift);

	ecc[0] = ra_inl(NFC_ECC);
	if (ecc[0] == 0) {
		//printk("clean page.\n");
		return 0;
	}

	ecc_bytes = ra->oob->eccbytes;
	ecc_offset = ra->oob->eccpos[0];

	/* each ecc register store 3 bytes ecc value */
	if (ecc_bytes == 12) {
		ecc[1] = ra_inl(NFC_ECCII);
		ecc[2] = ra_inl(NFC_ECCIII);
		ecc[3] = ra_inl(NFC_ECCIV);
	}

	ecc_num = ecc_bytes / 3;
	for (i = 0; i < ecc_num; i++) {
		e = (unsigned char*)&ecc[i];
		ecc_swap[0] = *((unsigned char*)&ecc[i]+3);
		ecc_swap[1] = *((unsigned char*)&ecc[i]+2);
		ecc_swap[2] = *((unsigned char*)&ecc[i]+1);
#ifdef RALINK_NAND_BMT
		ecc_offset = ra->oob->eccpos[i * 3];
#endif
		err_byte_index = 0;
		err_bit_index = 0;
		/* each ecc register store 3 bytes ecc value */
		ecc_ret = 0;

		for (j = 0; j < 3; j++) {
#ifdef __BIG_ENDIAN
#ifdef RALINK_NAND_BMT
			int eccpos = ecc_offset - j + 2;
#else
			int eccpos = ecc_offset - j + 2 + i * 3;
#endif
#else
#ifdef RALINK_NAND_BMT
			int eccpos = ecc_offset + j;
#else
			int eccpos = ecc_offset + j + i * 3;
#endif
#endif
			if (*(p + eccpos) != *(e + j + 1)) {
				printk("%s mode:%s, invalid ecc, page: %x read:%x %x %x, ecc:%x \n",
					   __func__, (mode == FL_READING)?"read":"write", page, 
#ifdef __BIG_ENDIAN
					   *(p+ecc_offset+2), *(p+ecc_offset+1), *(p+ecc_offset), ecc[i]);
#else
					   *(p+ecc_offset), *(p+ecc_offset+1), *(p+ecc_offset+2), ecc[i]);
#endif
				ecc_ret = -1;
				break;
			}
		}

		if (ecc_ret == -1) {
			ecc_error_code = nfc_ecc_err_handler(i , p+ecc_offset, ecc_swap, &err_byte_index, 
				&err_bit_index );
			if (ecc_error_code != ECC_NO_ERR) {
				printk("\n ecc_error_code=%d, page=%d, i=%d", ecc_error_code, page, i);
				if (ecc_error_code == ECC_ONE_BIT_ERR) {
					//correct the error
					printk("\n err_byte_index=%lu, err_bit_index=%lu",
							 err_byte_index , err_bit_index);
					correct_byte = buf[err_byte_index + i*512];
					if ((correct_byte & (1u << err_bit_index)) != 0) {
						correct_byte &= (~(1u << err_bit_index));
					} else {
						correct_byte |= (1u << err_bit_index);
					}
					buf[err_byte_index + i * 512] = correct_byte;
					ecc_ret = 0;
					ecc_error_code = ECC_NO_ERR;
					continue;
				}
				return ecc_error_code;
			}
		}
	}

	return 0;

bad_block:
	return -1;
}

/**
 * @return -EIO, writing size is less than a page 
 * @return 0, OK
 */
static int
nfc_read_page(struct ra_nand_chip *ra, unsigned char *buf, int page, int flags)
{
	unsigned int cmd1 = 0, cmd2 = 0, conf = 0;
	unsigned long column_addr, row_addr;
	int pagesize, size, offs;
	int status = 0;

	page = page & ((1u << ra->flash->chip_shift) - 1); // chip boundary
	pagesize = (1u << ra->flash->page_shift);

	if (flags & FLAG_ECC_EN)
		size = pagesize + (1u << ra->flash->oob_shift); //add oobsize
	else
		size = pagesize;

	offs = 0;

	while (size > 0) {
		int len;

		len = size;

		row_addr = page;
		column_addr = offs & ((1u << (column_addr_cycle << 3)) - 1);

		if (ra->flash->page_shift == SIZE_512iB_BIT) {
			if (unlikely((offs & ~((1u << ra->flash->page_shift) - 1))))
				cmd1 = ra->opcode->readoob;
			else if (offs & ~((1u << (column_addr_cycle << 3)) - 1))
				cmd1 = ra->opcode->readB;
			else
				cmd1 = ra->opcode->read1;
			conf = 0x000141 | ((addr_cycle) << 16) | (len << 20);
		} else {
			cmd1 = ra->opcode->read1;
			cmd2 = ra->opcode->read2;
			conf = 0x000511 | ((addr_cycle) << 16) | (len << 20);
		}

		if (flags & FLAG_ECC_EN)
			conf |= (1 << 3);

		status = _nfc_read_raw_data(ra, cmd1, cmd2, row_addr, column_addr, conf, buf+offs, len, flags);
		if (status & NAND_STATUS_FAIL) {
			printk(KERN_ERR "%s: fail \n", __func__);
			return -EIO;
		}

		offs += len;
		size -= len;
	}

	// verify and correct ecc
	if ((flags & (FLAG_VERIFY | FLAG_ECC_EN)) == (FLAG_VERIFY | FLAG_ECC_EN)) {
		status = nfc_ecc_verify(ra, buf, page, FL_READING);
		if (status != 0) {
			printk(KERN_ERR "%s: fail, buf:%x, page:%x, flag:%x\n", 
				__func__, (unsigned int)buf, page, flags);
			return status;
		}
	} else {
		// fix,e not yet support
		ra->buffers_page = -1; //cached
	}

	return 0;
}


/** 
 * @return -EIO, fail to write
 * @return 0, OK
 */
static int
nfc_write_page(struct ra_nand_chip *ra, unsigned char *buf, int page, int flags)
{
	unsigned int cmd1 = 0, cmd3, conf = 0;
	unsigned long row_addr = 0;
	int pagesize;
	char status;

	ra->buffers_page = -1;		//cached

	page = page & ((1u << ra->flash->chip_shift) - 1); // chip boundary
	pagesize = (1u << ra->flash->page_shift);

	if (flags & FLAG_ECC_EN) {
#ifndef RALINK_NAND_BMT
		memset(ra->buffers + pagesize, 0xff, (1u << ra->flash->oob_shift));
#endif
		pagesize = pagesize + (1u << ra->flash->oob_shift);
	}

	row_addr = page;

	cmd1 = ra->opcode->pageprog1;
	cmd3 = ra->opcode->pageprog2;

	if (ra->flash->page_shift == SIZE_512iB_BIT) {
		conf = 0x001243 | ((addr_cycle) << 16) | ((pagesize) << 20);
	} else {
		conf = 0x001103 | ((addr_cycle) << 16) | ((pagesize) << 20);
	}

	ra_dbg("in nfc_write_page function\n");
	ra_dbg("CMD1:%02x\n", cmd1);
	ra_dbg("CMD3:%02x\n", cmd3);
	ra_dbg("CONFIG:%06x\n", conf);

	if (flags & FLAG_ECC_EN)
		conf |= (1 << 3);

	// set NFC
	ra_dbg("nfc_write_page: cmd1: %x, cmd3: %x, conf: %x, len:%x\n",cmd1, cmd3, conf, pagesize);

	status = _nfc_write_raw_data(ra, cmd1, cmd3, row_addr, 0, conf, buf, pagesize, flags);
	if (status & NAND_STATUS_FAIL) {
		printk(KERN_ERR "%s: fail \n", __func__);
		return -EIO;
	}
	
	if (flags & FLAG_VERIFY) { // verify and correct ecc
		status = nfc_ecc_verify(ra, buf, page, FL_WRITING);
		if (status != 0) {
			printk(KERN_ERR "%s: ecc_verify fail: ret:%x \n", __func__, status);
			return -EBADMSG;
		}
	}

	ra->buffers_page = page; //cached
	return 0;
}


/*************************************************************
 * nand internal process 
 *************************************************************/

/**
 * nand_release_device - [GENERIC] release chip
 * @mtd:	MTD device structure
 *
 * Deselect, release chip lock and wake up anyone waiting on the device
 */
static void
nand_release_device(struct ra_nand_chip *ra)
{
	/* De-select the NAND device */
	nfc_select_chip(ra, -1);

	/* Release the controller and the chip */
	ra->state = FL_READY;

	mutex_unlock(ra->controller);
}


/**
 * nand_get_device - [GENERIC] Get chip for selected access
 * @chip:	the nand chip descriptor
 * @mtd:	MTD device structure
 * @new_state:	the state which is requested
 *
 * Get the device and lock it for exclusive access
 */
static int
nand_get_device(struct ra_nand_chip *ra, int new_state)
{
	int ret = 0;

	ret = mutex_lock_interruptible(ra->controller);	// code review tag
	if (!ret)
		ra->state = new_state;

	return ret;

}


/*************************************************************
 * nand internal process 
 *************************************************************/
static int
nand_bbt_get(struct ra_nand_chip *ra, int block)
{
	int byte, bits;
	bits = block * BBTTAG_BITS;

	byte = bits / 8;
	bits = bits % 8;

	return (ra->bbt[byte] >> bits) & BBTTAG_BITS_MASK;
}

static int
nand_bbt_set(struct ra_nand_chip *ra, int block, int tag)
{
	int byte, bits;
	bits = block * BBTTAG_BITS;

	byte = bits / 8;
	bits = bits % 8;

	ra->bbt[byte] = (ra->bbt[byte] & ~(BBTTAG_BITS_MASK << bits)) | ((tag & BBTTAG_BITS_MASK) << bits);

	return tag;
}


/*
 * nand_block_checkbad - [GENERIC] Check if a block is marked bad
 * @mtd:	MTD device structure
 * @ofs:	offset from device start
 *
 * Check, if the block is bad. Either by reading the bad block table or
 * calling of the scan function.
 */
static int
nand_block_checkbad(struct ra_nand_chip *ra, loff_t offs, unsigned long bmt_block)
{
	int page, block;
	int ret = 4;
	unsigned int tag;
	char *str[] = {"UNK", "RES", "BAD", "GOOD"};

	if (ranfc_bbt == 0)
		return 0;

	// align with chip
	offs = offs & (((loff_t)1u << ra->flash->chip_shift) - 1);

	page = offs >> ra->flash->page_shift;
	block = offs >> ra->flash->erase_shift;

#ifdef RALINK_NAND_BMT
    if (bmt_block == BAD_BLOCK_RAW) {
#endif
	tag = nand_bbt_get(ra, block);

	if (tag == BBT_TAG_UNKNOWN) {
		ret = nfc_read_oob(ra, page, ra->flash->badblockpos, (char*)&tag, 1, FLAG_NONE);
#ifdef __BIG_ENDIAN
		tag >>= 24;
#endif
		if (ret == 0)
			tag = ((tag & 0xff) == 0xff) ? BBT_TAG_GOOD : BBT_TAG_BAD;
		else
			tag = BBT_TAG_BAD;

		nand_bbt_set(ra, block, tag);
	}

	if (tag != BBT_TAG_GOOD) {
		printk("%s: offs:%llx tag: %s \n", __func__, (loff_t)offs, str[tag]);
		return 1;
	} else {
		return 0;
	}

#ifdef RALINK_NAND_BMT
    } else {
		ret = nfc_read_oob(ra, page, BMT_BAD_BLOCK_INDEX_OFFSET, (char*)&tag, 1, FLAG_NONE);
#ifdef __BIG_ENDIAN
		tag >>= 24;
#endif
		if (ret == 0 && ((tag & 0xff) == 0xff))
			return 0;
		else
			return 1;
    }
#endif
}


/**
 * nand_block_markbad -
 */
static int
nand_block_markbad(struct ra_nand_chip *ra, loff_t offs, unsigned long bmt_block)
{
	int page, block;
	int start_page, end_page;
	int ret = 4;
	unsigned int tag = BBT_TAG_UNKNOWN;
	char *ecc;

	// align with chip
	ra_dbg("%s offs: %llx \n", __func__, (loff_t)offs);

	offs = offs & (((loff_t)1 << ra->flash->chip_shift) - 1);

	block = offs >> ra->flash->erase_shift;
	start_page = block * (1u << (ra->flash->erase_shift - ra->flash->page_shift));
	end_page = (block + 1) * (1u << (ra->flash->erase_shift - ra->flash->page_shift));

#ifdef RALINK_NAND_BMT
    if (bmt_block == BAD_BLOCK_RAW) {
#endif
	tag = nand_bbt_get(ra, block);

	if (tag == BBT_TAG_BAD) {
		printk("%s: mark repeatedly \n", __func__);
		return 0;
	}

#ifdef RALINK_NAND_BMT
    }
#endif

	for (page = start_page; page < end_page; page++) {
		// new tag as bad
		tag = BBT_TAG_BAD;
		ret = nfc_read_page(ra, ra->buffers, page, FLAG_ECC_EN);
		if (ret != 0) {
			printk("%s: fail to read bad block tag \n", __func__);
			goto tag_bbt;
		}

#ifdef RALINK_NAND_BMT
		if (bmt_block)
			ecc = &ra->buffers[(1u << ra->flash->page_shift) + BMT_BAD_BLOCK_INDEX_OFFSET];
		else
#endif
		ecc = &ra->buffers[(1u << ra->flash->page_shift) + ra->flash->badblockpos];

		if (*ecc == (char)0xff) {
			//tag into flash
			*ecc = (char)tag;
			ret = nfc_write_page(ra, ra->buffers, page, FLAG_ECC_EN);
			if (ret) {
				printk("%s: fail to write bad block tag \n", __func__);	
				break;
			}
		}

#ifdef RALINK_NAND_BMT
		break;
#endif
	}

tag_bbt:

	//update bbt
#ifdef RALINK_NAND_BMT
	if (bmt_block == BAD_BLOCK_RAW)
#endif
	nand_bbt_set(ra, block, tag);

	return 0;
}

/**
 * nand_erase_nand - [Internal] erase block(s)
 * @mtd:	MTD device structure
 * @instr:	erase instruction
 * @allowbbt:	allow erasing the bbt area
 *
 * Erase one ore more blocks
 */
static int
nand_erase_nand(struct ra_nand_chip *ra, struct erase_info *instr)
{
	int page, len, status, ret;
	unsigned long long addr;
	unsigned int blocksize = 1u << ra->flash->erase_shift;
#ifdef RALINK_NAND_BMT
	int physical_block;
	unsigned long logic_addr;
	unsigned short phy_block_bbt;
#endif

	ra_dbg("%s: start:%llx, len:%x \n", __func__, instr->addr, (unsigned int)instr->len);

	if (BLOCK_ALIGNED(instr->addr) || BLOCK_ALIGNED(instr->len)) {
		ra_dbg("%s: erase block not aligned, addr:%llx len:%x\n", __func__, instr->addr, (unsigned int)instr->len);
		return -EINVAL;
	}

	instr->fail_addr = -1;

	len = instr->len;
	addr = instr->addr;	    //logic address
	instr->state = MTD_ERASING;

#ifdef RALINK_NAND_BMT
	logic_addr = addr;  //logic address
#endif

	while (len) {

#ifdef RALINK_NAND_BMT
		physical_block = get_mapping_block_index(logic_addr >> ra->flash->erase_shift, &phy_block_bbt); //physical block
		addr = (physical_block << ra->flash->erase_shift);  //physical address
#endif

		page = (int)(addr >> ra->flash->page_shift);
		ranfc_page = page;

		/* select device and check wp */
		if (nfc_enable_chip(ra, addr, 0)) {
			printk(KERN_WARNING "%s: nand is write protected \n", __func__);
			instr->state = MTD_ERASE_FAILED;
			goto erase_exit;
		}

#ifndef RALINK_NAND_BMT
		/*
		 * heck if we have a bad block, we do not erase bad blocks !
		 */
		if (nand_block_checkbad(ra, addr, 0)) {
			printk(KERN_WARNING "nand_erase: attempt to erase a "
				"bad block at 0x%llx\n", addr);
			instr->state = MTD_ERASE_FAILED;
			goto erase_exit;
		}
#endif
		/*
		 * Invalidate the page cache, if we erase the block which
		 * contains the current cached page
		 */
		if (BLOCK_ALIGNED(addr) == BLOCK_ALIGNED(ra->buffers_page << ra->flash->page_shift))
			ra->buffers_page = -1;

		status = nfc_erase_block(ra, page);

		/* See if block erase succeeded */
		if (status) {
#ifdef RALINK_NAND_BMT
			if (update_bmt(addr, UPDATE_ERASE_FAIL, NULL, NULL)) {
				printk(KERN_INFO "Erase fail at block, update BMT success\n");
			} else {
				printk(KERN_ERR "Erase fail at block, update BMT fail\n");
				return -1;
			}
#else
			printk("%s: failed erase, block 0x%08x\n", __func__, page);
			instr->state = MTD_ERASE_FAILED;
			instr->fail_addr = (page << ra->flash->page_shift);
			goto erase_exit;
#endif
		}


		/* Increment page address and decrement length */
		len -= blocksize;
		addr += blocksize;  //physical address

#ifdef RALINK_NAND_BMT
		logic_addr += blocksize;    //logic address
#endif
	}

	instr->state = MTD_ERASE_DONE;

erase_exit:

	ret = ((instr->state == MTD_ERASE_DONE) ? 0 : -EIO);

	/* Do call back function */
	if (!ret)
		mtd_erase_callback(instr);

	if (ret) {
		nand_bbt_set(ra, addr >> ra->flash->erase_shift, BBT_TAG_BAD);
	}

	/* Return more or less happy */
	return ret;
}

static int
nand_write_oob_buf(struct ra_nand_chip *ra, uint8_t *buf, uint8_t *oob,
		   size_t size, int mode, int ooboffs)
{
	size_t oobsize = 1u << ra->flash->oob_shift;
	int retsize = 0;

	ra_dbg("%s: size:%x, mode:%x, offs:%x  \n", __func__, size, mode, ooboffs);

	switch(mode) {
	case MTD_OPS_PLACE_OOB:
	case MTD_OPS_RAW:
		if (ooboffs > oobsize)
			return -1;

#if 0		/* clear buffer */
		if (ooboffs || ooboffs+size < oobsize) 
			memset (ra->buffers + oobsize, 0x0ff, 1u << ra->oob_shift);
#endif

		size = min(size, oobsize - ooboffs);
		memcpy(buf + ooboffs, oob, size);
		retsize = size;
		break;

	case MTD_OPS_AUTO_OOB:
	{
		struct nand_oobfree *free;
		uint32_t woffs = ooboffs;

		if (ooboffs > ra->oob->oobavail) 
			return -1;

		/* OOB AUTO does not clear buffer */
		for (free = ra->oob->oobfree; free->length && size; free++) {
			int wlen = free->length - woffs;
			int bytes = 0;

			/* Write request not from offset 0 ? */
			if (wlen <= 0) {
				woffs = -wlen;
				continue;
			}
			
			bytes = min_t(size_t, size, wlen);
			memcpy (buf + free->offset + woffs, oob, bytes);
			woffs = 0;
			oob += bytes;
			size -= bytes;
			retsize += bytes;
		}

		buf += oobsize;
		break;
	}

	default:
		BUG();
	}

	return retsize;
}


static int
nand_read_oob_buf(struct ra_nand_chip *ra, uint8_t *oob, size_t size,
		  int mode, int ooboffs)
{
	size_t oobsize = 1u << ra->flash->oob_shift;
	uint8_t *buf = ra->buffers + (1u << ra->flash->page_shift);
	int retsize = 0;

	ra_dbg("%s: size:%x, mode:%x, offs:%x  \n", __func__, size, mode, ooboffs);

	switch(mode) {
	case MTD_OPS_PLACE_OOB:
	case MTD_OPS_RAW:
		if (ooboffs > oobsize)
			return -1;

		size = min(size, oobsize - ooboffs);
		memcpy(oob, buf + ooboffs, size);
		return size;

	case MTD_OPS_AUTO_OOB: {
		struct nand_oobfree *free;
		uint32_t woffs = ooboffs;

		if (ooboffs > ra->oob->oobavail) 
			return -1;

		size = min(size, ra->oob->oobavail - ooboffs);
		for (free = ra->oob->oobfree; free->length && size; free++) {
			int wlen = free->length - woffs;
			int bytes = 0;

			/* Write request not from offset 0 ? */
			if (wlen <= 0) {
				woffs = -wlen;
				continue;
			}
			
			bytes = min_t(size_t, size, wlen);
			memcpy(oob, buf + free->offset + woffs, bytes);
			woffs = 0;
			oob += bytes;
			size -= bytes;
			retsize += bytes;
		}

		return retsize;
	}
	default:
		BUG();
	}

	return -1;
}


/**
 * nand_do_write_ops - [Internal] NAND write with ECC
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @ops:	oob operations description structure
 *
 * NAND write with ECC
 */
static int
nand_do_write_ops(struct ra_nand_chip *ra, loff_t to,
		  struct mtd_oob_ops *ops)
{
	int page;
	uint32_t datalen = ops->len;
	uint32_t ooblen = ops->ooblen;
	uint8_t *oob = ops->oobbuf;
	uint8_t *data = ops->datbuf;
	int ranfc_flags = FLAG_NONE;
	int pagesize = (1u << ra->flash->page_shift);
	int pagemask = (pagesize -1);
	int oobsize = (1u << ra->flash->oob_shift);

#ifdef RALINK_NAND_BMT
	int physical_block;
	int logic_page;
	unsigned long addr_offset_in_block;
	unsigned long logic_addr;
	unsigned short phy_block_bbt;
#endif
	loff_t addr = to;   //logic address

#ifdef RALINK_NAND_BMT
	logic_addr = addr;        //logic address
#endif

	ops->retlen = 0;
	ops->oobretlen = 0;

	/* Invalidate the page cache, when we write to the cached page */
	ra->buffers_page = -1;

	if (data == NULL)
		datalen = 0;

	// data sequential (burst) write
	if (datalen && ooblen == 0) {
//		printk("ranfc can not support write_data_burst, since hw-ecc and fifo constraints..\n");
	}

	// page write
	while (datalen || ooblen) {
		int len;
		int ret;
		int offs;
		int ecc_en = 0;

		ra_dbg("%s : addr:%llx, ops data:%p, oob:%p datalen:%x ooblen:%x, ooboffs:%x \n", 
		       __func__, addr, data, oob, datalen, ooblen, ops->ooboffs);

#ifdef RALINK_NAND_BMT
		addr_offset_in_block = logic_addr % (1u << ra->flash->erase_shift);  //logic address offset
		physical_block = get_mapping_block_index(logic_addr >> ra->flash->erase_shift, &phy_block_bbt); //physical block
		addr = (physical_block << ra->flash->erase_shift) + addr_offset_in_block;   //physical address offset
#endif

		page = (int)((addr & (((loff_t)1 << ra->flash->chip_shift) - 1)) >> ra->flash->page_shift); //chip boundary

#ifdef RALINK_NAND_BMT
		logic_page = (int)((logic_addr & ((1u << ra->flash->chip_shift) - 1)) >> ra->flash->page_shift); //logic page
#endif

		ranfc_page = page;

		/* select chip, and check if it is write protected */
		if (nfc_enable_chip(ra, addr, 0))
			return -EIO;

		if (oob && ooblen > 0) {
			memset(ra->buffers + pagesize, 0xff, oobsize);
			len = nand_write_oob_buf(ra, ra->buffers + pagesize, oob, ooblen, ops->mode, ops->ooboffs);
			if (len < 0)
				return -EINVAL;
			oob += len;
			ops->oobretlen += len;
			ooblen -= len;
		}

		// data write
		offs = addr & pagemask;
		len = min_t(size_t, datalen, pagesize - offs);

		if (data && len > 0) {
#ifdef RALINK_NAND_BMT
			memset(ra->buffers, 0xff, pagesize + oobsize);
#else
			memset(ra->buffers, 0xff, pagesize);
#endif
			memcpy(ra->buffers + offs, data, len);	// we can not sure ops->buf wether is DMA-able.
#ifdef RALINK_NAND_BMT
			if (block_is_in_bmt_region(physical_block))
				memcpy(ra->buffers + pagesize + OOB_INDEX_OFFSET, &phy_block_bbt, OOB_INDEX_SIZE);
#endif
			data += len;
			datalen -= len;
			ops->retlen += len;
			ecc_en = FLAG_ECC_EN;
		}

#ifdef RALINK_NAND_BMT
		ranfc_flags = (FLAG_VERIFY | ecc_en);
#else
		ranfc_flags = (FLAG_VERIFY | ((ops->mode == MTD_OPS_RAW || ops->mode == MTD_OPS_PLACE_OOB) ? 0 : ecc_en));
#endif

#ifdef RALINK_NAND_BMT
		if (!(data && len > 0)) {
			ret = nfc_write_oob(ra, page, 0, ra->buffers + pagesize, oobsize, ranfc_flags);
			if (ret)
				nfc_read_page(ra, ra->buffers, page, ranfc_flags);
		} else
#endif
		ret = nfc_write_page(ra, ra->buffers, page, ranfc_flags);
		if (ret) {
#ifdef RALINK_NAND_BMT
			printk(KERN_WARNING "write fail at page: %d \n", page);
			memcpy(write_ops_dat, ra->buffers, SIZE_2KiB_BYTES + SIZE_64iB_BYTES);

			if (update_bmt(page << ra->flash->page_shift, UPDATE_WRITE_FAIL, write_ops_dat, write_ops_dat + SIZE_2KiB_BYTES)) {
				printk(KERN_INFO "Update BMT success\n");
			} else {
				printk(KERN_ERR "Update BMT fail\n");
				return -1;
			}
#else
			nand_bbt_set(ra, addr >> ra->flash->erase_shift, BBT_TAG_BAD);
			return ret;
#endif
		}

#ifndef RALINK_NAND_BMT
		nand_bbt_set(ra, addr >> ra->flash->erase_shift, BBT_TAG_GOOD);
#endif

		addr = (page+1) << ra->flash->page_shift;   //physical address

#ifdef RALINK_NAND_BMT
		logic_addr = (logic_page + 1) << ra->flash->page_shift; //logic address
#endif
	}

	return 0;
}

/**
 * nand_do_read_ops - [Internal] Read data with ECC
 *
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @ops:	oob ops structure
 *
 * Internal function. Called with chip held.
 */
static int
nand_do_read_ops(struct ra_nand_chip *ra, loff_t from,
		 struct mtd_oob_ops *ops)
{
	int page;
	uint32_t datalen = ops->len;
	uint32_t ooblen = ops->ooblen;
	uint8_t *oob = ops->oobbuf;
	uint8_t *data = ops->datbuf;
	int ranfc_flags = FLAG_NONE;
	int pagesize = (1u << ra->flash->page_shift);
	int oobsize = (1u << ra->flash->oob_shift);
	int pagemask = (pagesize - 1);
	loff_t addr = from; //logic address

#ifdef RALINK_NAND_BMT
	int physical_block;
	int logic_page;
	unsigned long addr_offset_in_block;
	unsigned long logic_addr = addr;
	unsigned short phy_block_bbt;
#endif

	ops->retlen = 0;
	ops->oobretlen = 0;

	if (data == NULL)
		datalen = 0;

	while (datalen || ooblen) {
		int len, ret, offs;

		/* select chip */
		if (nfc_enable_chip(ra, addr, 1) < 0)
			return -EIO;

#ifdef RALINK_NAND_BMT
		addr_offset_in_block = logic_addr % (1u << ra->flash->erase_shift);  //logic address offset
		physical_block = get_mapping_block_index(logic_addr >> ra->flash->erase_shift, &phy_block_bbt); //physical block
		addr = (physical_block << ra->flash->erase_shift) + addr_offset_in_block;   //physical address
#endif

		page = (int)((addr & (((loff_t)1 << ra->flash->chip_shift) - 1)) >> ra->flash->page_shift); 

#ifdef RALINK_NAND_BMT
		logic_page = (int)((logic_addr & ((1u << ra->flash->chip_shift) - 1)) >> ra->flash->page_shift); //logic page
#endif

		ranfc_page = page;

#ifdef RALINK_NAND_BMT
		ranfc_flags = (FLAG_VERIFY | FLAG_ECC_EN);
#else
		ranfc_flags = (FLAG_VERIFY | ((ops->mode == MTD_OPS_RAW || ops->mode == MTD_OPS_PLACE_OOB) ? 0 : FLAG_ECC_EN));
#endif

#ifdef RALINK_NAND_BMT
		if (data && datalen > 0) {
#endif
		ret = nfc_read_page(ra, ra->buffers, page, ranfc_flags);
		//FIXME, something strange here, some page needs 2 more tries to guarantee read success.
		if (ret) {
			ret = nfc_read_page(ra, ra->buffers, page, ranfc_flags); 
			if (ret) {
#ifdef RALINK_NAND_BMT
				return ret;
#else
				nand_bbt_set(ra, addr >> ra->flash->erase_shift, BBT_TAG_BAD);
				if ((ret != -EUCLEAN) && (ret != -EBADMSG)) {
					return ret;
				} else {
					/* ecc verification fail, but data need to be returned. */
				}
#endif
			}
		}

#ifdef RALINK_NAND_BMT
		}
#endif

		// oob read
		if (oob && ooblen > 0) {
#ifdef RALINK_NAND_BMT
			memset(ra->buffers + pagesize, 0xff, oobsize);
			nfc_read_oob(ra, page, 0, ra->buffers + pagesize, oobsize, FLAG_NONE);
#endif
			len = nand_read_oob_buf(ra, oob, ooblen, ops->mode, ops->ooboffs);
			if (len < 0) {
				printk(KERN_ERR "nand_read_oob_buf: fail return %x \n", len);
				return -EINVAL;
			}
			oob += len;
			ops->oobretlen += len;
			ooblen -= len;
		}

		// data read
		offs = addr & pagemask;
		len = min_t(size_t, datalen, pagesize - offs);
		if (data && len > 0) {
			memcpy(data, ra->buffers + offs, len);	// we can not sure ops->buf wether is DMA-able.
			data += len;
			datalen -= len;
			ops->retlen += len;
			if (ret) {
				return ret;
			}
		}

		// address go further to next page, instead of increasing of length of write. This avoids some special cases wrong.
		addr = ((loff_t)(page+1) << ra->flash->page_shift); //physical address

#ifdef RALINK_NAND_BMT
		logic_addr = (logic_page + 1) << ra->flash->page_shift; //logic address
#endif
	}

	return 0;
}

/*
 * nand_setup - setup nand flash info and opcode
 *
 */
static struct ra_nand_chip *
nand_setup(int *err)
{
	int flash_id, i, subpage_bit = 0;
	unsigned int id_mask, mfr_id, dev_id, nbits;
	int alloc_size, bbt_size, buffers_size;
	struct ra_nand_chip *ra = NULL;
	struct nand_info *flash = NULL;
	struct nand_opcode *opcode = NULL;

	*err = -1;

	flash_id = nfc_read_id();
	if (flash_id == -1) {
		printk(KERN_ERR "%s: read flash id fail!\n", __func__);
		return NULL;
	}

	id_mask = 0xffff;
	nbits = 16;

	if ((flash_id >> nbits) != 0) {
		nbits = 8;
		id_mask = 0xff;
	}

	if (ra_inl(NFC_CTRLII) & 0x02) {
		mfr_id = ((flash_id >> 24) & id_mask);
		dev_id = ((flash_id >> (24 - nbits)) & id_mask);
	} else {
		mfr_id = (flash_id & id_mask);
		dev_id = ((flash_id >> nbits) & id_mask);
	}

	for (i = 0; i < ARRAY_SIZE(flash_tables); i++) {
		if ((mfr_id == flash_tables[i].mfr_id) && (dev_id == flash_tables[i].dev_id)) {
			flash = &flash_tables[i];
			break;
		}
	}

	if (flash == NULL) {
		printk(KERN_ERR "%s: Undefined NAND Manuf ID (%04X) and Device ID (%04X)\n",
			__func__, mfr_id, dev_id);
		return NULL;
	}

	printk(KERN_INFO "NAND Flash      : %s %dMB\n", flash->name, (1u << flash->chip_shift) / (1024 * 1024));
	printk(KERN_INFO "NAND Block size : %dKB\n", (1u << flash->erase_shift) / 1024);
	printk(KERN_INFO "NAND Page size  : %dB\n", (1u << flash->page_shift));

	if (flash->page_shift == SIZE_512iB_BIT)
		subpage_bit = 1;

	column_addr_cycle = (flash->page_shift - subpage_bit + 7) / 8;
	row_addr_cycle = (flash->chip_shift - flash->page_shift + 7) / 8;
	addr_cycle = column_addr_cycle + row_addr_cycle;

	for (i = 0; i < ARRAY_SIZE(opcode_tables); i++) {
		if (flash->opcode_type == opcode_tables[i].type) {
			opcode = &opcode_tables[i];
			break;
		}
	}

	if (opcode == NULL) {
		printk(KERN_ERR "%s: Undefined NAND opcode table\n", __func__);
		return NULL;
	}

	if (flash->page_shift == SIZE_512iB_BIT) {
		ra_outl(NFC_CTRLII, 0x76500);
	} else if (flash->page_shift == SIZE_2KiB_BIT) {
		ra_outl(NFC_CTRLII, 0x76501);
	} else {
		printk(KERN_ERR "%s: Undefined NAND OOB layout\n", __func__);
		return NULL;
	}

#ifdef __BIG_ENDIAN
	ra_or(NFC_CTRLII, 0x02);
#endif

	/*
	 * TWAITB: 0x240
	 * THOLD: 0x2
	 * TPERIOD: 0x7
	 * TSETUP: 0x2
	 * BURST_SIZE: 0
	 * WP: 1
	 */
	ra_outl(NFC_CTRL, 0x02402721);

#define ALIGN_32(a) (((unsigned long)(a)+31) & ~31)	// code review tag

	buffers_size = ALIGN_32((1u << flash->page_shift) + (1u << flash->oob_shift)); //ra->buffers
	bbt_size = BBTTAG_BITS * (1u << (flash->chip_shift - flash->erase_shift)) / 8; //ra->bbt
	bbt_size = ALIGN_32(bbt_size);

	alloc_size = buffers_size + bbt_size;
	alloc_size += buffers_size; //for ra->readback_buffers
	alloc_size += sizeof(*ra);
	alloc_size += sizeof(*ranfc_mtd);

	ra = kzalloc(alloc_size, GFP_KERNEL | GFP_DMA);
	if (!ra) {
		*err = -ENOMEM;
		printk(KERN_ERR "%s: mem alloc fail \n", __func__);
		return NULL;
	}

	ra->buffers = (char *)((char *)ra + sizeof(*ra));
	ra->readback_buffers = ra->buffers + buffers_size;
	ra->bbt = ra->readback_buffers + buffers_size;
	ranfc_mtd = (struct mtd_info *)(ra->bbt + bbt_size);
	ra->buffers_page = -1;

	ra_dbg("%s: alloc %x, at %p , btt(%p, %x), ranfc_mtd:%p\n",
		__func__ , alloc_size, ra, ra->bbt, bbt_size, ranfc_mtd);

	if (flash->page_shift == SIZE_512iB_BIT)
		ra->oob = &oob_layout_tables[STANDARD_SMALL_FLASH];
	else
		ra->oob = &oob_layout_tables[STANDARD_LARGE_FLASH];

	ra->flash = flash;
	ra->opcode = opcode;

#ifdef RALINK_NAND_BMT
	bmt_pool_size = calc_bmt_pool_size(ra);
	printk(KERN_INFO "BMT pool size   : %d\n", bmt_pool_size);

	if (!g_bmt) {
		g_bmt = init_bmt(ra, bmt_pool_size);
		if (!g_bmt) {
			printk(KERN_ERR "Error: init bmt failed \n");
			goto setup_fail;
		}
	}

	if (!g_bbt) {
		g_bbt = start_init_bbt();
		if (!g_bbt) {
			printk(KERN_ERR "Error: init bbt failed!\n");
			goto setup_fail;
		}
	}

	if (write_bbt_or_bmt_to_flash() != 0) {
		printk(KERN_ERR "Error: save bbt or bmt to nand failed!\n");
		goto setup_fail;
	}

	if (create_badblock_table_by_bbt()) {
		printk(KERN_ERR "Error: create bad block table failed!\n");
		goto setup_fail;
	}
#endif

	*err = 0;

	return ra;

setup_fail:

	kfree(ra);
	return NULL;
}


/************************************************************
 * the following are mtd necessary interface.
 ************************************************************/


/**
 * nand_erase - [MTD Interface] erase block(s)
 * @mtd:	MTD device structure
 * @instr:	erase instruction
 *
 * Erase one ore more blocks
 */
static int
ramtd_nand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;
	struct ra_nand_chip *ra = (struct ra_nand_chip *)mtd->priv;

	ra_dbg("%s: \n", __func__);

	/* Grab the lock and see if the device is available */
	nand_get_device(ra, FL_ERASING);
	ret = nand_erase_nand((struct ra_nand_chip *)mtd->priv, instr);
	nand_release_device(ra);

	return ret;
}

/**
 * nand_write - [MTD Interface] NAND write with ECC
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @len:	number of bytes to write
 * @retlen:	pointer to variable to store the number of written bytes
 * @buf:	the data to write
 *
 * NAND write with ECC
 */
static int
ramtd_nand_write(struct mtd_info *mtd, loff_t to, size_t len,
		 size_t *retlen, const uint8_t *buf)
{
	struct ra_nand_chip *ra = mtd->priv;
	struct mtd_oob_ops ops;
	int ret;

	ra_dbg("%s: \n", __func__);

	/* Do not allow reads past end of device */
	if ((to + len) > mtd->size)
		return -EINVAL;

	if (!len)
		return 0;

	memset(&ops, 0, sizeof(ops));

	ops.len = len;
	ops.datbuf = (uint8_t *)buf;
	ops.oobbuf = NULL;
	ops.ooblen = 0;
	ops.mode = MTD_OPS_AUTO_OOB;

	nand_get_device(ra, FL_WRITING);
	ret = nand_do_write_ops(ra, to, &ops);
	*retlen = ops.retlen;
	nand_release_device(ra);

	return ret;

}

/**
 * nand_read - [MTD Interface] MTD compability function for nand_do_read_ecc
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @len:	number of bytes to read
 * @retlen:	pointer to variable to store the number of read bytes
 * @buf:	the databuffer to put data
 *
 * Get hold of the chip and call nand_do_read
 */
static int
ramtd_nand_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, uint8_t *buf)
{
	struct ra_nand_chip *ra = mtd->priv;
	struct mtd_oob_ops ops;
	int ret;

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size)
		return -EINVAL;

	if (!len)
		return 0;

	memset(&ops, 0, sizeof(ops));
	ops.ooblen = 0;
	ops.oobbuf = NULL;
	ops.len = len;
	ops.datbuf = buf;
	ops.mode = MTD_OPS_AUTO_OOB;

	nand_get_device(ra, FL_READING);
	ret = nand_do_read_ops(ra, from, &ops);
	*retlen = ops.retlen;
	nand_release_device(ra);

	return ret;
}

/**
 * nand_read_oob - [MTD Interface] NAND read data and/or out-of-band
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @ops:	oob operation description structure
 *
 * NAND read data and/or out-of-band data
 */
static int
ramtd_nand_readoob(struct mtd_info *mtd, loff_t from,
		   struct mtd_oob_ops *ops)
{
	struct ra_nand_chip *ra = mtd->priv;
	int ret;

	ra_dbg("%s: \n", __func__);

	nand_get_device(ra, FL_READING);
	ret = nand_do_read_ops(ra, from, ops);
	nand_release_device(ra);

	return ret;
}

/**
 * nand_write_oob - [MTD Interface] NAND write data and/or out-of-band
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @ops:	oob operation description structure
 */
static int
ramtd_nand_writeoob(struct mtd_info *mtd, loff_t to,
		    struct mtd_oob_ops *ops)
{
	struct ra_nand_chip *ra = mtd->priv;
	int ret;

	ra_dbg("%s: \n", __func__);

	nand_get_device(ra, FL_READING);
	ret = nand_do_write_ops(ra, to, ops);
	nand_release_device(ra);

	return ret;
}

/**
 * nand_block_isbad - [MTD Interface] Check if block at offset is bad
 * @mtd:	MTD device structure
 * @offs:	offset relative to mtd start
 */
static int
ramtd_nand_block_isbad(struct mtd_info *mtd, loff_t offs)
{
#ifdef RALINK_NAND_BMT
	return 0;
#else
	if (offs > mtd->size)
		return -EINVAL;

	return nand_block_checkbad((struct ra_nand_chip *)mtd->priv, offs, 0);
#endif
}

/**
 * nand_block_markbad - [MTD Interface] Mark block at the given offset as bad
 * @mtd:	MTD device structure
 * @ofs:	offset relative to mtd start
 */
static int
ramtd_nand_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
#ifdef RALINK_NAND_BMT
	return 0;
#else
	struct ra_nand_chip *ra = mtd->priv;
	int ret;

	ra_dbg("%s: \n", __func__);

	nand_get_device(ra, FL_WRITING);
	ret = nand_block_markbad(ra, ofs, 0);
	nand_release_device(ra);

	return ret;
#endif
}

#ifdef RALINK_NAND_BMT
int
mt6573_nand_erase_hw(struct ra_nand_chip *ra, unsigned long page)
{
	return nfc_erase_block(ra, page);
}

int
mt6573_nand_exec_write_page(struct ra_nand_chip *ra, int page, u32 page_size, u8 *dat, u8 *oob)
{
	memset(ra->buffers, 0xff, sizeof(ra->buffers));
	memcpy(ra->buffers, dat, page_size);
	memcpy(ra->buffers + page_size, oob, 1u << ra->flash->oob_shift);

	return nfc_write_page(ra, ra->buffers, page, FLAG_ECC_EN);
}

int
mt6573_nand_exec_read_page(struct ra_nand_chip *ra, int page, u32 page_size, u8 *dat, u8 *oob)
{
	int ret;

	ret = nfc_read_page(ra, ra->buffers, page, FLAG_ECC_EN | FLAG_VERIFY);
	if (ret) {
		ret = nfc_read_page(ra, ra->buffers, page, FLAG_ECC_EN | FLAG_VERIFY);
		if (ret) {
			printk("[%s]: read again fail!", __func__);
			goto read_fail;
		}
	}

	memcpy(dat, ra->buffers, page_size);
	memcpy(oob, ra->buffers + page_size, 1u << ra->flash->oob_shift);

read_fail:
	return ret;
}

int
mt6573_nand_block_markbad_hw(struct ra_nand_chip *ra, unsigned long ofs, unsigned long bmt_block)
{
	unsigned long page;
	int block;

	block = ofs >> ra->flash->erase_shift;
	page = block * (1u << (ra->flash->erase_shift - ra->flash->page_shift));

	nfc_erase_block(ra, page);
	nand_block_markbad(ra, ofs, bmt_block);

	return 0;
}

int
mt6573_nand_block_bad_hw(struct ra_nand_chip *ra, unsigned long ofs, unsigned long bmt_block)
{
	return nand_block_checkbad(ra, ofs, bmt_block);
}

static int
calc_bmt_pool_size(struct ra_nand_chip *ra)
{
	int chip_size = 1u << ra->flash->chip_shift;
	int block_size = 1u << ra->flash->erase_shift;
	int total_block = chip_size / block_size;
	int last_block = total_block - 1;
	u16 valid_block_num = 0;
	u16 need_valid_block_num = total_block * POOL_GOOD_BLOCK_PERCENT;

	for (; last_block > 0; --last_block) {
		if (nand_block_checkbad(ra, last_block * block_size, BAD_BLOCK_RAW))
			continue;

		valid_block_num++;
		if (valid_block_num == need_valid_block_num)
			break;
	}

	return (total_block - last_block);
}
#endif

static int __init
ra_nand_init(void)
{
	int err = 0;
	struct ra_nand_chip *ra;

	if (ra_check_flash_type() != BOOT_FROM_NAND)
		return 0;

	/* GPIO pins to NAND mode */
	ra_or(RALINK_REG_GPIOMODE, RALINK_GPIOMODE_NFD);

	nfc_all_reset();

	ra = nand_setup(&err);
	if (!ra)
		return err;

	ranfc_mtd->name			= "ra_nfc";
	ranfc_mtd->type			= MTD_NANDFLASH;
	ranfc_mtd->flags		= MTD_CAP_NANDFLASH;
#ifdef RALINK_NAND_BMT
	ranfc_mtd->size			= nand_logic_size;
#else
	ranfc_mtd->size			= CONFIG_NUMCHIPS * (1u << ra->flash->chip_shift);
#endif
	ranfc_mtd->erasesize		= (1u << ra->flash->erase_shift);
	ranfc_mtd->writesize		= (1u << ra->flash->page_shift);
	ranfc_mtd->writebufsize = ranfc_mtd->writesize;
	ranfc_mtd->oobsize 		= (1u << ra->flash->oob_shift);
	ranfc_mtd->oobavail		= ra->oob->oobavail;
	ranfc_mtd->ecclayout		= ra->oob;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	ranfc_mtd->_erase		= ramtd_nand_erase;
	ranfc_mtd->_read		= ramtd_nand_read;
	ranfc_mtd->_write		= ramtd_nand_write;
	ranfc_mtd->_read_oob		= ramtd_nand_readoob;
	ranfc_mtd->_write_oob		= ramtd_nand_writeoob;
	ranfc_mtd->_block_isbad		= ramtd_nand_block_isbad;
	ranfc_mtd->_block_markbad	= ramtd_nand_block_markbad;
#else
	ranfc_mtd->erase 		= ramtd_nand_erase;
	ranfc_mtd->read			= ramtd_nand_read;
	ranfc_mtd->write		= ramtd_nand_write;
	ranfc_mtd->read_oob		= ramtd_nand_readoob;
	ranfc_mtd->write_oob		= ramtd_nand_writeoob;
	ranfc_mtd->block_isbad		= ramtd_nand_block_isbad;
	ranfc_mtd->block_markbad	= ramtd_nand_block_markbad;
#endif

	ranfc_mtd->priv = ra;

	ranfc_mtd->owner = THIS_MODULE;
	ra->controller = &ra->hwcontrol;
	mutex_init(ra->controller);

	/* register the partitions */
	return mtd_device_parse_register(ranfc_mtd, part_probes, NULL, NULL, 0);
}

static void __exit
ra_nand_exit(void)
{
	if (ranfc_mtd) {
		struct ra_nand_chip *ra = (struct ra_nand_chip *)ranfc_mtd->priv;
		mtd_device_unregister(ranfc_mtd);
		kfree(ra);
		ranfc_mtd = NULL;
	}
}

module_init(ra_nand_init);
module_exit(ra_nand_exit);

MODULE_LICENSE("GPL");
