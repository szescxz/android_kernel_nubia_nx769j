#
# Makefile for zte touchscreen_v2 driver.
#
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/zlog/zlog_common
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_3681/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_3681/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_9916r/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_9966/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/

zte_tpd-y := ztp_core.o ztp_report_algo.o ztp_state_change.o
zte_tpd-$(CONFIG_TOUCHSCREEN_UFP_MAC) +=  ztp_ufp.o

######################################################
ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_V2)),)
	ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C)),)
		zte_tpd-y += goodix_berlin_driver_v2/goodix_brl_i2c.o
	else
		zte_tpd-y += goodix_berlin_driver_v2/goodix_brl_spi.o
	endif
endif
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_V2) += \
	goodix_berlin_driver_v2/goodix_ts_core.o \
	goodix_berlin_driver_v2/goodix_brl_hw.o \
	goodix_berlin_driver_v2/goodix_cfg_bin.o \
	goodix_berlin_driver_v2/goodix_ts_utils.o \
	goodix_berlin_driver_v2/goodix_brl_fwupdate.o \
	goodix_berlin_driver_v2/goodix_ts_gesture.o \
	goodix_berlin_driver_v2/goodix_ts_inspect.o \
	goodix_berlin_driver_v2/goodix_ts_tools.o \
	goodix_berlin_driver_v2/goodix_common_interface.o

######################################################
ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_9916R)),)
	ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C)),)
		zte_tpd-y += goodix_berlin_driver_9916r/goodix_brl_i2c.o
	else
		zte_tpd-y += goodix_berlin_driver_9916r/goodix_brl_spi.o
	endif
endif
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_9916R) += \
	goodix_berlin_driver_9916r/goodix_brl_fwupdate.o \
	goodix_berlin_driver_9916r/goodix_brl_hw.o \
	goodix_berlin_driver_9916r/goodix_cfg_bin.o \
	goodix_berlin_driver_9916r/goodix_ts_core.o \
	goodix_berlin_driver_9916r/goodix_ts_gesture.o \
	goodix_berlin_driver_9916r/goodix_ts_inspect.o \
	goodix_berlin_driver_9916r/goodix_ts_tools.o \
	goodix_berlin_driver_9916r/goodix_ts_utils.o \
	goodix_berlin_driver_9916r/goodix_common_interface.o		

######################################################
ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_9966)),)
	ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C)),)
		zte_tpd-y += goodix_berlin_driver_9966/goodix_brl_i2c.o
	else
		zte_tpd-y += goodix_berlin_driver_9966/goodix_brl_spi.o
	endif
endif
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_9966) += \
	goodix_berlin_driver_9966/goodix_brl_fwupdate.o \
	goodix_berlin_driver_9966/goodix_brl_hw.o \
	goodix_berlin_driver_9966/goodix_cfg_bin.o \
	goodix_berlin_driver_9966/goodix_ts_core.o \
	goodix_berlin_driver_9966/goodix_ts_gesture.o \
	goodix_berlin_driver_9966/goodix_ts_inspect.o \
	goodix_berlin_driver_9966/goodix_ts_tools.o \
	goodix_berlin_driver_9966/goodix_ts_utils.o \
	goodix_berlin_driver_9966/goodix_common_interface.o

######################################################

zte_tpd-$(CONFIG_TOUCHSCREEN_FTS_3681) += \
	focaltech_touch_3681/focaltech_core.o \
	focaltech_touch_3681/focaltech_ex_fun.o \
	focaltech_touch_3681/focaltech_ex_mode.o \
	focaltech_touch_3681/focaltech_gesture.o \
	focaltech_touch_3681/focaltech_esdcheck.o \
	focaltech_touch_3681/focaltech_point_report_check.o \
	focaltech_touch_3681/focaltech_flash.o \
	focaltech_touch_3681/focaltech_common_interface.o \
	focaltech_touch_3681/focaltech_i2c.o \
	focaltech_touch_3681/focaltech_test/focaltech_test.o \
	focaltech_touch_3681/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_3681/focaltech_flash/focaltech_upgrade_ft3681.o \
	focaltech_touch_3681/focaltech_test/supported_ic/focaltech_test_ft3681.o \

######################################################
obj-m += zte_tpd.o
