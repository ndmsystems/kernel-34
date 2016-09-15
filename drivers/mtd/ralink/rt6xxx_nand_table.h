#ifndef _RT6XXX_NAND_TABLE_H
#define _RT6XXX_NAND_TABLE_H

/* Manufacturers */
#define MANUFACTURER_ST3A 		0x20
#define MANUFACTURER_ST4A 		0x0020
#define	MANUFACTURER_MICRON		0x2c
#define MANUFACTURER_SAMSUNG		0xec
#define MANUFACTURER_SPANSION		0x01
#define MANUFACTURER_ZENTEL		0x92

/* ST Device ID */
#define ST128W3A			0x73
#define ST512W3A			0x76

/* MICRON Device ID */
#define MT29F2G08AAD			0xda
#define MT29F4G08AAC			0xdc

#define K9F1G08U0D			0xf1
#define S34ML01G1			0xf1
#define A5U1GA31ATS			0xf1

/* Flash Type */
#define STANDARD_SMALL_FLASH		(0)
#define STANDARD_LARGE_FLASH		(1)

#define SMALL_FLASH_ECC_BYTES		3	//! ecc has 3 bytes
#define SMALL_FLASH_ECC_OFFSET		5	//! ecc starts from offset 5.

#define LARGE_FLASH_ECC_BYTES		12	//! ecc has 12 bytes
#define LARGE_FLASH_ECC_OFFSET		5	//! ecc starts from offset 5.

#define NONE				(-1)

/* SIZE BIT */
#define SIZE_512MiB_BIT			(29)
#define SIZE_256MiB_BIT			(28)
#define SIZE_128MiB_BIT			(27)
#define SIZE_64MiB_BIT			(26)
#define SIZE_16MiB_BIT			(24)
#define SIZE_128KiB_BIT			(17)
#define SIZE_16KiB_BIT			(14)
#define SIZE_4KiB_BIT			(12)
#define SIZE_2KiB_BIT			(11)
#define SIZE_512iB_BIT			(9)
#define SIZE_64iB_BIT			(6)
#define SIZE_16iB_BIT			(4)

/* SIZE BYTE */
#define SIZE_512M_BYTES			(512)
#define SIZE_2K_BYTES			(2048)

static struct nand_opcode opcode_tables[] = {
	{
		type: STANDARD_SMALL_FLASH,
		read1: 0x00,
		read2: NONE,
		readB: 0x01,
		readoob: 0x50,
		pageprog1: 0x8000,
		pageprog2: 0x10,
		writeoob: 0x8050,
		erase1: 0x60,
		erase2: 0xd0,
		status: 0x70,
		reset: 0xff,
	},
	{
		type: STANDARD_LARGE_FLASH,
		read1: 0x00,
		read2: 0x30,
		readB: NONE,
		readoob: 0x00,
		pageprog1: 0x80,
		pageprog2: 0x10,
		writeoob: 0x80,
		erase1: 0x60,
		erase2: 0xd0,
		status: 0x70,
		reset: 0xff,
	},
};

