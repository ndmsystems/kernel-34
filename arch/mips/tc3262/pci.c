/**************************************************************************
 *
 *  BRIEF MODULE DESCRIPTION
 *     PCI init for Ralink RT2880 solution
 *
 *  Copyright 2007 Ralink Inc. (bruce_chang@ralinktech.com.tw)
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
 * May 2007 Bruce Chang
 * Initial Release
 *
 * May 2009 Bruce Chang
 * support RT2880/RT3883 PCIe
 *
 * May 2011 Bruce Chang
 * support RT6855/MT7620 PCIe
 *
 * Sep 2011 Bruce Chang
 * support RT6855A PCIe architecture
 *
 **************************************************************************
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/pci.h>

#include <asm/pci.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_PCI
#include <asm/rt2880/surfboardint.h>
#include <asm/rt2880/eureka_ep430.h>

//#define RAPCI_DEBUG

#define RALINK_RSTCTRL_REG		(RALINK_SYSCTL_BASE + 0x834)
#define RALINK_PCIEC_REG		(RALINK_SYSCTL_BASE + 0x088)
#define RALINK_SSR_REG			(RALINK_SYSCTL_BASE + 0x090)

#define RALINK_PCI_MM_MAP_BASE		0x20000000
#define RALINK_PCI_IO_MAP_BASE		0x1f600000
#define BAR0_MASK			0x7FFF0000	/* 2G */
#define BAR0_MEMORY_BASE		0x0

/*
 * These functions and structures provide the BIOS scan and mapping of the PCI
 * devices.
 */
#define PCI_ACCESS_READ_1		0
#define PCI_ACCESS_READ_2		1
#define PCI_ACCESS_READ_4		2
#define PCI_ACCESS_WRITE_1		3
#define PCI_ACCESS_WRITE_2		4
#define PCI_ACCESS_WRITE_4		5

static int pcie_link_status = 0;
static DEFINE_SPINLOCK(asic_pcr_lock);

static int config_access(int access_type, u32 busn, u32 slot, u32 func, u32 where, u32 *data)
{
	unsigned int address, shift, tmp;
	unsigned long flags;

	/* setup PCR address */
	address = (busn << 24) | (slot << 19) | (func << 16) | (where & 0xffc);

	shift = (where & 0x3) << 3;

	spin_lock_irqsave(&asic_pcr_lock, flags);

	/* start the configuration cycle */
	RALINK_PCI_PCR_ADDR = address;

	switch (access_type) {
	case PCI_ACCESS_WRITE_1:
		tmp = RALINK_PCI_PCR_DATA;
		tmp &= ~(0xff << shift);
		tmp |= ((*data & 0xff) << shift);
		RALINK_PCI_PCR_DATA = tmp;
		break;
	case PCI_ACCESS_WRITE_2:
		tmp = RALINK_PCI_PCR_DATA;
		if (shift > 16)
			shift = 16;
		tmp &= ~(0xffff << shift);
		tmp |= ((*data & 0xffff) << shift);
		RALINK_PCI_PCR_DATA = tmp;
		break;
	case PCI_ACCESS_WRITE_4:
		RALINK_PCI_PCR_DATA = *data;
		break;
	case PCI_ACCESS_READ_1:
		tmp = RALINK_PCI_PCR_DATA;
		*data = (tmp >> shift) & 0xff;
		break;
	case PCI_ACCESS_READ_2:
		tmp = RALINK_PCI_PCR_DATA;
		if (shift > 16)
			shift = 16;
		*data = (tmp >> shift) & 0xffff;
		break;
	case PCI_ACCESS_READ_4:
		*data = RALINK_PCI_PCR_DATA;
		break;
	}

	spin_unlock_irqrestore(&asic_pcr_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int ralink_pci_config_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	u32 busn = bus->number;
	u32 slot = PCI_SLOT(devfn);
	u32 func = PCI_FUNC(devfn);
	int access_type = PCI_ACCESS_READ_4;

	switch (size) {
	case 1:
		access_type = PCI_ACCESS_READ_1;
		break;
	case 2:
		access_type = PCI_ACCESS_READ_2;
		break;
	}

	return config_access(access_type, busn, slot, func, (u32)where, val);
}

static int ralink_pci_config_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	u32 busn = bus->number;
	u32 slot = PCI_SLOT(devfn);
	u32 func = PCI_FUNC(devfn);
	int access_type = PCI_ACCESS_WRITE_4;

	switch (size) {
	case 1:
		access_type = PCI_ACCESS_WRITE_1;
		break;
	case 2:
		access_type = PCI_ACCESS_WRITE_2;
		break;
	}

	return config_access(access_type, busn, slot, func, (u32)where, &val);
}

