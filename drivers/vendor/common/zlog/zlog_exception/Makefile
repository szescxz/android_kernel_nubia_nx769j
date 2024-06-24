
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_exception
obj-$(CONFIG_VENDOR_ZTE_LOG_EXCEPTION) := zlog_exception_dlkm.o
zlog_exception_dlkm-objs += zlog_exception.o

ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
endif

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/zlog_exception_dlkm.ko




