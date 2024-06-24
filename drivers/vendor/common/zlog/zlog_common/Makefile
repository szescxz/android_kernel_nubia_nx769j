
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_exception
obj-$(CONFIG_VENDOR_ZTE_LOG_COMMON) := zlog_common_dlkm.o
zlog_common_dlkm-objs += zlog_common.o


ifeq ($(CONFIG_VENDOR_ZTE_LOG_COMMON),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_COMMON
endif

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/zlog_common_dlkm.ko



