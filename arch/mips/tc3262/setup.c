#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <asm/reboot.h>
#include <asm/time.h>

#include <asm/tc3162/tc3162.h>
#ifdef CONFIG_TC3162_ADSL
struct sk_buff;
#include <asm/tc3162/TCIfSetQuery_os.h>
#endif

void (*back_to_prom)(void) = (void (*)(void))0xbfc00000;

#ifdef CONFIG_TC3162_ADSL
adsldev_ops *adsl_dev_ops = NULL;
EXPORT_SYMBOL(adsl_dev_ops);

void stop_adsl_dmt(void)
{
	/* stop adsl */
	if (adsl_dev_ops)
		adsl_dev_ops->set(ADSL_SET_DMT_CLOSE, NULL, NULL);
}
#endif

static void hw_reset(void)
{
#ifdef CONFIG_TC3162_ADSL
	/* stop adsl */
	stop_adsl_dmt();
#endif

	/* stop each module dma task */
	VPint(CR_INTC_IMR) = 0x0;
	VPint(CR_TIMER_CTL) = 0x0;

	/* stop atm sar dma */
	TSARM_GFR &= ~((1 << 1) | (1 << 0));

	/* reset USB */
	/* reset USB DMA */
	VPint(CR_USB_SYS_CTRL_REG) |= (1 << 31);
	/* reset USB SIE */
	VPint(CR_USB_DEV_CTRL_REG) |= (1 << 30);
	mdelay(5);

	/* restore USB SIE */
	VPint(CR_USB_DEV_CTRL_REG) &= ~(1 << 30);
	mdelay(5);
	VPint(CR_USB_SYS_CTRL_REG) &= ~(1 << 31);

	/* watchdog reset 100ms */
	tc_timer_set(TC_TIMER_WDG, 10 * TIMERTICKS_10MS, ENABLE,
		     TIMER_TOGGLEMODE, TIMER_HALTDISABLE);
	tc_timer_wdg(ENABLE, ENABLE);

	while (1);
}

static void tc_machine_restart(char *command)
{
	printk(KERN_WARNING "Machine restart ... \n");
	hw_reset();
	back_to_prom();
}

static void tc_machine_halt(void)
{
	printk(KERN_WARNING "Machine halted ... \n");
	hw_reset();
	while (1);
}

static void tc_machine_power_off(void)
{
	printk(KERN_WARNING "Machine poweroff ... \n");
	hw_reset();
	while (1);
}

static int tc_panic_event(struct notifier_block *this,
			  unsigned long event, void *ptr)
{
	tc_machine_restart(NULL);

	return NOTIFY_DONE;
}

static struct notifier_block tc_panic_block = {
	.notifier_call = tc_panic_event,
};

static void tc_reboot_setup(void)
{
	_machine_restart = tc_machine_restart;
	_machine_halt = tc_machine_halt;
	pm_power_off = tc_machine_power_off;

	atomic_notifier_chain_register(&panic_notifier_list, &tc_panic_block);
}

void __init plat_mem_setup(void)
{
	iomem_resource.start = 0;
	iomem_resource.end = ~0;

	ioport_resource.start = 0;
	ioport_resource.end = 0x1fffffff;

	tc_reboot_setup();
}
