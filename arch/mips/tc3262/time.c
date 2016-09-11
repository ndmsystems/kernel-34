#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/timer.h>

#include <asm/time.h>

#include <asm/tc3162/tc3162.h>

extern unsigned int surfboard_sysclk;
extern unsigned int tc_mips_cpu_freq;

extern void tc_setup_watchdog_irq(void);
extern void tc_setup_bus_timeout_irq(void);

static void tc_timer_ctl(
	unsigned int timer_no,
	unsigned int timer_enable,
	unsigned int timer_mode,
	unsigned int timer_halt)
{
	unsigned int word;

	timer_enable &= 0x1;

	word = VPint(CR_TIMER_CTL);
	word &= ~(1u << timer_no);
	word |=  (timer_enable << timer_no);
	word |=  (timer_mode << (timer_no + 8));
	word |=  (timer_halt << (timer_no + 26));
	VPint(CR_TIMER_CTL) = word;
}

void tc_timer_wdg(
	unsigned int tick_enable,
	unsigned int wdg_enable)
{
	unsigned int word;

	word = VPint(CR_TIMER_CTL);
	word &= 0xfdffffdf;
	word |= ((tick_enable & 0x1) << 5);
	word |= ((wdg_enable & 0x1) << 25);
	VPint(CR_TIMER_CTL) = word;
}
EXPORT_SYMBOL(tc_timer_wdg);

void tc_timer_set(
	unsigned int timer_no,
	unsigned int timerTime,
	unsigned int enable,
	unsigned int mode,
	unsigned int halt)
{
	unsigned int word;

	word = (timerTime * SYS_HCLK) * 500;
	timerLdvSet(timer_no, word);
	tc_timer_ctl(timer_no, enable, mode, halt);
}
EXPORT_SYMBOL(tc_timer_set);

void __init plat_time_init(void)
{
	mips_hpt_frequency = tc_mips_cpu_freq / 2;

	tc_timer_set(1, TIMERTICKS_10MS, ENABLE, TIMER_TOGGLEMODE, TIMER_HALTDISABLE);

	/* setup watchdog timer interrupt */
	tc_setup_watchdog_irq();

	/* set countdown 2 seconds to issue WDG interrupt */
	VPint(CR_WDOG_THSLD) = (2 * TIMERTICKS_1S * SYS_HCLK) * 500;

	/* setup bus timeout interrupt */
	tc_setup_bus_timeout_irq();
//	VPint(CR_MON_TMR) |= ((1<<30) | (0xff));
}

unsigned int get_surfboard_sysclk(void)
{
	return surfboard_sysclk;
}

EXPORT_SYMBOL(get_surfboard_sysclk);
