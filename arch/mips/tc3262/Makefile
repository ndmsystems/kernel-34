
obj-y := prom.o cmdline.o printf.o setup.o time.o irq.o

ifdef CONFIG_ECONET_EN75XX_MP
obj-$(CONFIG_PCI) += pci_en75xx.o
else
obj-$(CONFIG_PCI) += pci.o
endif
obj-$(CONFIG_RT3XXX_EHCI_OHCI) += dev-ehci_ohci.o
obj-$(CONFIG_RALINK_TIMER_WDG) += rt_timer_wdg.o
obj-$(CONFIG_MIPS_MT_SMP) += malta-smp.o
obj-$(CONFIG_MIPS_MT_SMTC) += malta-smtc.o
