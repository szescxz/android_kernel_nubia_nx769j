# Makefile for the focaltech touchscreen drivers.
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_3681/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_3681/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_core.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_ex_fun.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_ex_mode.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_gesture.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_esdcheck.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)  +=  focaltech_point_report_check.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)  +=  focaltech_test/
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_flash.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)	+=  focaltech_common_interface.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)  +=  focaltech_i2c.o
obj-$(CONFIG_TOUCHSCREEN_FTS_3681)  +=  focaltech_flash/focaltech_upgrade_ft3681.o
