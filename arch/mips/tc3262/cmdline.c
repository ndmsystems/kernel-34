#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <asm/bootinfo.h>

#if defined (CONFIG_CMDLINE_BOOL)
char rt2880_cmdline[] = CONFIG_CMDLINE;
#else
#if defined (CONFIG_RT2880_UART_115200)
#define TTY_BAUDRATE	"115200n8"
#else
#define TTY_BAUDRATE	"57600n8"
#endif
char rt2880_cmdline[]="console=ttyS0," TTY_BAUDRATE "";
#endif

#ifdef CONFIG_UBOOT_CMDLINE
extern int prom_argc;
extern int *_prom_argv;

/*
 * YAMON (32-bit PROM) pass arguments and environment as 32-bit pointer.
 * This macro take care of sign extension.
 */
#define prom_argv(index) ((char *)(((int *)(int)_prom_argv)[(index)]))
#endif

extern char arcs_cmdline[COMMAND_LINE_SIZE];

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

#ifdef CONFIG_IMAGE_CMDLINE_HACK
extern char __image_cmdline[];

static int __init use_image_cmdline(void)
{
	char *p = __image_cmdline;
	int replace = 0;

	if (*p == '-') {
		replace = 1;
		p++;
	}

	if (*p == '\0')
		return 0;

	if (replace) {
		strlcpy(arcs_cmdline, p, sizeof(arcs_cmdline));
	} else {
		strlcat(arcs_cmdline, " ", sizeof(arcs_cmdline));
		strlcat(arcs_cmdline, p, sizeof(arcs_cmdline));
	}

	return 1;
}
#else
static int inline use_image_cmdline(void) { return 0; }
#endif

void  __init prom_init_cmdline(void)
{
#ifdef CONFIG_UBOOT_CMDLINE
	int actr=1; /* Always ignore argv[0] */
#endif
	char *cp;

	if (use_image_cmdline())
		return;

	cp = &(arcs_cmdline[0]);
#ifdef CONFIG_UBOOT_CMDLINE
	if (prom_argc > 1) {
		while(actr < prom_argc) {
			strcpy(cp, prom_argv(actr));
			cp += strlen(prom_argv(actr));
			*cp++ = ' ';
			actr++;
		}
	} else
#endif
	{
		strcpy(cp, rt2880_cmdline);
		cp += strlen(rt2880_cmdline);
		*cp++ = ' ';
	}

	if (cp != &(arcs_cmdline[0])) /* get rid of trailing space */
		--cp;
	*cp = '\0';
}