static struct nand_info flash_tables[] = {
	{
		mfr_id: MANUFACTURER_ST3A,
		dev_id: ST128W3A,
		name: "ST NAND128W3A",
		numchips: (1),
		chip_shift: SIZE_16MiB_BIT,
		page_shift: SIZE_512iB_BIT,
		erase_shift: SIZE_16KiB_BIT,
		oob_shift: SIZE_16iB_BIT,
		badblockpos: (4),		//512 pagesize bad blk offset --> 4
		opcode_type: STANDARD_SMALL_FLASH,
	},
	{
		mfr_id: MANUFACTURER_ST3A,
		dev_id: ST512W3A,
		name: "ST NAND512W3A",
		numchips: (1),
		chip_shift: SIZE_64MiB_BIT,
		page_shift: SIZE_512iB_BIT,
		erase_shift: SIZE_16KiB_BIT,
		oob_shift: SIZE_16iB_BIT,
		badblockpos: (4),		//512 pagesize bad blk offset --> 4
		opcode_type: STANDARD_SMALL_FLASH,
	},
	{
		mfr_id: MANUFACTURER_ZENTEL,
		dev_id: A5U1GA31ATS,
		name: "ZENTEL NAND1GA31ATS",
		numchips: (1),
		chip_shift: SIZE_128MiB_BIT,
		page_shift: SIZE_2KiB_BIT,
		erase_shift: SIZE_128KiB_BIT,
		oob_shift: SIZE_64iB_BIT,
		badblockpos: (51),
		opcode_type: STANDARD_LARGE_FLASH,
	},
	{
		mfr_id: MANUFACTURER_MICRON,
		dev_id: MT29F2G08AAD,
		name: "MICRON NAND2G08AAD",
		numchips: (1),
		chip_shift: SIZE_256MiB_BIT,
		page_shift: SIZE_2KiB_BIT,
		erase_shift: SIZE_128KiB_BIT,
		oob_shift: SIZE_64iB_BIT,
		badblockpos: (51),
		opcode_type: STANDARD_LARGE_FLASH,
	},
	{
		mfr_id: MANUFACTURER_MICRON,
		dev_id: MT29F4G08AAC,
		name: "MICRON NAND4G08AAC",
		numchips: (1),
		chip_shift: SIZE_512MiB_BIT,
		page_shift: SIZE_2KiB_BIT,
		erase_shift: SIZE_128KiB_BIT,
		oob_shift: SIZE_64iB_BIT,
		badblockpos: (51),
		opcode_type: STANDARD_LARGE_FLASH,
	},
	{
		mfr_id: MANUFACTURER_SAMSUNG,
		dev_id: K9F1G08U0D,
		name: "SAMSUNG K9F1G08U0D",
		numchips: (1),
		chip_shift: SIZE_128MiB_BIT,
		page_shift: SIZE_2KiB_BIT,
		erase_shift: SIZE_128KiB_BIT,
		oob_shift: SIZE_64iB_BIT,
		badblockpos: (0),
		opcode_type: STANDARD_LARGE_FLASH,
	},
	{
		mfr_id: MANUFACTURER_SPANSION,
		dev_id: S34ML01G1,
		name: "SPANSION S34ML01G1",
		numchips: (1),
		chip_shift: SIZE_128MiB_BIT,
		page_shift: SIZE_2KiB_BIT,
		erase_shift: SIZE_128KiB_BIT,
		oob_shift: SIZE_64iB_BIT,
		badblockpos: (0),
		opcode_type: STANDARD_LARGE_FLASH,
	},
};

static struct nand_ecclayout oob_layout_tables[] = {
	/* 512iB page size flash */
	{
		.eccbytes = SMALL_FLASH_ECC_BYTES,
		.eccpos = {
			SMALL_FLASH_ECC_OFFSET, SMALL_FLASH_ECC_OFFSET+1, SMALL_FLASH_ECC_OFFSET+2
		},
		.oobfree = {
			{ .offset = 0, .length = 4 },
			{ .offset = 8, .length = 8 },
			{ .offset = 0, .length = 0 }
		 },
		.oobavail = 12,
		// 4th byte is bad-block flag.
	},
	/* 2K page size flash */
	{
		.eccbytes = LARGE_FLASH_ECC_BYTES,
		.eccpos = {
			LARGE_FLASH_ECC_OFFSET,    LARGE_FLASH_ECC_OFFSET+1,  LARGE_FLASH_ECC_OFFSET+2,
			LARGE_FLASH_ECC_OFFSET+16, LARGE_FLASH_ECC_OFFSET+17, LARGE_FLASH_ECC_OFFSET+18,
			LARGE_FLASH_ECC_OFFSET+32, LARGE_FLASH_ECC_OFFSET+33, LARGE_FLASH_ECC_OFFSET+34,
			LARGE_FLASH_ECC_OFFSET+48, LARGE_FLASH_ECC_OFFSET+49, LARGE_FLASH_ECC_OFFSET+50
		},
		.oobfree = {
#ifdef RALINK_NAND_BMT
			{ .offset =  4, .length =  1 },
#else
			{ .offset =  0, .length =  5 },
#endif
			{ .offset =  8, .length = 13 },
			{ .offset = 24, .length = 13 },
			{ .offset = 40, .length = 11 },
			{ .offset = 52, .length =  1 },
			{ .offset = 56, .length =  8 }
		},
#ifdef RALINK_NAND_BMT
		.oobavail = 47,
#else
		.oobavail = 51,
#endif
		// 2009th byte is bad-block flag.
	}
};

#endif /* _RT6XXX_NAND_TABLE_H */
