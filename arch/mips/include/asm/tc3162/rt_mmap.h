/**************************************************************************
 *
 *  BRIEF MODULE DESCRIPTION
 *     register definition for Ralink RT-series SoC
 *
 *  Copyright 2007 Ralink Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 **************************************************************************
 */

#ifndef __ASM_MACH_MIPS_TC3262_RT_MMAP_H
#define __ASM_MACH_MIPS_TC3262_RT_MMAP_H

#if defined(CONFIG_ECONET_EN75XX_MP)

#define RALINK_SYSCTL_BASE		0xBFB00000
#define RALINK_TIMER_BASE		0xBFBF0100
#define RALINK_INTCL_BASE		0xBFB40000
#define RALINK_MEMCTRL_BASE		0xBFB20000
#define RALINK_PIO_BASE			0xBFBF0200
#define RALINK_I2C_BASE			0xBFBF8000
#define RALINK_UART_LITE_BASE		0xBFBF0000
#define RALINK_UART_LITE2_BASE		0xBFBF0300
#define RALINK_PCM_BASE			0xBFBD0000
#define RALINK_GDMA_BASE		0xBFB30000
#define RALINK_FRAME_ENGINE_BASE	0xBFB50000
#define RALINK_ETH_SW_BASE		0xBFB58000
#define RALINK_CRYPTO_ENGINE_BASE	0xBFB70000
#define RALINK_PCI_BASE			0xBFB80000
#define RALINK_PCI_PHY0_BASE		0xBFAF2000
#define RALINK_PCI_PHY1_BASE		0xBFAC0000
#define RALINK_USB_HOST_BASE		0x1FB90000
#define RALINK_XHCI_HOST_BASE		0xBFB90000
#define RALINK_XHCI_UPHY_BASE		0xBFA80000
#define RALINK_SFC_BASE			0xBFA10000
#define RALINK_CHIP_SCU_BASE		0xBFA20000
#define RALINK_11N_MAC_BASE		0xBFB00000 // Unused

//Interrupt Controller
#define RALINK_INTCTL_UARTLITE		(1<<0)
#define RALINK_INTCTL_PIO		(1<<10)
#define RALINK_INTCTL_PCM		(1<<11)
#define RALINK_INTCTL_DMA		(1<<14)
#define RALINK_INTCTL_GSW		(1<<15)
#define RALINK_INTCTL_UHST		(1<<17)
#define RALINK_INTCTL_FE		(1<<21)
#define RALINK_INTCTL_QDMA		(1<<22)
#define RALINK_INTCTL_PCIE0		(1<<23)
#define RALINK_INTCTL_PCIE1		(1<<24)

//Reset Control Register
#define RALINK_I2S1_RST			(1<<0)
#define RALINK_FE_QDMA_LAN_RST		(1<<1)
#define RALINK_FE_QDMA_WAN_RST		(1<<2)
#define RALINK_PCM2_RST			(1<<4)
#define RALINK_PTM_MAC_RST		(1<<5)
#define RALINK_CRYPTO_RST		(1<<6)
#define RALINK_SAR_RST			(1<<7)
#define RALINK_TIMER_RST		(1<<8)
#define RALINK_INTC_RST			(1<<9)
#define RALINK_BONDING_RST		(1<<10)
#define RALINK_PCM1_RST			(1<<11)
#define RALINK_UART_RST			(1<<12)
#define RALINK_PIO_RST			(1<<13)
#define RALINK_DMA_RST			(1<<14)
#define RALINK_I2C_RST			(1<<16)
#define RALINK_I2S2_RST			(1<<17)
#define RALINK_SPI_RST			(1<<18)
#define RALINK_UARTL_RST		(1<<19)
#define RALINK_FE_RST			(1<<21)
#define RALINK_UHST_RST			(1<<22)
#define RALINK_ESW_RST			(1<<23)
#define RALINK_SFC2_RST			(1<<25)
#define RALINK_PCIE0_RST		(1<<26)
#define RALINK_PCIE1_RST		(1<<27)
#define RALINK_PCIEHB_RST		(1<<29)

#else

#define RALINK_SYSCTL_BASE		0xBFB00000
#define RALINK_TIMER_BASE		0xBFBF0100
#define RALINK_INTCL_BASE		0xBFB40000
#define RALINK_MEMCTRL_BASE		0xBFB20000
#define RALINK_PIO_BASE			0xBFBF0200
#define RALINK_NAND_CTRL_BASE		0xBFBE0010
#define RALINK_NANDECC_CTRL_BASE	0xBFBE0040
#define RALINK_I2C_BASE			0xBFBF8000
#define RALINK_I2S_BASE			0xBFBF8100
#define RALINK_SPI_BASE			0xBFBC0000
#define RALINK_UART_LITE_BASE		0xBFBF0000
#define RALINK_UART_LITE2_BASE		0xBFBF0300
#define RALINK_PCM_BASE			0xBFBD0000
#define RALINK_GDMA_BASE		0xBFB30000
#define RALINK_FRAME_ENGINE_BASE	0xBFB50000
#define RALINK_ETH_SW_BASE		0xBFB58000
#define RALINK_CRYPTO_ENGINE_BASE	0xBFB70000
#define RALINK_PCI_BASE			0xBFB80000
#define RALINK_USB_HOST_BASE		0x1FBB0000
#define RALINK_11N_MAC_BASE		0xBFB00000 // Unused

//Interrupt Controller
#define RALINK_INTCTL_UARTLITE		(1<<0)
#define RALINK_INTCTL_PIO		(1<<10)
#define RALINK_INTCTL_PCM		(1<<11)
#define RALINK_INTCTL_DMA		(1<<14)
#define RALINK_INTCTL_GMAC2		(1<<15)
#define RALINK_INTCTL_PCI		(1<<17)
#define RALINK_INTCTL_UHST2		(1<<20)
#define RALINK_INTCTL_GMAC1		(1<<21)
#define RALINK_INTCTL_UHST1		(1<<23)
#define RALINK_INTCTL_PCIE		(1<<24)
#define RALINK_INTCTL_NAND		(1<<25)
#define RALINK_INTCTL_SPI		(1<<27)

//Reset Control Register
#define RALINK_CRYPTO_RST		(1<<6)
#define RALINK_TIMER_RST		(1<<8)
#define RALINK_INTC_RST			(1<<9)
#define RALINK_MC_RST			(1<<10)
#define RALINK_PCM_RST			(1<<11)
#define RALINK_UART_RST			(1<<12)
#define RALINK_PIO_RST			(1<<13)
#define RALINK_DMA_RST			(1<<14)
#define RALINK_NAND_RST			(1<<15)
#define RALINK_I2C_RST			(1<<16)
#define RALINK_I2S_RST			(1<<17)
#define RALINK_SPI_RST			(1<<18)
#define RALINK_UARTL_RST		(1<<19)
#define RALINK_FE_RST			(1<<21)
#define RALINK_UHST_RST			(1<<22)
#define RALINK_ESW_RST			(1<<23)
#define RALINK_EPHY_RST			(1<<24)
#define RALINK_UDEV_RST			(1<<25)
#define RALINK_PCIE0_RST		(1<<26)
#define RALINK_PCIE1_RST		(1<<27)

#endif

#endif