/*
 *  General-purpose PCI functions.
 */

struct pci_ops ralink_pci_ops = {
	.read		 = ralink_pci_config_read,
	.write		 = ralink_pci_config_write,
};

static struct resource ralink_res_pci_mem1 = {
	.name		 = "PCI MEM1",
	.start		 = RALINK_PCI_MM_MAP_BASE,
	.end		 = (RALINK_PCI_MM_MAP_BASE + 0x0fffffff),
	.flags		 = IORESOURCE_MEM,
};

static struct resource ralink_res_pci_io1 = {
	.name		 = "PCI I/O1",
	.start		 = RALINK_PCI_IO_MAP_BASE,
	.end		 = (RALINK_PCI_IO_MAP_BASE + 0x0ffff),
	.flags		 = IORESOURCE_IO,
};

struct pci_controller ralink_pci_controller = {
	.pci_ops	 = &ralink_pci_ops,
	.mem_resource	 = &ralink_res_pci_mem1,
	.io_resource	 = &ralink_res_pci_io1,
	.mem_offset	 = 0x00000000UL,
	.io_offset	 = 0x00000000UL,
};

int pcibios_plat_dev_init(struct pci_dev *dev)
{
#ifdef RAPCI_DEBUG
	u32 i, val;
	struct resource *res;

	printk("%s: ** bus: %d, slot: 0x%x\n",
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn));

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &val);
	printk(" PCI_BASE_ADDRESS_0: 0x%08X\n", val);

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_1, &val);
	printk(" PCI_BASE_ADDRESS_1: 0x%08X\n", val);

	pci_read_config_dword(dev, PCI_IO_BASE, &val);
	printk(" PCI_IO_BASE: 0x%08X\n", val);

	for (i = 0; i < 2; i++) {
		res = (struct resource*)&dev->resource[i];
		printk(" res[%d]->start = %x\n", i, res->start);
		printk(" res[%d]->end = %x\n", i, res->end);
	}
#endif

	/* P2P bridge */
	if (dev->bus->number == 0) {
		/* set CLS */
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES >> 2));
	}

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
#else
int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
#endif
{
	int pci_irq = 0;

	if ((dev->bus->number == 1) && (slot == 0x0)) {
#if defined (CONFIG_PCIE_PORT1)
		if ((pcie_link_status & 0x3) == 0x2)
			pci_irq = SURFBOARDINT_PCIE1;
		else
#endif
			pci_irq = SURFBOARDINT_PCIE0;
	}
#if defined (CONFIG_PCIE_PORT1)
	else if ((dev->bus->number == 1) && (slot == 0x1)) {
		pci_irq = SURFBOARDINT_PCIE1;
	} else if ((dev->bus->number == 2) && (slot == 0x0)) {
		pci_irq = SURFBOARDINT_PCIE1;
	} else if ((dev->bus->number == 2) && (slot == 0x1)) {
		pci_irq = SURFBOARDINT_PCIE1;
	}
#endif

#ifdef RAPCI_DEBUG
	printk("%s: ** bus: %d, slot: 0x%x -> irq: %d\n", __FUNCTION__,
		dev->bus->number, slot, pci_irq);
#endif

	return pci_irq;
}

