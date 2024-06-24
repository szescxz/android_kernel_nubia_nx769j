# When adding new entries keep the list in alphabetical order
CFLAGS_scm.o :=$(call as-instr,.arch_extension sec,-DREQUIRES_SEC=1)

obj-$(CONFIG_VENDOR_PMLOG)				+= pm_debug.o
pm_debug-y := pm_debugging.o
pm_debug-y += pm_subsystems_sleep.o
#obj-$(CONFIG_VENDOR_PMLOG)				+= gpio_show_suspend.o
