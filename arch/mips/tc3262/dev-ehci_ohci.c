#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>

#include <asm/irq.h>
#include <asm/rt2880/surfboardint.h>
#include <asm/rt2880/rt_mmap.h>

#define RT6XXX_EHCI_MEM_START	(RALINK_USB_HOST_BASE)
#define RT6XXX_OHCI_MEM_START	(RALINK_USB_HOST_BASE - 0x10000)

static struct resource rt6xxx_ehci_resources[] = {
	[0] = {
		.start  = RT6XXX_EHCI_MEM_START,
		.end    = RT6XXX_EHCI_MEM_START + 0xfff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SURFBOARDINT_UHST,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rt6xxx_ohci_resources[] = {
	[0] = {
		.start  = RT6XXX_OHCI_MEM_START,
		.end    = RT6XXX_OHCI_MEM_START + 0xfff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SURFBOARDINT_UHST,
		.flags  = IORESOURCE_IRQ,
	},
};

static u64 rt6xxx_ehci_dmamask = DMA_BIT_MASK(32);
static u64 rt6xxx_ohci_dmamask = DMA_BIT_MASK(32);

static atomic_t rt6xxx_power_instance = ATOMIC_INIT(0);

#define RSTCTRL_REG	(RALINK_SYSCTL_BASE + 0x834)

static void rt_usb_wake_up(void)
{
	u32 val;

	/* perform reset */
	val = *(volatile u32 *)(RSTCTRL_REG);
	if (!(val & (RALINK_UHST_RST | RALINK_UDEV_RST))) {
		val |= (RALINK_UHST_RST | RALINK_UDEV_RST);
		*(volatile u32 *)(RSTCTRL_REG) = val;
		mdelay(1);
		val = *(volatile u32 *)(RSTCTRL_REG);
	}

	/* release reset */
	val &= ~(RALINK_UHST_RST | RALINK_UDEV_RST);
	*(volatile u32 *)(RSTCTRL_REG) = val;
	mdelay(100);
}

static void rt_usb_sleep(void)
{
	u32 val;

	/* raise reset */
	val = *(volatile u32 *)(RSTCTRL_REG);
	val |= (RALINK_UHST_RST | RALINK_UDEV_RST);
	*(volatile u32 *)(RSTCTRL_REG) = val;
	udelay(100);
}

static int rt6xxx_power_on(struct platform_device *pdev)
{
	if (atomic_inc_return(&rt6xxx_power_instance) == 1)
		rt_usb_wake_up();

	return 0;
}

static void rt6xxx_power_off(struct platform_device *pdev)
{
	if (atomic_dec_return(&rt6xxx_power_instance) == 0)
		rt_usb_sleep();
}

static struct usb_ehci_pdata rt6xxx_ehci_pdata = {
	.caps_offset		= 0,
	.has_synopsys_hc_bug	= 1,
	.port_power_off		= 1,
	.power_on		= rt6xxx_power_on,
	.power_off		= rt6xxx_power_off,
};

static struct usb_ohci_pdata rt6xxx_ohci_pdata = {
	.power_on		= rt6xxx_power_on,
	.power_off		= rt6xxx_power_off,
};

static struct platform_device rt6xxx_ehci_device = {
	.name		= "ehci-platform",
	.id		= -1,
	.dev		= {
		.platform_data = &rt6xxx_ehci_pdata,
		.dma_mask = &rt6xxx_ehci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(rt6xxx_ehci_resources),
	.resource	= rt6xxx_ehci_resources,
};

static struct platform_device rt6xxx_ohci_device = {
	.name		= "ohci-platform",
	.id		= -1,
	.dev		= {
		.platform_data = &rt6xxx_ohci_pdata,
		.dma_mask = &rt6xxx_ohci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(rt6xxx_ohci_resources),
	.resource	= rt6xxx_ohci_resources,
};

static struct platform_device *rt6xxx_devices[] __initdata = {
	&rt6xxx_ehci_device,
	&rt6xxx_ohci_device,
};

int __init init_rt6xxx_ehci_ohci(void)
{
	int retval = 0;

	retval = platform_add_devices(rt6xxx_devices, ARRAY_SIZE(rt6xxx_devices));
	if (retval != 0) {
		printk(KERN_ERR "register %s device fail!\n", "EHCI/OHCI");
		return retval;
	}

	return retval;
}

device_initcall(init_rt6xxx_ehci_ohci);