int __init init_ralink_pci(void)
{
	u32 i, val;

	PCIBIOS_MIN_IO = 0;
	PCIBIOS_MIN_MEM = 0;

	/* raise PCIRST */
	val = RALINK_PCI_PCICFG_ADDR;
	if (!(val & (1<<1))) {
		val |= (1<<1);
		RALINK_PCI_PCICFG_ADDR = val;
		mdelay(1);
	}

#if defined (CONFIG_PCIE_PORT1)
	/* assert PCIe RC0/RC1 reset signal */
	*((volatile u32 *)(RALINK_RSTCTRL_REG)) |= (RALINK_PCIE0_RST | RALINK_PCIE1_RST);
	udelay(100);

	/* de-assert PCIe RC0/RC1 reset signal */
	*((volatile u32 *)(RALINK_RSTCTRL_REG)) &= ~(RALINK_PCIE0_RST | RALINK_PCIE1_RST);
#else
	/* PCIEC: Port1(bit22) disable */
	*((volatile u32 *)(RALINK_PCIEC_REG)) &= ~(1<<22);
	mdelay(1);

	/* assert PCIe RC0/RC1 reset signal */
	*((volatile u32 *)(RALINK_RSTCTRL_REG)) |= (RALINK_PCIE0_RST | RALINK_PCIE1_RST);
	udelay(100);

	/* disable reference clock of dev1 */
	*((volatile u32 *)(RALINK_SSR_REG)) &= ~(1<<3);

	/* de-assert PCIe RC0 reset signal */
	*((volatile u32 *)(RALINK_RSTCTRL_REG)) &= ~(RALINK_PCIE0_RST);
#endif
	mdelay(1);

	/* release PCIRST */
	RALINK_PCI_PCICFG_ADDR &= ~(1<<1);

	/* wait before detect card in slots */
	mdelay(100);
	for (i = 0; i < 500; i++) {
		if ((RALINK_PCI0_STATUS & 0x1)
#if defined (CONFIG_PCIE_PORT1)
		 && (RALINK_PCI1_STATUS & 0x1)
#endif
		    )
			break;
		mdelay(1);
	}

	if ((RALINK_PCI0_STATUS & 0x1) == 0) {
		/* PCIEC: Port0 disable */
		*((volatile u32 *)(RALINK_PCIEC_REG)) &= ~(1<<23);

		printk(KERN_WARNING "PCIe%d no card, disable it\n", 0);
	} else {
		pcie_link_status |= 0x1;
	}

#if defined (CONFIG_PCIE_PORT1)
	if ((RALINK_PCI1_STATUS & 0x1) == 0) {
		/* PCIEC: Port1 disable */
		*((volatile u32 *)(RALINK_PCIEC_REG)) &= ~(1<<22);
		mdelay(1);

		/* assert PCIe RC1 reset signal */
		*((volatile u32 *)(RALINK_RSTCTRL_REG)) |= RALINK_PCIE1_RST;
		udelay(100);

		/* disable reference clock of dev1 */
		*((volatile u32 *)(RALINK_SSR_REG)) &= ~(1<<3);

		printk(KERN_WARNING "PCIe%d no card, disable it\n", 1);
	} else {
		pcie_link_status |= 0x2;
	}
#endif

	if (!pcie_link_status)
		return 0;

	if (!(pcie_link_status & 0x1)) {
		/* PCIe1 only */
		RALINK_PCI_PCICFG_ADDR &= ~(0xff<<16);
		RALINK_PCI_PCICFG_ADDR |=  (0x01<<16);
	}

	RALINK_PCI_MEMBASE = 0xffffffff;			// valid for PCI host mode only
	RALINK_PCI_IOBASE = RALINK_PCI_IO_MAP_BASE;		// valid for PCI host mode only

	// PCIe0
	if (pcie_link_status & 0x1) {
		RALINK_PCI0_BAR0SETUP_ADDR	= BAR0_MASK;	// disable BAR0
		RALINK_PCI0_IMBASEBAR0_ADDR	= BAR0_MEMORY_BASE;
		RALINK_PCI0_CLASS		= 0x06040001;
		RALINK_PCI0_BAR0SETUP_ADDR	= BAR0_MASK|1;	// open BAR0
		RALINK_PCI_PCIMSK_ADDR		|= (1<<20);	// enable PCIe0 interrupt
	}

#if defined (CONFIG_PCIE_PORT1)
	// PCIe1
	if (pcie_link_status & 0x2) {
		RALINK_PCI1_BAR0SETUP_ADDR	= BAR0_MASK;	// disable BAR0
		RALINK_PCI1_IMBASEBAR0_ADDR	= BAR0_MEMORY_BASE;
		RALINK_PCI1_CLASS		= 0x06040001;
		RALINK_PCI1_BAR0SETUP_ADDR	= BAR0_MASK|1;	// open BAR0
		RALINK_PCI_PCIMSK_ADDR		|= (1<<21);	// enable PCIe1 interrupt
	}
#endif

	/* Enable CRC count */
	val = 0;
	config_access(PCI_ACCESS_READ_4, 0, 0, 0, 0x118, &val);
	if (!(val & (1<<8))) {
		val |= (1<<8);
		config_access(PCI_ACCESS_WRITE_4, 0, 0, 0, 0x118, &val);
	}

	ralink_pci_controller.io_map_base = mips_io_port_base;

	register_pci_controller(&ralink_pci_controller);

	return 0;
}

arch_initcall(init_ralink_pci);

#endif /* CONFIG_PCI */
