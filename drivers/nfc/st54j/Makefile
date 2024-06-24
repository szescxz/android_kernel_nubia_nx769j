#
# Makefile for STMicroelectronics NFC transceiver ST54J
#
obj-m += st54jnfc.o
st54jnfc-objs += st21nfc.o

BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/st54jnfc.ko

obj-$(CONFIG_NFC_ST54J_ESE) += st54jese.o
st54jese-objs += st54spi.o

ifeq ($(CONFIG_NFC_ST54J_ESE),m)
$(info "ST54J eSE IS BUILT AS DLKM")
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/st54jese.ko
endif
