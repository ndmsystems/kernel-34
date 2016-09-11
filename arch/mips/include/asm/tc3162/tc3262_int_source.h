/************************************************************************
 *
 *	Copyright (C) 2006 Trendchip Technologies, Corp.
 *	All Rights Reserved.
 *
 * Trendchip Confidential; Need to Know only.
 * Protected as an unpublished work.
 *
 * The computer program listings, specifications and documentation
 * herein are the property of Trendchip Technologies, Co. and shall
 * not be reproduced, copied, disclosed, or used in whole or in part
 * for any reason without the prior express written permission of
 * Trendchip Technologeis, Co.
 *
 *************************************************************************/

#ifndef _TC3262_INT_SOURCE_H_
#define _TC3262_INT_SOURCE_H_

enum interrupt_source
{
	DUMMY_INT,
	UART_INT,		//0 	IPL10
	PTM_B0_INT,		//1
	SI_SWINT1_INT0,		//2
	SI_SWINT1_INT1,		//3
	TIMER0_INT,		//4 	IPL1
	TIMER1_INT,		//5 	IPL5
	TIMER2_INT,		//6 	IPL6
	SI_SWINT_INT0, 		//7
	SI_SWINT_INT1,		//8
	TIMER5_INT, 		//9 	IPL9
	GPIO_INT,		//10	IPL11
	PCM_INT,		//11	IPL20
	SI_PC1_INT,		//12
	SI_PC_INT, 		//13
	APB_DMA0_INT,		//14	IPL12
	ESW_INT,		//15	IPL13
	HSUART_INT,		//16	IPL23
	IRQ_RT3XXX_USB,		//17	IPL24
	DYINGGASP_INT,		//18	IPL25
	DMT_INT,		//19	IPL26
	UNUSED0_INT,		//20
	FE_MAC_INT,		//21	IPL3
	SAR_INT,		//22	IPL2
	PCIE1_INT,		//23
	PCIE_INT,		//24
	NFC_INT,		//25
	UNUSED1_INT,		//26	IPL15
	SPI_MC_INT,		//27	IPL16
	CRYPTO_INT,		//28	IPL17
	SI_TIMER1_INT,		//29
	SI_TIMER_INT,		//30
	SWR_INT,		//31	IPL4
	BUS_TOUT_INT		//32
};

#endif /* _TC3262_INT_SOURCE_H_ */
