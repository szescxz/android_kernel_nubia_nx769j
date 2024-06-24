# Makefile for the goodix touchscreen drivers.
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/


obj-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C) += goodix_brl_i2c.o
obj-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI) += goodix_brl_spi.o
obj-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_V2) += goodix_ts_core.o \
				goodix_brl_hw.o \
				goodix_cfg_bin.o \
				goodix_ts_utils.o \
				goodix_brl_fwupdate.o \
				goodix_ts_gesture.o \
				goodix_ts_inspect.o \
				goodix_ts_tools.o \
				goodix_common_interface.o

