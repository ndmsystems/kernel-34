#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/smp-ops.h>
#include <asm/mipsmtregs.h>
#include <asm/cpu.h>

#include <asm/mips-boards/prom.h>
#include <asm/tc3162/tc3162.h>

extern struct plat_smp_ops msmtc_smp_ops;

unsigned int surfboard_sysclk;
unsigned int tc_mips_cpu_freq;

#ifdef CONFIG_UBOOT_CMDLINE
int prom_argc;
int *_prom_argv, *_prom_envp;

/*
 * YAMON (32-bit PROM) pass arguments and environment as 32-bit pointer.
 * This macro take care of sign extension, if running in 64-bit mode.
 */
#define prom_envp(index) ((char *)(((int *)(int)_prom_envp)[(index)]))
#endif

char *prom_getenv(char *envname)
{
#ifdef CONFIG_UBOOT_CMDLINE
	/*
	 * Return a pointer to the given environment variable.
	 * In 64-bit mode: we're using 64-bit pointers, but all pointers
	 * in the PROM structures are only 32-bit, so we need some
	 * workarounds, if we are running in 64-bit mode.
	 */
	int i, index=0;
	char *p, *q;

	i = strlen(envname);
	while (prom_envp(index)) {
		p = (char*) KSEG0ADDR(prom_envp(index));
		if(!strncmp(envname, p, i)) {
			q = strchr(p, '=');
			if (q)
				q++;
			return q;
		}
		index++;
	}
#endif
	return NULL;
}

const char *get_system_type(void)
{
#ifdef __BIG_ENDIAN
	if (isRT63368)
		return "Ralink RT63368 SoC";
	else
		return "Ralink RT63365 SoC";
#else
	if (isRT63368)
		return "Ralink RT6856 SoC";
	else
		return "Ralink RT6855A SoC";
#endif
}

static inline void tc_mips_setup(void)
{
#ifndef CONFIG_SMP
	/* enable 34K/1004K external sync */
	unsigned int oconfig7 = read_c0_config7();
	unsigned int nconfig7 = oconfig7;

	nconfig7 |= (1 << 8);
	if (oconfig7 != nconfig7) {
		__asm__ __volatile("sync");
		write_c0_config7(nconfig7);
		ehb();
	}
#else
	strcat(arcs_cmdline, " es=1");		// enable external sync
#ifdef CONFIG_MIPS_MT_SMTC
	strcat(arcs_cmdline, " maxtcs=4");	// 4 TC
	strcat(arcs_cmdline, " vpe0tcs=3");	// VPE0 have 3 TC
	strcat(arcs_cmdline, " ipibufs=32");	// force 32 buffers for IPI
#endif
#endif
}

static inline void tc_uart_setup(void)
{
	unsigned int div_x, div_y, word;

	/* Set FIFO controo enable, reset RFIFO, TFIFO, 16550 mode, watermark=0x00 (1 byte) */
	VPchar(CR_HSUART_FCR) = UART_FCR | UART_WATERMARK;

	/* Set modem control to 0 */
	VPchar(CR_HSUART_MCR) = UART_MCR;

	/* Disable IRDA, Disable Power Saving Mode, RTS , CTS flow control */
	VPchar(CR_HSUART_MISCC) = UART_MISCC;

	/* Access the baudrate divider */
	VPchar(CR_HSUART_LCR) = UART_BRD_ACCESS;

	div_y = UART_XYD_Y;
#if defined (CONFIG_RT2880_UART_115200)
	div_x = 59904;	// Baud rate 115200
#else
	div_x = 29952;	// Baud rate 57600
#endif
	word = (div_x << 16) | div_y;
	VPint(CR_HSUART_XYD) = word;

	/* Set Baud Rate Divisor to 1*16 */
	VPchar(CR_HSUART_BRDL) = UART_BRDL_20M;
	VPchar(CR_HSUART_BRDH) = UART_BRDH_20M;

	/* Set DLAB = 0, clength = 8, stop = 1, no parity check */
	VPchar(CR_HSUART_LCR) = UART_LCR;
}

static inline void tc_ahb_setup(void)
{
#ifdef CONFIG_RALINK_RT63365
	/* assert DMT reset */
	VPint(CR_AHB_DMTCR) = 0x1;
	udelay(100);

#ifndef CONFIG_TC3162_ADSL
	/* disable DMT clock to power save */
	VPint(CR_AHB_DMTCR) = 0x3;
#endif
#endif
	/* setup bus timeout value */
	VPint(CR_AHB_AACS) = 0xffff;
}

void __init prom_init(void)
{
	unsigned long memsize;
	unsigned int hw_conf, bus_freq, cpu_freq, cpu_ratio;
	const char *ram_type = "SDRAM";

	if (!isRT63365) {
		/* Unsupported hardware */
		BUG();
	}

	mips_machtype = MACH_RALINK_ROUTER;

#ifdef CONFIG_UBOOT_CMDLINE
	prom_argc = fw_arg0;
	_prom_argv = (int*) fw_arg1;
	_prom_envp = (int*) fw_arg2;
#endif

	set_io_port_base(KSEG1);

	prom_init_cmdline();

	tc_mips_setup();
	tc_uart_setup();
	tc_ahb_setup();

	hw_conf = VPint(CR_AHB_HWCONF);

	/* DDR */
	if (hw_conf & (1<<25)) {
		ram_type = (hw_conf & (1<<24)) ? "DDR2" : "DDR";
		cpu_ratio = (hw_conf & (1<<26)) ? 4 : 3;
		memsize = 0x800000 * (1 << (((VPint(CR_DMC_DDR_CFG1) >> 18) & 0x7) - 1));
	/* SDRAM */
	} else {
		unsigned int sdram_cfg1, col, row;
		
		cpu_ratio = 4;
		
		/* calculate SDRAM size */
		sdram_cfg1 = VPint(0xbfb20004);
		row = 11 + ((sdram_cfg1>>16) & 0x3);
		col = 8 + ((sdram_cfg1>>20) & 0x3);
		/* 4 bands and 16 bit width */
		memsize = (1 << row) * (1 << col) * 4 * 2;
	}

	add_memory_region(0, memsize, BOOT_MEM_RAM);

	bus_freq = SYS_HCLK;
	cpu_freq = bus_freq * cpu_ratio;
	if (cpu_freq % 2)
		cpu_freq += 1;

	surfboard_sysclk = bus_freq * 1000 * 1000;
	tc_mips_cpu_freq = cpu_freq * 1000 * 1000;

	printk(KERN_INFO "%s: RAM: %s %luMB\n",
		get_system_type(),
		ram_type,
		memsize / 1024 / 1024);

	printk(KERN_INFO "CPU/SYS frequency: %u/%u MHz\n",
		cpu_freq,
		bus_freq);

#ifdef CONFIG_MIPS_MT_SMP
	register_vsmp_smp_ops();
#endif
#ifdef CONFIG_MIPS_MT_SMTC
	register_smp_ops(&msmtc_smp_ops);
#endif
}

void __init prom_free_prom_memory(void)
{
	/* We do not have any memory to free */
}

