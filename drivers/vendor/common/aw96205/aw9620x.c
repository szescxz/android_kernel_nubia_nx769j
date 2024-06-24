/*
* aw9620x.c
*
* Copyright (c) 2022 AWINIC Technology CO., LTD
*
* Author: Bob <renxinghu@awinic.com>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

#include "aw9620x.h"

#define AW9620X_I2C_NAME 		"aw9620x_sar"
#define AW9620X_DRIVER_VERSION 		"v0.0.4.19"

static struct mutex aw_lock;
static struct mutex aw_update_lock;

#define AWINIC_CODE_VERSION "V0.0.7-V1.0.4"	/* "code version"-"excel version" */

#define DEBUG_LOG_LEVEL
#ifdef DEBUG_LOG_LEVEL
#define DBG(fmt, arg...)   do {\
printk("AWINIC_BIN %s,line= %d,"fmt, __func__, __LINE__, ##arg);\
} while (0)
#define DBG_ERR(fmt, arg...)   do {\
printk("AWINIC_BIN_ERR %s,line= %d,"fmt, __func__, __LINE__, ##arg);\
} while (0)
#else
#define DBG(fmt, arg...) do {} while (0)
#define DBG_ERR(fmt, arg...) do {} while (0)
#endif

#define printing_data_code

typedef unsigned short int aw_uint16;
typedef unsigned long int aw_uint32;

volatile static uint8_t aw_init_irq_flag [2] = { AW_FALSE , AW_FALSE } ;
#define BigLittleSwap16(A)	((((aw_uint16)(A) & 0xff00) >> 8) | \
				 (((aw_uint16)(A) & 0x00ff) << 8))

#define BigLittleSwap32(A)	((((aw_uint32)(A) & 0xff000000) >> 24) | \
				(((aw_uint32)(A) & 0x00ff0000) >> 8) | \
				(((aw_uint32)(A) & 0x0000ff00) << 8) | \
				(((aw_uint32)(A) & 0x000000ff) << 24))

static int32_t aw9620x_reg_update_boot_work(struct aw9620x *aw9620x);
static int32_t aw9620x_reg_update_fw_to_flash(struct aw9620x *aw9620x, struct aw_bin *aw_bin);
/**
*
* Interface function
*
* return value:
*       value = 0 :success;
*       value = -1 :check bin header version
*       value = -2 :check bin data type
*       value = -3 :check sum or check bin data len error
*       value = -4 :check data version
*       value = -5 :check register num
*       value = -6 :check dsp reg num
*       value = -7 :check soc app num
*       value = -8 :bin is NULL point
*
**/

/********************************************************
*
* check sum data
*
********************************************************/
int aw_check_sum(struct aw_bin *bin, int bin_num)
{
	unsigned int i = 0;
	unsigned int sum_data = 0;
	unsigned int check_sum = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr -
			      bin->header_info[bin_num].header_len)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	check_sum = GET_32_DATA(*(p_check_sum + 3),
				*(p_check_sum + 2),
				*(p_check_sum + 1), *(p_check_sum));

	for (i = 4;
	     i <
	     bin->header_info[bin_num].bin_data_len +
	     bin->header_info[bin_num].header_len; i++) {
		sum_data += *(p_check_sum + i);
	}
	DBG("aw_bin_parse bin_num=%d, check_sum = 0x%x, sum_data = 0x%x\n",
		bin_num, check_sum, sum_data);
	if (sum_data != check_sum) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse check sum or check bin data len error\n");
		DBG_ERR("aw_bin_parse bin_num=%d, check_sum = 0x%x, sum_data = 0x%x\n", bin_num, check_sum, sum_data);
		return -3;
	}
	p_check_sum = NULL;

	return 0;
}

int aw_check_data_version(struct aw_bin *bin, int bin_num)
{
	int i = 0;
	DBG("enter\n");

	for (i = DATA_VERSION_V1; i < DATA_VERSION_MAX; i++) {
		if (bin->header_info[bin_num].bin_data_ver == i) {
			return 0;
		}
	}
	DBG_ERR("aw_bin_parse Unrecognized this bin data version\n");
	return -4;
}

int aw_check_register_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_register_num = 0;
	unsigned int parse_register_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	parse_register_num = GET_32_DATA(*(p_check_sum + 3),
					 *(p_check_sum + 2),
					 *(p_check_sum + 1), *(p_check_sum));
	check_register_num = (bin->header_info[bin_num].bin_data_len - 4) /
	    (bin->header_info[bin_num].reg_byte_len +
	     bin->header_info[bin_num].data_byte_len);
	DBG
	    ("aw_bin_parse bin_num=%d, parse_register_num = 0x%x, check_register_num = 0x%x\n",
	     bin_num, parse_register_num, check_register_num);
	if (parse_register_num != check_register_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse register num is error\n");
		DBG_ERR("aw_bin_parse bin_num=%d, parse_register_num = 0x%x, check_register_num = 0x%x\n", bin_num, parse_register_num, check_register_num);
		return -5;
	}
	bin->header_info[bin_num].reg_num = parse_register_num;
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 4;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 4;
	return 0;
}

int aw_check_dsp_reg_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_dsp_reg_num = 0;
	unsigned int parse_dsp_reg_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	parse_dsp_reg_num = GET_32_DATA(*(p_check_sum + 7),
					*(p_check_sum + 6),
					*(p_check_sum + 5),
					*(p_check_sum + 4));
	bin->header_info[bin_num].reg_data_byte_len =
	    GET_32_DATA(*(p_check_sum + 11), *(p_check_sum + 10),
			*(p_check_sum + 9), *(p_check_sum + 8));
	check_dsp_reg_num =
	    (bin->header_info[bin_num].bin_data_len -
	     12) / bin->header_info[bin_num].reg_data_byte_len;
	DBG
	    ("aw_bin_parse bin_num=%d, parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x\n",
	     bin_num, parse_dsp_reg_num, check_dsp_reg_num);
	if (parse_dsp_reg_num != check_dsp_reg_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse dsp reg num is error\n");
		DBG_ERR("aw_bin_parse bin_num=%d, parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x\n", bin_num, parse_dsp_reg_num, check_dsp_reg_num);
		return -6;
	}
	bin->header_info[bin_num].download_addr =
	    GET_32_DATA(*(p_check_sum + 3), *(p_check_sum + 2),
			*(p_check_sum + 1), *(p_check_sum));
	bin->header_info[bin_num].reg_num = parse_dsp_reg_num;
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 12;
	return 0;
}

int aw_check_soc_app_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_soc_app_num = 0;
	unsigned int parse_soc_app_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");

	p_check_sum =
	    &(bin->info.data[(bin->header_info[bin_num].valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	bin->header_info[bin_num].app_version = GET_32_DATA(*(p_check_sum + 3),
							    *(p_check_sum + 2),
							    *(p_check_sum + 1),
							    *(p_check_sum));
	parse_soc_app_num = GET_32_DATA(*(p_check_sum + 11),
					*(p_check_sum + 10),
					*(p_check_sum + 9), *(p_check_sum + 8));
	check_soc_app_num = bin->header_info[bin_num].bin_data_len - 12;
	DBG
	    ("aw_bin_parse bin_num=%d, parse_soc_app_num = 0x%x, check_soc_app_num = 0x%x\n",
	     bin_num, parse_soc_app_num, check_soc_app_num);
	if (parse_soc_app_num != check_soc_app_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse soc app num is error\n");
		DBG_ERR("aw_bin_parse bin_num=%d, parse_soc_app_num = 0x%x, check_soc_app_num = 0x%x\n", bin_num, parse_soc_app_num, check_soc_app_num);
		return -7;
	}
	bin->header_info[bin_num].reg_num = parse_soc_app_num;
	bin->header_info[bin_num].download_addr =
	    GET_32_DATA(*(p_check_sum + 7), *(p_check_sum + 6),
			*(p_check_sum + 5), *(p_check_sum + 4));
	bin->header_info[bin_num].valid_data_len =
	    bin->header_info[bin_num].bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr =
	    bin->header_info[bin_num].valid_data_addr + 12;
	return 0;
}

/************************
***
***bin header 1_0_0
***
************************/
void aw_get_single_bin_header_1_0_0(struct aw_bin *bin)
{
	int i;
	DBG("enter %s\n", __func__);
	bin->header_info[bin->all_bin_parse_num].header_len = 60;
	bin->header_info[bin->all_bin_parse_num].check_sum =
	    GET_32_DATA(*(bin->p_addr + 3), *(bin->p_addr + 2),
			*(bin->p_addr + 1), *(bin->p_addr));
	bin->header_info[bin->all_bin_parse_num].header_ver =
	    GET_32_DATA(*(bin->p_addr + 7), *(bin->p_addr + 6),
			*(bin->p_addr + 5), *(bin->p_addr + 4));
	bin->header_info[bin->all_bin_parse_num].bin_data_type =
	    GET_32_DATA(*(bin->p_addr + 11), *(bin->p_addr + 10),
			*(bin->p_addr + 9), *(bin->p_addr + 8));
	bin->header_info[bin->all_bin_parse_num].bin_data_ver =
	    GET_32_DATA(*(bin->p_addr + 15), *(bin->p_addr + 14),
			*(bin->p_addr + 13), *(bin->p_addr + 12));
	bin->header_info[bin->all_bin_parse_num].bin_data_len =
	    GET_32_DATA(*(bin->p_addr + 19), *(bin->p_addr + 18),
			*(bin->p_addr + 17), *(bin->p_addr + 16));
	bin->header_info[bin->all_bin_parse_num].ui_ver =
	    GET_32_DATA(*(bin->p_addr + 23), *(bin->p_addr + 22),
			*(bin->p_addr + 21), *(bin->p_addr + 20));
	bin->header_info[bin->all_bin_parse_num].reg_byte_len =
	    GET_32_DATA(*(bin->p_addr + 35), *(bin->p_addr + 34),
			*(bin->p_addr + 33), *(bin->p_addr + 32));
	bin->header_info[bin->all_bin_parse_num].data_byte_len =
	    GET_32_DATA(*(bin->p_addr + 39), *(bin->p_addr + 38),
			*(bin->p_addr + 37), *(bin->p_addr + 36));
	bin->header_info[bin->all_bin_parse_num].device_addr =
	    GET_32_DATA(*(bin->p_addr + 43), *(bin->p_addr + 42),
			*(bin->p_addr + 41), *(bin->p_addr + 40));
	for (i = 0; i < 8; i++) {
		bin->header_info[bin->all_bin_parse_num].chip_type[i] =
		    *(bin->p_addr + 24 + i);
	}
	bin->header_info[bin->all_bin_parse_num].chip_type[i] = '\0';
	DBG("%s : enter chip_type is %s\n", __func__,
							bin->header_info[bin->all_bin_parse_num].chip_type);

	bin->header_info[bin->all_bin_parse_num].reg_num = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].reg_data_byte_len = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].download_addr = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].app_version = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].valid_data_len = 0x00000000;
	bin->all_bin_parse_num += 1;
}

int aw_parse_each_of_multi_bins_1_0_0(unsigned int bin_num, int bin_serial_num,
				      struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_start_addr = 0;
	unsigned int valid_data_len = 0;
	DBG("aw_bin_parse enter multi bin branch -- %s\n", __func__);
	if (!bin_serial_num) {
		bin_start_addr = GET_32_DATA(*(bin->p_addr + 67),
					     *(bin->p_addr + 66),
					     *(bin->p_addr + 65),
					     *(bin->p_addr + 64));
		bin->p_addr += (60 + bin_start_addr);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    bin->header_info[bin->all_bin_parse_num -
				     1].valid_data_addr + 4 + 8 * bin_num + 60;
	} else {
		valid_data_len =
		    bin->header_info[bin->all_bin_parse_num - 1].bin_data_len;
		bin->p_addr += (60 + valid_data_len);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    bin->header_info[bin->all_bin_parse_num -
				     1].valid_data_addr +
		    bin->header_info[bin->all_bin_parse_num - 1].bin_data_len +
		    60;
	}

	ret = aw_parse_bin_header_1_0_0(bin);
	return ret;
}

/* Get the number of bins in multi bins, and set a for loop, loop processing each bin data */
int aw_get_multi_bin_header_1_0_0(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;
	unsigned int bin_num = 0;
	DBG("aw_bin_parse enter multi bin branch -- %s\n", __func__);
	bin_num = GET_32_DATA(*(bin->p_addr + 63),
			      *(bin->p_addr + 62),
			      *(bin->p_addr + 61), *(bin->p_addr + 60));
	if (bin->multi_bin_parse_num == 1) {
		bin->header_info[bin->all_bin_parse_num].valid_data_addr = 60;
	}
	aw_get_single_bin_header_1_0_0(bin);

	for (i = 0; i < bin_num; i++) {
		DBG("aw_bin_parse enter multi bin for is %d\n", i);
		ret = aw_parse_each_of_multi_bins_1_0_0(bin_num, i, bin);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

/********************************************************
*
* If the bin framework header version is 1.0.0,
  determine the data type of bin, and then perform different processing
  according to the data type
  If it is a single bin data type, write the data directly into the structure array
  If it is a multi-bin data type, first obtain the number of bins,
  and then recursively call the bin frame header processing function
  according to the bin number to process the frame header information of each bin separately
*
********************************************************/
int aw_parse_bin_header_1_0_0(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_data_type;
	DBG("enter %s\n", __func__);
	bin_data_type = GET_32_DATA(*(bin->p_addr + 11),
				    *(bin->p_addr + 10),
				    *(bin->p_addr + 9), *(bin->p_addr + 8));
	DBG("aw_bin_parse bin_data_type 0x%x\n", bin_data_type);
	switch (bin_data_type) {
	case DATA_TYPE_REGISTER:
	case DATA_TYPE_DSP_REG:
	case DATA_TYPE_SOC_APP:
		/* Divided into two processing methods,
		   one is single bin processing,
		   and the other is single bin processing in multi bin */
		DBG("aw_bin_parse enter single bin branch\n");
		bin->single_bin_parse_num += 1;
		DBG("%s bin->single_bin_parse_num is %d\n", __func__,
			bin->single_bin_parse_num);
		if (!bin->multi_bin_parse_num) {
			bin->header_info[bin->
					 all_bin_parse_num].valid_data_addr =
			    60;
		}
		aw_get_single_bin_header_1_0_0(bin);
		break;
	case DATA_TYPE_MULTI_BINS:
		/* Get the number of times to enter multi bins */
		DBG("aw_bin_parse enter multi bin branch\n");
		bin->multi_bin_parse_num += 1;
		DBG("%s bin->multi_bin_parse_num is %d\n", __func__,
			bin->multi_bin_parse_num);
		ret = aw_get_multi_bin_header_1_0_0(bin);
		if (ret < 0) {
			return ret;
		}
		break;
	default:
		DBG_ERR("aw_bin_parse Unrecognized this bin data type\n");
		return -2;
	}
	return 0;
}

/* get the bin's header version */
static int aw_check_bin_header_version(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int header_version = 0;

	header_version = GET_32_DATA(*(bin->p_addr + 7),
				     *(bin->p_addr + 6),
				     *(bin->p_addr + 5), *(bin->p_addr + 4));

	DBG("aw_bin_parse header_version 0x%x\n", header_version);

	/* Write data to the corresponding structure array
	   according to different formats of the bin frame header version */
	switch (header_version) {
	case HEADER_VERSION_1_0_0:
		ret = aw_parse_bin_header_1_0_0(bin);
		return ret;
	default:
		DBG_ERR("aw_bin_parse Unrecognized this bin header version \n");
		return -1;
	}
}

int aw_parsing_bin_file(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;

	DBG("aw_bin_parse code version:%s\n", AWINIC_CODE_VERSION);
	if (!bin) {
		DBG_ERR("aw_bin_parse bin is NULL\n");
		return -8;
	}
	bin->p_addr = bin->info.data;
	bin->all_bin_parse_num = 0;
	bin->multi_bin_parse_num = 0;
	bin->single_bin_parse_num = 0;

	/* filling bins header info */
	ret = aw_check_bin_header_version(bin);
	if (ret < 0) {
		DBG_ERR("aw_bin_parse check bin header version error\n");
		return ret;
	}
	bin->p_addr = NULL;

	/* check bin header info */
	for (i = 0; i < bin->all_bin_parse_num; i++) {
		/* check sum */
		ret = aw_check_sum(bin, i);
		if (ret < 0) {
			DBG_ERR("aw_bin_parse check sum data error\n");
			return ret;
		}
		/* check bin data version */
		ret = aw_check_data_version(bin, i);
		if (ret < 0) {
			DBG_ERR("aw_bin_parse check data version error\n");
			return ret;
		}
		/* check valid data */
		if (bin->header_info[i].bin_data_ver == DATA_VERSION_V1) {
			/* check register num */
			if (bin->header_info[i].bin_data_type ==
			    DATA_TYPE_REGISTER) {
				ret = aw_check_register_num_v1(bin, i);
				if (ret < 0) {
					DBG_ERR
					    ("aw_bin_parse check register num error\n");
					return ret;
				}
				/* check dsp reg num */
			} else if (bin->header_info[i].bin_data_type ==
				   DATA_TYPE_DSP_REG) {
				ret = aw_check_dsp_reg_num_v1(bin, i);
				if (ret < 0) {
					DBG_ERR
					    ("aw_bin_parse check dsp reg num error\n");
					return ret;
				}
				/* check soc app num */
			} else if (bin->header_info[i].bin_data_type ==
				   DATA_TYPE_SOC_APP) {
				ret = aw_check_soc_app_num_v1(bin, i);
				if (ret < 0) {
					DBG_ERR
					    ("aw_bin_parse check soc app num error\n");
					return ret;
				}
			} else {
				bin->header_info[i].valid_data_len =
				    bin->header_info[i].bin_data_len;
			}
		}
	}
	DBG("aw_bin_parse parsing success\n");

	return 0;
}




static int32_t i2c_write(struct i2c_client *i2c, uint8_t *tr_data, uint16_t len)
{
	struct i2c_msg msg;

	msg.addr = i2c->addr;
	msg.flags = I2C_WR;
	msg.len = len;
	msg.buf = tr_data;

	return i2c_transfer(i2c->adapter, &msg, 1);
}

static int32_t i2c_read(struct i2c_client *i2c, uint8_t *addr,
				uint8_t addr_len, uint8_t *data, uint16_t data_len)
{
	struct i2c_msg msg[2];

	msg[0].addr = i2c->addr;
	msg[0].flags = I2C_WR;
	msg[0].len = addr_len;
	msg[0].buf = addr;

	msg[1].addr = i2c->addr;
	msg[1].flags = I2C_RD;
	msg[1].len = data_len;
	msg[1].buf = data;

	return i2c_transfer(i2c->adapter, msg, 2);
}

static int32_t aw9620x_i2c_read(struct aw9620x *aw9620x,
				uint16_t reg_addr16,  uint32_t *reg_data32)
{	int8_t cnt = AW_RETRIES;
	int32_t ret = -1;
	uint8_t r_buf[6] = { 0 };

	r_buf[0] = (unsigned char)(reg_addr16 >> OFFSET_BIT_8);
	r_buf[1] = (unsigned char)(reg_addr16);

	do {
		ret = i2c_read(aw9620x->i2c, r_buf, LEN_BYTE_2, &r_buf[2], LEN_BYTE_4);
		if (ret < 0)
			AWLOGE(aw9620x->dev,
				"i2c read error reg: 0x%04x, ret= %d cnt= %d",
				reg_addr16, ret, cnt);
		else
			break;
		usleep_range(2000, 3000);
	} while (cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "i2c read error!");
		return -AW_ERR;
	}

	*reg_data32 = ((uint32_t)r_buf[5] << OFFSET_BIT_0) | ((uint32_t)r_buf[4] << OFFSET_BIT_8) |
		      ((uint32_t)r_buf[3] << OFFSET_BIT_16) | ((uint32_t)r_buf[2] << OFFSET_BIT_24);

	return AW_OK;
}

static int32_t aw9620x_i2c_write(struct aw9620x *aw9620x,
				uint16_t reg_addr16, uint32_t reg_data32)
{
	int8_t cnt = AW_RETRIES;
	int32_t ret = -1;
	uint8_t w_buf[6] = { 0 };

	/*reg_addr*/
	w_buf[0] = (uint8_t)(reg_addr16 >> OFFSET_BIT_8);
	w_buf[1] = (uint8_t)(reg_addr16);
	/*data*/
	w_buf[2] = (uint8_t)(reg_data32 >> OFFSET_BIT_24);
	w_buf[3] = (uint8_t)(reg_data32 >> OFFSET_BIT_16);
	w_buf[4] = (uint8_t)(reg_data32 >> OFFSET_BIT_8);
	w_buf[5] = (uint8_t)(reg_data32);

	do {
		ret = i2c_write(aw9620x->i2c, w_buf, ARRAY_SIZE(w_buf));
		if (ret < 0) {
			AWLOGE(aw9620x->dev,
				"aw9620x i2c write error reg: 0x%04x data: 0x%08x, ret= %d cnt= %d",
				reg_addr16, reg_data32, ret, cnt);
		} else {
			break;
		}
		usleep_range(2000, 3000);
	} while (cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "i2c write error!");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_i2c_write_bits(struct aw9620x *aw9620x, uint16_t reg_addr16,
				uint32_t mask, uint32_t reg_data32)
{
	uint32_t reg_val;

	aw9620x_i2c_read(aw9620x, reg_addr16, &reg_val);
	reg_val &= mask;
	reg_val |= reg_data32;
	aw9620x_i2c_write(aw9620x, reg_addr16, reg_val);

	return 0;
}


static int32_t i2c_write_seq(struct aw9620x *aw9620x)
{
	int8_t cnt = AW_RETRIES;
	int32_t ret =  -AW_ERR;

	do {
		ret = i2c_write(aw9620x->i2c,
				aw9620x->awrw_info.p_i2c_tranfar_data,
				aw9620x->awrw_info.i2c_tranfar_data_len);
		if (ret < 0)
			AWLOGE(aw9620x->dev, "aw9620x i2c write seq error %d", ret);
		else
			break;
		usleep_range(2000, 3000);
	} while (cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "i2c write error!");
		return -AW_ERR;
	}

	return ret;
}

static int32_t i2c_read_seq(struct aw9620x *aw9620x)
{
	int8_t cnt = AW_RETRIES;
	int32_t ret = -AW_ERR;

	do {
		ret = i2c_read(aw9620x->i2c,
			 aw9620x->awrw_info.p_i2c_tranfar_data,
			 aw9620x->awrw_info.addr_len,
			 aw9620x->awrw_info.p_i2c_tranfar_data + aw9620x->awrw_info.addr_len,
			 (uint16_t)(aw9620x->awrw_info.data_len * aw9620x->awrw_info.reg_num));
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "aw9620x i2c write error %d", ret);
		} else {
			break;
		}
		usleep_range(2000, 3000);
	} while (cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "i2c write error!");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_parse_dt(struct device *dev, struct aw9620x *aw9620x,
						struct device_node *np)
{
	int32_t val = 0;

	val = of_property_read_u32(np, "sar-num", &aw9620x->sar_num);
	if (val != 0) {
		AWLOGE(aw9620x->dev, "multiple sar failed!");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "sar num = %d", aw9620x->sar_num);
	}

	aw9620x->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (aw9620x->irq_gpio < 0) {
		aw9620x->irq_gpio = -1;
		AWLOGE(aw9620x->dev, "no irq gpio provided.");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "irq gpio provided ok.");
	}

	val = of_property_read_u32(np, "channel_use_flag", &aw9620x->channel_use_flag);
	if (val != 0) {
		AWLOGE(aw9620x->dev, "channel_use_flag failed!");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "channel_use_flag = 0x%x", aw9620x->channel_use_flag);
	}

	aw9620x->arr_update_flag = of_property_read_bool(np, "aw9620x,using-arr-laod-reg");
	AWLOGI(aw9620x->dev, "firmware_flag = <%d>", aw9620x->arr_update_flag);

	return AW_OK;
}

#ifdef AW_POWER_ON
static int32_t aw9620x_power_init(struct aw9620x *aw9620x)
{
	int32_t rc = 0;
	uint8_t vcc_name[20] = { 0 };

	AWLOGD(aw9620x->dev, "aw9620x power init enter");

	snprintf(vcc_name, sizeof(vcc_name), "vcc%d", aw9620x->sar_num);
	AWLOGD(aw9620x->dev, "vcc_name = %s", vcc_name);

	aw9620x->vcc = regulator_get(aw9620x->dev, vcc_name);
	if (IS_ERR(aw9620x->vcc)) {
		rc = PTR_ERR(aw9620x->vcc);
		AWLOGE(aw9620x->dev, "regulator get failed vcc rc = %d", rc);
		return rc;
	}

	if (regulator_count_voltages(aw9620x->vcc) > 0) {
		rc = regulator_set_voltage(aw9620x->vcc,
					AW_VCC_MIN_UV, AW_VCC_MAX_UV);
		if (rc) {
			AWLOGE(aw9620x->dev,
				"regulator set vol failed rc = %d", rc);
			goto reg_vcc_put;
		}
	}

	return rc;

reg_vcc_put:
	regulator_put(aw9620x->vcc);
	return rc;
}

static void aw9620x_power_enable(struct aw9620x *aw9620x, bool on)
{
	int32_t rc = 0;

	AWLOGD(aw9620x->dev, "aw9620x power enable enter");

	if (on) {
		rc = regulator_enable(aw9620x->vcc);
		if (rc)
			AWLOGE(aw9620x->dev,
				"regulator_enable vol failed rc = %d", rc);
		else
			aw9620x->power_enable = true;

	} else {
		rc = regulator_disable(aw9620x->vcc);
		if (rc)
			AWLOGE(aw9620x->dev,
				"regulator_disable vol failed rc = %d", rc);
		else
			aw9620x->power_enable = false;
	}
}

static int32_t regulator_is_get_voltage(struct aw9620x *aw9620x)
{
	int32_t cnt = 10;
	int32_t voltage_val = 0;

	AWLOGD(aw9620x->dev, "enter");

	while(cnt--) {
		voltage_val = regulator_get_voltage(aw9620x->vcc);
		AWLOGD(aw9620x->dev, "aw9620x voltage is : %d uv", voltage_val);
		if (voltage_val >= AW9620X_CHIP_MIN_VOLTAGE)
			return AW_OK;
		mdelay(1);
	}

	return -AW_ERR;
}
#endif

static int32_t aw9620x_read_chipid(struct aw9620x *aw9620x)
{
	int32_t ret = -AW_ERR;
	uint32_t reg_val = 0;

	AWLOGD(aw9620x->dev, "enter");

	ret = aw9620x_i2c_read(aw9620x, REG_CHIP_ID, &reg_val);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "read CHIP ID failed: %d", ret);
		return -AW_ERR;
	}

	switch (reg_val)
	{
		case AW96203CSR_CHIP_ID:
			memcpy(aw9620x->chip_name, "AW96203", strlen("AW96203") + 1);
			AWLOGD(aw9620x->dev, "AW96203CSR CHIP ID : 0x%x", reg_val);
		break;
		case AW96205DNR_CHIP_ID:
			memcpy(aw9620x->chip_name, "AW96205", strlen("AW96205") + 1);
			AWLOGD(aw9620x->dev, "AW96205DNR CHIP ID : 0x%x", reg_val);
		break;
		case AW96208CSR_CHIP_ID:
			memcpy(aw9620x->chip_name, "AW96208", strlen("AW96208") + 1);
			AWLOGD(aw9620x->dev, "AW96208CSR CHIP ID : 0x%x", reg_val);
		break;
		default:
			AWLOGD(aw9620x->dev,
				"no chipid,need update root and frimware,CHIP ID : 0x%x", reg_val);
			return -AW_ERR_CHIPID;
		break;
	}

	return AW_OK;
}

static int32_t aw9620x_soft_reset(struct aw9620x *aw9620x)
{
	int32_t ret = -AW_ERR;

	AWLOGD(aw9620x->dev, "enter");

	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_APB_ACCESS_EN);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "read REG_APB_ACCESS_EN err: %d", ret);
		return -AW_ERR;
	}

	ret = aw9620x_i2c_write(aw9620x, AW_REG_FLASH_WAKE_UP, 0x110);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "read REG_APB_ACCESS_EN err: %d", ret);
		return -AW_ERR;
	}

	msleep(1);

	aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_MCFG00_EN);


	ret = aw9620x_i2c_write(aw9620x, REG_RSTNALL, REG_RSTNALL_VAL);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "read soft_reset err: %d", ret);
		return -AW_ERR;
	}

	msleep(AW_POWER_ON_DELAY_MS);

	ret = aw9620x_i2c_write(aw9620x, AW_REG_FLASH_WAKE_UP, 0x102);
	if (ret != AW_OK) {
		if (aw9620x->i2c->addr == 0x12) {
			aw9620x->i2c->addr = 0x13;
		}
		ret = aw9620x_i2c_write(aw9620x, AW_REG_FLASH_WAKE_UP, 0x102);
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "read REG_APB_ACCESS_EN err1: %d", ret);
			return -AW_ERR;
		} else {
			AWLOGE(aw9620x->dev, "AW_REG_FLASH_WAKE_UP ok");
			AWLOGE(aw9620x->dev, "i2c->addr = 0x%x", aw9620x->i2c->addr);
		}
	}
	return AW_OK;
}

static int32_t aw9620x_read_init_over_irq(struct aw9620x *aw9620x)
{
	int32_t ret = -AW_ERR;
	uint32_t irq_stat = 0;
	int8_t cnt = AW_IRQ_DELAY_MS;

	AWLOGD(aw9620x->dev, "enter");

	do {
		ret = aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &irq_stat);
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "read init irq reg err: %d cnt: %d", ret, cnt);
			return -AW_ERR;
		}
		if ((irq_stat & INIT_OVER_IRQ) == INIT_OVER_IRQ_OK) {
			AWLOGD(aw9620x->dev, "init over irq ok cnt: %d", cnt);
			return AW_OK;
		} else {
			AWLOGE(aw9620x->dev, "init over irq no ok cnt: %d", cnt);
		}
		msleep(1);
	} while (cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "init over irq err!");
	}

	return -AW_ERR_IRQ_INIT;
}

/********aw9620x_reg_mode_update start********/
static int32_t aw9620x_check_isp_go_reg(struct aw9620x *aw9620x)
{
	int32_t delay_cnt = 100;
	uint32_t r_isp_go_reg = 0;
	int32_t ret = 0;

	do {
		ret = aw9620x_i2c_read(aw9620x, REG_ISP_GO, &r_isp_go_reg);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0xff20 err");
			return ret;
		}
		if (r_isp_go_reg == 0) {
			break;
		}
		mdelay(1);
	} while (delay_cnt--);

	if (delay_cnt < 0) {
		AWLOGE(aw9620x->dev, "check_isp_go_reg err!");
		return -AW_ERR;
	}

	return AW_OK;
}


static int32_t aw9620x_close_write_flash_protect(struct aw9620x *aw9620x, uint32_t flash_addr_flag)
{
	int32_t ret = 0;

	//Open host read / write FMC protection
	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_APB_ACCESS_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	//Configure PMC_ CFG register
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_SET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	//Turn on flash write protection
	if (flash_addr_flag == BOOT_UPDATE) {
		ret = aw9620x_i2c_write(aw9620x, REG_BTROM_EW_EN, REG_SET_BTROM_EW_EN);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0xff20 err");
			return -AW_ERR;
		}
	}

	return AW_OK;
}

static int32_t aw9620x_reg_write_to_flash_once(struct aw9620x *aw9620x, uint16_t addr,
						uint32_t w_data)
{
	int32_t ret = 0;

	//Write access address
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_ADDR, addr);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	//Write data
	ret = aw9620x_i2c_write(aw9620x, AW_FW_W_DATA_ADDR, w_data);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	//Configure ISP_CMD reg
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_CMD, REG_ISP_CMD_CONFIG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	//Configure ISP_GO reg
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_GO, REG_SET_ISP_GO);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}

	ret = aw9620x_check_isp_go_reg(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "check_isp_go_reg err");
		return -AW_ERR;
	}

	return AW_OK;
}

static uint32_t aw9620x_get_bin_checksum(uint8_t *w_bin_offset,
					uint32_t update_data_len, uint32_t check_len)
{
	uint32_t i = 0;
	uint32_t check_sum = 0;
	uint32_t tmp = 0;
	uint32_t index = 0;

	for (i = 0; i < check_len; i += WORD_LEN) {
		if (i < update_data_len) {
			tmp = AW_GET_32_DATA(w_bin_offset[index + 3],
				w_bin_offset[index + 2],
				w_bin_offset[index + 1],
				w_bin_offset[index + 0]);
			index  += WORD_LEN;
		} else {
			tmp = 0xffffffff;
		}
		check_sum += tmp;
	}
	check_sum = ~check_sum + 1;

	return check_sum;
}

static int32_t aw9620x_reg_write_val_to_flash(struct aw9620x *aw9620x,
						uint16_t addr,
						uint32_t val,
						struct aw_update_common *update_info)
{
	int32_t ret = 0;

	AWLOGD(aw9620x->dev, "enter");

	ret = aw9620x_close_write_flash_protect(aw9620x, update_info->update_flag);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "close_write_flash_protect err");
		return -AW_ERR;
	}

	ret = aw9620x_reg_write_to_flash_once(aw9620x, addr, val);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write aw9620x_reg_write_bin_once err");
		return -AW_ERR;
	}

	//Configure PMU_ CFG register
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_ENSET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0x4820 err");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_reg_read_val(struct aw9620x *aw9620x, uint32_t *read_data, uint16_t start_addr)
{
	int32_t ret = 0;

	//3.config PMU_CFG reg
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_ADDR, start_addr);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config PMU_CFG regerr");
		return -AW_ERR;
	}
	//4.config ISP_CMD reg
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_CMD, REG_ISP_CMD_MAIN_ARR);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config ISP_CMD reg err");
		return -AW_ERR;
	}
	//5.config ISP_GO reg
	ret = aw9620x_i2c_write(aw9620x, REG_ISP_GO, REG_SET_ISP_GO);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config ISP_CMD reg err");
		return -AW_ERR;
	}
	//6.check isp_go reg
	ret = aw9620x_check_isp_go_reg(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config check_isp_go_reg err");
		return -AW_ERR;
	}
	//7 read data
	ret = aw9620x_i2c_read(aw9620x, REG_ISP_RDDATA, read_data);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config ISP_CMD reg err");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_rd_or_wi_cmp(struct aw9620x *aw9620x,
				struct aw_update_common *update_info,
				uint8_t *w_bin_offset, uint32_t update_data_len)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint32_t r_data = 0;
	uint32_t w_data = 0;
	uint32_t read_cnt = update_info->update_data_len;

	AWLOGD(aw9620x->dev, "enter");

	//1.config FMC reg
	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_APB_ACCESS_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}
	//2.config PMU_CFG reg
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_ENSET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config PMU_CFG reg err");
		return -AW_ERR;
	}

	AWLOGE(aw9620x->dev, "read_cnt = %d", read_cnt);
	for (i = 0; i < read_cnt; i += WORD_LEN) {
		ret = aw9620x_reg_read_val(aw9620x, &r_data,
					update_info->flash_tr_start_addr + i);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg_read_bin err");
			return -AW_ERR;
		}
		w_data = AW_GET_32_DATA(w_bin_offset[i + 3],
					w_bin_offset[i + 2],
					w_bin_offset[i + 1],
					w_bin_offset[i + 0]);
/*		AWLOGD(aw9620x->dev, "i= %d, addr= 0x%08x, W_DATA= 0x%08x, R_DATA= 0x%08x",
			i, update_info->flash_tr_start_addr + i, w_data, r_data);*/

		if (w_data != r_data) {
			AWLOGE(aw9620x->dev, "w_data != r_data err!");
			return -AW_ERR;
		}
	}
	AWLOGD(aw9620x->dev, "END");

	return AW_OK;
}

static int32_t aw9620x_reg_write_bin_to_flash(struct aw9620x *aw9620x, struct aw_update_common *update_info)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint32_t w_data = 0;
	uint32_t index = 0;
	uint8_t *p_data = update_info->w_bin_offset;
	uint32_t len = update_info->update_data_len;
	uint16_t flash_addr = update_info->flash_tr_start_addr;

	AWLOGD(aw9620x->dev, "enter");

	ret = aw9620x_close_write_flash_protect(aw9620x, update_info->update_flag);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "close_write_flash_protect err");
		return -AW_ERR;

	}
	for (i = 0; i < len; i += WORD_LEN, index += WORD_LEN) {
		w_data = AW_GET_32_DATA(p_data[index + 3], p_data[index + 2],
				p_data[index + 1], p_data[index + 0]);
		// AWLOGD(aw9620x->dev, "w_data :0x%08x", w_data);
		ret = aw9620x_reg_write_to_flash_once(aw9620x, flash_addr + i, w_data);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "write aw9620x_reg_write_bin_once err");
			return -AW_ERR;
		}
	}

	//Configure PMU_ CFG register
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_ENSET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0x4820 err");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_erase_sector(struct aw9620x *aw9620x, struct aw_update_common *update_info)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint32_t erase_len = update_info->check_info->check_len;
	uint32_t temp = (erase_len % SOCTOR_SIZE > 0) ? 1 : 0;
	uint32_t erase_cnt = erase_len / SOCTOR_SIZE + temp;

	AWLOGD(aw9620x->dev, "enter temp = %d erase_cnt = %d", temp, erase_cnt);

	//1.close write protect
	AWLOGD(aw9620x->dev, "1.open host write FMC protect");
	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_APB_ACCESS_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return ret;
	}

	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_SET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return ret;
	}

	if (update_info->update_flag == BOOT_UPDATE) {
		ret = aw9620x_i2c_write(aw9620x, REG_BTROM_EW_EN, REG_SET_BTROM_EW_EN);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "write 0x4794 err");
			return ret;
		}
	}

	for (i = 0; i < erase_cnt; i++)	{
		//2.0x3800~0x3FFF Erase one sector at a time
		ret = aw9620x_i2c_write(aw9620x, REG_ISP_ADDR,
				update_info->check_info->flash_check_start_addr + i * SOCTOR_SIZE);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0x4794 err");
			return ret;
		}

		ret = aw9620x_i2c_write(aw9620x, REG_ISP_CMD, REG_ACCESS_MAIN_ARR);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0x4710 err");
			return ret;
		}

		ret = aw9620x_i2c_write(aw9620x, REG_T_RCV, REG_SET_T_RCV);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0x472c err");
			return ret;
		}

		ret = aw9620x_i2c_write(aw9620x, REG_ISP_GO, REG_SET_ISP_GO);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0x4714 err");
			return ret;
		}

		ret = aw9620x_check_isp_go_reg(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "check_isp_go_reg err");
			return ret;
		}
	}
	ret = aw9620x_i2c_write(aw9620x, REG_T_RCV, REG_SET_T_RCV_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0x472c err");
		return ret;
	}

	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_ENSET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0x4820 err");
		return ret;
	}

	return AW_OK;
}

int32_t aw9620x_flash_update(struct aw9620x *aw9620x, struct aw_update_common *update_info)
{
	int32_t ret = 0;
	uint32_t check_sum = 0;

	AWLOGD(aw9620x->dev, "enter read_len = %d", update_info->update_data_len);

	//1.open register access enable
	AWLOGE(aw9620x->dev, "1.opne register access enable");
	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_REG_ACCESS_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return ret;
	}
	ret = aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_OPEN_MCFG_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "0x4444 write 0x10000 err");
		return ret;
	}
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_SET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "0x4820 write 0x6 err");
		return ret;
	}
	if (update_info->update_flag == FRIMWARE_UPDATE) {
		ret = aw9620x_i2c_write(aw9620x, REG_BTROM_EW_EN, REG_SET_BTROM_EW_EN);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "write 0x4794 err");
			return ret;
		}
	}
	//2.Erase sector (0x3800~0x37ff)
	AWLOGD(aw9620x->dev, "2.Erase sector (0x3800~0x37ff)");
	ret = aw9620x_erase_sector(aw9620x, update_info);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "erase_sector_main err");
		return ret;
	}

	//3.write boot
	AWLOGD(aw9620x->dev, "3.write boot");
	ret = aw9620x_reg_write_bin_to_flash(aw9620x, update_info);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "erase_sector_main err");
		return ret;
	}

	//4.read data check
	AWLOGD(aw9620x->dev, "4.read data check");
	ret = aw9620x_rd_or_wi_cmp(aw9620x,
				   update_info,
				   update_info->w_bin_offset,
				   update_info->update_data_len);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "reg_read_bin err");
		return -AW_ERR;
	}

	//5.write checksum
	AWLOGD(aw9620x->dev, "5.write checksum");
	ret = aw9620x_reg_write_val_to_flash(aw9620x, update_info->check_info->w_check_en_addr,
					AW_CHECK_EN_VAL, update_info);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write checksum en err");
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "write checksum en ok");
	}

	check_sum = aw9620x_get_bin_checksum(update_info->w_bin_offset,
						update_info->update_data_len,
						update_info->check_info->check_len);
	AWLOGD(aw9620x->dev, "check_sum = 0x%x", check_sum);
	ret = aw9620x_reg_write_val_to_flash(aw9620x, update_info->check_info->w_check_code_addr,
					check_sum, update_info);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "5.write checksum err");
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "5.write checksum ok");
	}

	//6.open flash protect
	AWLOGD(aw9620x->dev, "6.open flash protect");
	if (update_info->update_flag == FRIMWARE_UPDATE) {
		ret = aw9620x_i2c_write(aw9620x, REG_BTROM_EW_EN, REG_ENSET_BTROM_EW_EN);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "6.open flash protect err");
			return ret;
		}
	}

/*	ret = aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_SET_MCFG00);
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "6.open flash protect err");
		return ret;
	}*/

	AWLOGD(aw9620x->dev, "%s update success!!!",
		((update_info->update_flag == BOOT_UPDATE)? "bt":"fw"));

	return AW_OK;
}

int32_t aw9620x_update_bt_to_flash(struct aw9620x * aw9620x, uint8_t *p_w_bin_offset, uint32_t data_len)
{
	struct check_info bt_check_info = {
		.check_len = AW_BT_CHECK_LEN,
		.flash_check_start_addr = AW_BT_CHECK_START_ADDR,
		.w_check_en_addr = AW_BT_CHECK_EN_ADDR,
		.w_check_code_addr = AW_BT_CHECK_CODE_ADDR,
	};

	struct aw_update_common bt_update = {
		.update_flag = BOOT_UPDATE,
		.w_bin_offset = p_w_bin_offset,
		.update_data_len = data_len,
		.flash_tr_start_addr = AW_BT_TR_START_ADDR,
		.check_info = &bt_check_info,
	};

	if (p_w_bin_offset == NULL) {
		AWLOGD(aw9620x->dev, "w_bin_offset is null err");
		return -AW_ERR;
	}

	AWLOGD(aw9620x->dev, "update_data_len = %d", bt_update.update_data_len);

	return aw9620x_flash_update(aw9620x, &bt_update);
}

int32_t aw9620x_update_fw_to_flash(struct aw9620x *aw9620x, uint8_t *w_bin_offset, uint32_t update_data_len)
{
	struct check_info fw_check_info = {
		.check_len = AW_FW_CHECK_LEN,
		.flash_check_start_addr = AW_FW_CHECK_START_ADDR,
		.w_check_en_addr = AW_FW_CHECK_EN_ADDR,
		.w_check_code_addr = AW_FW_CHECK_CODE_ADDR,
	};

	struct aw_update_common fw_update = {
		.update_flag = FRIMWARE_UPDATE,
		.w_bin_offset = w_bin_offset,
		.update_data_len = update_data_len,
		.flash_tr_start_addr = AW_FW_TR_START_ADDR,
		.check_info = &fw_check_info,
	};
	AWLOGD(aw9620x->dev, "update_data_len = %d", fw_update.update_data_len);

	return aw9620x_flash_update(aw9620x, &fw_update);
}

/********aw9620x_reg_mode_update end********/

/*******************aw_protocol_transfer start************************/
uint32_t get_pack_checksum(struct aw9620x *aw9620x,
			   uint8_t *data, uint16_t length,
			   uint8_t module, uint8_t command)
{
	uint32_t i = 0;
	uint32_t check_sum = 0;

	AWLOGD(aw9620x->dev, "enter");

	check_sum = module + command + length;
	for (i = 0; i < length; i += 4) {
		check_sum += AW_GET_32_DATA(data[i + 0], data[i + 1],
					data[i + 2], data[i + 3]);
		//AWLOGD(aw9620x->dev, "aw9620x check_sum = 0x%x", check_sum);
	}

	AWLOGD(aw9620x->dev, "aw9610x check_sum = 0x%x", check_sum);

	return (~check_sum + 1);
}

int32_t dri_to_soc_pack_send(struct aw9620x *aw9620x,
			     uint8_t module, uint8_t command,
			     uint16_t length, uint8_t *data)
{
	int8_t cnt = AW_RETRIES;
	//uint32_t i = 0;
	int32_t ret = -1;
	uint32_t checksum = 0;
	uint8_t *prot_pack_w = (uint8_t *)kzalloc(AW_PACK_FIXED_SIZE + length + SEND_ADDR_LEN, GFP_KERNEL);
	if (prot_pack_w == NULL) {
		AWLOGE(aw9620x->dev, "kzalloc_ err!");
		return -1;
	}

	AWLOGD(aw9620x->dev, "enter");

	prot_pack_w[0] = ((uint16_t)PROT_SEND_ADDR & GET_BITS_15_8) >> OFFSET_BIT_8;
	prot_pack_w[1] = (uint16_t)PROT_SEND_ADDR & GET_BITS_7_0;

	//header
	prot_pack_w[2] = ((uint16_t)AW_HEADER_VAL & GET_BITS_15_8) >> OFFSET_BIT_8;
	prot_pack_w[3] = (uint16_t)AW_HEADER_VAL & GET_BITS_7_0;

	//size
	prot_pack_w[4] = ((uint16_t)(AW_PACK_FIXED_SIZE + length) & GET_BITS_15_8) >> OFFSET_BIT_8;
	prot_pack_w[5] = (uint16_t)(AW_PACK_FIXED_SIZE + length) & GET_BITS_7_0;

	//checksum
	checksum = get_pack_checksum(aw9620x, data, length, module, command);
	prot_pack_w[6] = ((uint32_t)checksum & GET_BITS_31_25) >> OFFSET_BIT_24;
	prot_pack_w[7] = ((uint32_t)checksum & GET_BITS_24_16) >> OFFSET_BIT_16;
	prot_pack_w[8] = ((uint32_t)checksum & GET_BITS_15_8) >> OFFSET_BIT_8;
	prot_pack_w[9] = (uint32_t)checksum & GET_BITS_7_0;

	//module
	prot_pack_w[10] = module;

	//command
	prot_pack_w[11] = command;

	//length
	prot_pack_w[12] = ((uint16_t)length & 0xff00) >> OFFSET_BIT_8;
	prot_pack_w[13] = (uint16_t)length & 0x00ff;

	//value
	AWLOGD(aw9620x->dev, "length = %d", length);

	if (length != 0 && data != NULL)
		memcpy(prot_pack_w + AW_PACK_FIXED_SIZE + SEND_ADDR_LEN, data, length);
	else
		AWLOGE(aw9620x->dev, "length == 0 or data == NULL");
	/*
	AWLOGD(aw9620x->dev, "aw9620x addr: 0x%02x 0x%02x", prot_pack_w[0], prot_pack_w[1]);
	for (i = 2; i < AW_PACK_FIXED_SIZE + length + AW_ADDR_SIZE; i += 4) {
		AWLOGD(aw9620x->dev, "aw9620x prot_pack_w i = %d, 0x%02x 0x%02x 0x%02x 0x%02x",
			i, prot_pack_w[i + 0], prot_pack_w[i + 1],
			prot_pack_w[i + 2], prot_pack_w[i + 3]);
	}
	*/
	do {
		ret = i2c_write(aw9620x->i2c, prot_pack_w,
				AW_PACK_FIXED_SIZE + AW_ADDR_SIZE + length);
		if (ret < 0) {
				AWLOGE(aw9620x->dev, "aw9620x i2c write cmd err cnt = %d ret = %d", cnt, ret);
		} else {
			AWLOGD(aw9620x->dev, "aw9620x i2c write cmd ok");
			break;
		}
		//usleep_range(2000, 3000);
	} while(cnt--);

	kfree(prot_pack_w);

	if (cnt < 0) {
		AWLOGD(aw9620x->dev, "i2c write cmd err!!! ret = %d", ret);
		return -AW_ERR;
	}

	return AW_OK;
}

int32_t soc_to_dri_pack_recv(struct aw9620x *aw9620x,
				struct aw_soc_protocol *prot_pack,
				uint32_t pack_len, uint8_t *addr)
{
	int8_t cnt = AW_RETRIES;
	int32_t ret = -1;

	AWLOGD(aw9620x->dev, "enter");

	if (prot_pack == NULL || pack_len == 0) {
		return -1;
	}

	do {
		ret = i2c_read(aw9620x->i2c, addr, 2, (uint8_t *)prot_pack, pack_len);
		if (ret < 0) {
			AWLOGE(aw9620x->dev,
			"aw9620x i2c read cmd err cnt = %d ret = %d", cnt, ret);
		} else {
			break;
		}
		//usleep_range(2000, 3000);
	} while(cnt--);

	if (cnt < 0) {
		AWLOGE(aw9620x->dev, "aw9620x i2c read cmd err!!! ret = %d", ret);
		return -AW_ERR;
	}

	return AW_OK;
}

/**
  * @brief flash init
  * @param parse pack value
  * @retval err code
  */
int32_t soc_to_dri_pack_parse(struct aw9620x *aw9620x, uint32_t length,
					uint8_t module, uint8_t command)
{
	int32_t ret = -1;
	uint32_t pack_len = AW_PACK_FIXED_SIZE + length;
	uint8_t ack_addr[2] = { 0 };
	uint32_t cmd_status = 0;

	struct aw_soc_protocol *prot_pack_r = (struct aw_soc_protocol *)kzalloc(pack_len, GFP_KERNEL);
	if (prot_pack_r == NULL) {
		AWLOGE(aw9620x->dev, "kzalloc_ err!");
		return -1;
	}

	AWLOGD(aw9620x->dev, "enter");

	ack_addr[0] = (uint8_t)(AW_ACK_ADDR >> OFFSET_BIT_8);
	ack_addr[1] = (uint8_t)AW_ACK_ADDR;



	ret = soc_to_dri_pack_recv(aw9620x, prot_pack_r, pack_len, ack_addr);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "pack parse err");
		goto err_pack_parse;
	}

	prot_pack_r->header = AW_REVERSEBYTERS_U16(prot_pack_r->header);
	prot_pack_r->size = AW_REVERSEBYTERS_U16(prot_pack_r->size);
	prot_pack_r->length = AW_REVERSEBYTERS_U16(prot_pack_r->length);
	prot_pack_r->checksum = AW_REVERSEBYTERS_U32(prot_pack_r->checksum);

	cmd_status = AW_GET_32_DATA(prot_pack_r->value[3], prot_pack_r->value[2],
				    prot_pack_r->value[1], prot_pack_r->value[0]);

	AWLOGI(aw9620x->dev, "header= 0x%x, size= 0x%x, length= 0x%x, checksum= 0x%x,"
				"module= 0x%x, command= 0x%x, length=0x%x, cmd_status = %d",
			prot_pack_r->header, prot_pack_r->size,
			prot_pack_r->length, prot_pack_r->checksum,
			prot_pack_r->module, prot_pack_r->command,
			prot_pack_r->length, cmd_status);

	if ((module == prot_pack_r->module) && (command == prot_pack_r->command) && (cmd_status == 0)) {
		AWLOGI(aw9620x->dev, "soc_to_dri_pack_parse ok");
	} else {
		AWLOGE(aw9620x->dev, "soc_to_dri_pack_parse err!!!");
		return -AW_ERR;
	}

	kfree(prot_pack_r);

	return AW_OK;

err_pack_parse:
	kfree(prot_pack_r);
	return -AW_ERR;
}

static void aw9620x_swap(uint8_t *a, uint8_t *b)
{
	uint8_t tmp = 0;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static void aw9620x_bin_conver(uint8_t *data, uint32_t len)
{
	uint32_t i = 0;

	for (i = 0; i < len; i += WORD_LEN) {
		aw9620x_swap(&(data[i + 0]), &(data[i + 3]));
		aw9620x_swap(&(data[i + 1]), &(data[i + 2]));
	}
}

static int32_t aw9620x_fw_version_cmp(struct aw9620x *aw9620x, int8_t *cmp_val, uint32_t app_version)
{
	uint32_t firmware_version = 0;
	int32_t ret = -AW_ERR;

	AWLOGD(aw9620x->dev, "enter");

	ret = aw9620x_i2c_read(aw9620x, REG_FWVER, &firmware_version);
	if (ret < 0) {
		AWLOGD(aw9620x->dev, "read firmware version err");
		return -AW_ERR;
	}
	AWLOGD(aw9620x->dev, "REG_FWVER :0x%08x bin_fwver :0x%08x!",
			firmware_version, app_version);

	if (app_version != firmware_version) {
		*cmp_val = BIN_VER_GREATER_THAN_SOC_VER;
	} else {
		*cmp_val = BIN_VER_NO_GREATER_THAN_SOC_VER;
	}

	return AW_OK;
}

static int32_t aw9620x_read_ack_irq(struct aw9620x *aw9620x)
{
	uint32_t irq_stat = 0;
	int32_t cnt = AW_WAIT_IRQ_CYCLES;
	int32_t ret = 0;

	if (aw9620x->prot_update_fw_flag == SEND_UPDATE_FW_CMD) {
		cnt = AW_PROT_STOP_WAIT_IRQ_CYCLES;
	}

	do {
		ret = aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &irq_stat);
		AWLOGD(aw9620x->dev, "REG_HOSTIRQSRC :0x%x", irq_stat);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "read REG_HOSTIRQSRC err");
			return -AW_ERR;
		}
		if (((irq_stat >> 29) & 0x01) == 1) {
			AWLOGD(aw9620x->dev, "irq_stat bit29 = 1  cmd send success!");
			break;
		} else {
			AWLOGE(aw9620x->dev, "REG_HOSTIRQSRC val: 0x%x cnt: %d", irq_stat, cnt);
		}
		msleep(1);
	} while (cnt--);

	if (cnt == -1) {
		AWLOGD(aw9620x->dev, "irq_stat != 0 cmd send err!");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_read_init_comp_irq(struct aw9620x *aw9620x)
{
	uint32_t irq_stat = 0;
	int32_t cnt = 10;
	int32_t ret = 0;

	do {
		ret = aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &irq_stat);
		AWLOGD(aw9620x->dev, "REG_HOSTIRQSRC :0x%x", irq_stat);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "read REG_HOSTIRQSRC err");
			return -AW_ERR;
		}

		if (irq_stat == INIT_OVER_IRQ_OK) {
			AWLOGD(aw9620x->dev, "stop_flag irq_stat = 1 cmd send success!");
			break;
		} else {
			AWLOGE(aw9620x->dev, "REG_HOSTIRQSRC val: 0x%x cnt: %d", irq_stat, cnt);
		}

		msleep(1);
	} while (cnt--);

	if (cnt == -1) {
		AWLOGD(aw9620x->dev, "irq_stat != 0 cmd send err!");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_send_once_cmd(struct aw9620x *aw9620x,
				uint8_t module, uint8_t command,
				uint8_t* send_value, uint16_t send_val_len)
{
	int32_t ret = -AW_ERR;
	uint8_t recv_len = 0;
	uint32_t delay_ms_cnt = 0;

	AWLOGD(aw9620x->dev, "enter");

	//1.send cmd
	ret = dri_to_soc_pack_send(aw9620x, module, command,
				send_val_len, send_value);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "UPDATE_START_CMD err");
		return -AW_ERR;
	}

	ret = aw9620x_i2c_write(aw9620x, AW_BT_PROT_CMD_PACK_ADDR, AW_SRAM_FIRST_DETECT);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "REG_CMD_SEND_TIRG err");
		return -AW_ERR;
	}

	//2.send trig
	ret = aw9620x_i2c_write(aw9620x, REG_CMD, REG_H2C_TRIG_PARSE_CMD);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "REG_CMD_SEND_TIRG err");
		return -AW_ERR;
	}

	switch (aw9620x->prot_update_fw_flag) {
	case SEND_START_CMD:
		recv_len = SEND_START_CMD_RECV_LEN;
		delay_ms_cnt = SEND_START_CMD_DELAY_MS;
		break;
	case SEND_ERASE_SECTOR_CMD:
		recv_len = SEND_ERASE_CHIP_CMD_RECV_LEN;
		delay_ms_cnt = SEND_ERASE_SECTOR_CMD_DELAY_MS;
		break;
	case SEND_UPDATE_FW_CMD:
		recv_len = SEND_UPDATE_FW_CMD_RECV_LEN;
		delay_ms_cnt = 0;
		break;
	case SEND_UPDATE_CHECK_CODE_CMD:
		recv_len = SEND_UPDATE_CHECK_CODE_CMD_RECV_LEN;
		delay_ms_cnt = SEND_UPDATE_CHECK_CODE_CMD_DELAY_MS;
		break;
	case SEND_RESTORE_CMD:
		recv_len = SEND_RESTORE_CMD_RECV_LEN;
		delay_ms_cnt = SEND_RESTORE_CMD_DELAY_MS;
		break;
	default:
		recv_len = 0;
		delay_ms_cnt = 0;
		break;
	}

	AWLOGD(aw9620x->dev, "delay_ms_cnt = %d", delay_ms_cnt);

	msleep(delay_ms_cnt);

	//3.Read interrupt information, wait 100ms
	ret = aw9620x_read_ack_irq(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "read_ack_irq err");
		return -AW_ERR;
	}

	//4.read start ack and pare pack
	ret = soc_to_dri_pack_parse(aw9620x, recv_len, module, command);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "soc_to_dri_pack_parse err");
		return -AW_ERR;
	}

	return AW_OK;
}

static int32_t aw9620x_send_stop_cmd(struct aw9620x *aw9620x)
{

	int32_t ret = -AW_ERR;

	AWLOGD(aw9620x->dev, "enter");

	aw9620x_i2c_write(aw9620x, AW_REG_MCFG, AW_CPU_HALT);
	aw9620x_i2c_write(aw9620x, AW_REG_ACESS_EN, AW_ACC_PERI);
	aw9620x_i2c_write_bits(aw9620x, REG_UPDATA_DIS, AW_REG_UPDATA_DIS_MASK, 0);
	aw9620x_i2c_write(aw9620x, AW_REG_ACESS_EN, REG_ACCESSEN_CLOSE);
	aw9620x_i2c_write(aw9620x, AW_REG_MCFG, AW_CPU_RUN);

	msleep(SEND_STOP_CMD_DELAY_MS);

	//Read interrupt information, wait 10ms
	ret = aw9620x_read_init_comp_irq(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "stop read_ack_irq err");
		return -AW_ERR;
	}

	return AW_OK;
}


static int32_t aw9620x_reg_read_all_val(struct aw9620x *aw9620x,
					uint32_t *read_data,
					uint32_t read_len,
					uint8_t flash_addr_flag,
					uint16_t start_addr)
{
	int32_t ret = 0;

	AWLOGD(aw9620x->dev,  "enter");

	//1.config FMC reg
	ret = aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_OPEN_APB_ACCESS_EN);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write 0xff20 err");
		return -AW_ERR;
	}
	//2.config PMU_CFG reg
	ret = aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_ENSET_PMU_CFG);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "config PMU_CFG reg err");
		return -AW_ERR;
	}

	ret = aw9620x_reg_read_val(aw9620x, read_data, start_addr);
	if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "reg_read_bin err");
			return -AW_ERR;
	}
	AWLOGD(aw9620x->dev,  "addr = 0x%04x read data = 0x%08x",
					start_addr, *read_data);

	return AW_OK;
}

static int32_t aw9620x_cycle_write_firmware(struct aw9620x *aw9620x,
					    uint8_t *fw_data, uint32_t firmware_len,
					    uint32_t flash_addr)
{
	uint8_t value_head_len = TRANSFER_SEQ_LEN + TRANSFER_DTS_ADDR_LEN;
	uint32_t seq = 1;
	int32_t ret = -AW_ERR;
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t start_addr = 0;
	uint32_t word_comp_len = 0;
	uint32_t cycle_cnt = 0;
	uint32_t cycle_cnt_last_len = 0;
	uint32_t send_all_len = 0;
	uint8_t *firmware_info = NULL;
	uint32_t half_cache_len = AW_CACHE_LEN - AW_PROT_HEAD_MAX_SZIE; //max.4058

	AWLOGD(aw9620x->dev, "enter");

	if (firmware_len % WORD_LEN != 0) {
		word_comp_len = WORD_LEN - firmware_len % WORD_LEN;
		AWLOGE(aw9620x->dev, "word_comp_len = %d", word_comp_len);
	}

	firmware_len = firmware_len + word_comp_len;
	cycle_cnt = firmware_len / half_cache_len;
	cycle_cnt_last_len = firmware_len % half_cache_len;
	send_all_len = firmware_len + value_head_len;

	AWLOGD(aw9620x->dev, "firmware_len = %d cycle_cnt = %d cycle_cnt_last_len = %d send_all_len = %d",
			firmware_len, cycle_cnt, cycle_cnt_last_len, send_all_len);


	firmware_info = (uint8_t *)kzalloc(send_all_len, GFP_KERNEL);
	if (firmware_info == NULL) {
		AWLOGE(aw9620x->dev, "devm_kzalloc err");
		return -AW_OK;
	}

	//Insufficient word makes up for 0xff
	memset(firmware_info, 1, send_all_len);

	for (i = 0; i < cycle_cnt; i++) {
		firmware_info[0] = (uint8_t)(seq >> OFFSET_BIT_24);
		firmware_info[1] = (uint8_t)(seq >> OFFSET_BIT_16);
		firmware_info[2] = (uint8_t)(seq >> OFFSET_BIT_8);
		firmware_info[3] = (uint8_t)(seq);

		firmware_info[4] = (uint8_t)(flash_addr >> OFFSET_BIT_24);
		firmware_info[5] = (uint8_t)(flash_addr >> OFFSET_BIT_16);
		firmware_info[6] = (uint8_t)(flash_addr >> OFFSET_BIT_8);
		firmware_info[7] = (uint8_t)(flash_addr);

		for (j = 0; j < value_head_len; j += 4) {
			AWLOGD(aw9620x->dev,
				"cnt = %d tranfer head info 0x%02x 0x%02x 0x%02x 0x%02x",
				i, firmware_info[j + 0], firmware_info[j + 1],
				firmware_info[j + 2], firmware_info[j + 3]);
		}

		AWLOGD(aw9620x->dev, "half_cache_len = %d", half_cache_len);
		memcpy(firmware_info + value_head_len,
				&(fw_data[start_addr + half_cache_len * i]), half_cache_len);

		aw9620x_bin_conver(firmware_info + value_head_len, half_cache_len);

		ret = aw9620x_send_once_cmd(aw9620x, UPDATE_MODULE, UPDATE_TRANSFER_CMD,
					firmware_info, half_cache_len + value_head_len);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "send_transfer_cmd_once err");
			goto err_hand;
		}

		flash_addr += half_cache_len;
		seq++;
	}


	if (cycle_cnt_last_len != 0) {
		firmware_info[0] = (uint8_t)(seq >> OFFSET_BIT_24);
		firmware_info[1] = (uint8_t)(seq >> OFFSET_BIT_16);
		firmware_info[2] = (uint8_t)(seq >> OFFSET_BIT_8);
		firmware_info[3] = (uint8_t)(seq);

		firmware_info[4] = (uint8_t)(flash_addr >> OFFSET_BIT_24);
		firmware_info[5] = (uint8_t)(flash_addr >> OFFSET_BIT_16);
		firmware_info[6] = (uint8_t)(flash_addr >> OFFSET_BIT_8);
		firmware_info[7] = (uint8_t)(flash_addr);

		for (i = 0; i < value_head_len; i += 4) {
			AWLOGD(aw9620x->dev,
				"last_len = %d tranfer head info 0x%02x 0x%02x 0x%02x 0x%02x",
				cycle_cnt_last_len, firmware_info[i + 0], firmware_info[i + 1],
				firmware_info[i + 2], firmware_info[i + 3]);
		}

		memcpy(firmware_info + value_head_len,
				&(fw_data[start_addr + cycle_cnt * half_cache_len]),
				cycle_cnt_last_len);

		aw9620x_bin_conver(firmware_info + value_head_len, cycle_cnt_last_len);

		ret = aw9620x_send_once_cmd(aw9620x,
				UPDATE_MODULE, UPDATE_TRANSFER_CMD,
				firmware_info, cycle_cnt_last_len + value_head_len);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "send_transfer_cmd_once err");
			goto err_hand;
		}
	}

	if (firmware_info != NULL) {
		kfree(firmware_info);
		firmware_info = NULL;
	}
	return AW_OK;
err_hand:
	if (firmware_info != NULL) {
		kfree(firmware_info);
		firmware_info = NULL;
	}
	return -AW_ERR;
}

static int32_t aw9620x_write_firmware_checksum(struct aw9620x *aw9620x, uint8_t *p_checksum)
{
	uint8_t i = 0;
	uint8_t j = 0;
	uint8_t transfer_value_w[AW_EN_TR_CHECK_VALUE_LEN] = { 0 };
	int32_t ret = 0;
	uint32_t r_check_en = 0;
	uint32_t r_checksum = 0;
	uint32_t w_checksum = 0;

	AWLOGD(aw9620x->dev, "enter");

	for (i = 0; i < 4; i++) {
		transfer_value_w[i] = (uint8_t)(0 >> ((3 - i) * 8));
	}

	for (i = 4; i < 8; i++) {
		transfer_value_w[i] = (uint8_t)(AW_FW_CHECKSUM_EN_ADDR >> ((3 - j) * 8));
		j++;
	}

	j = 0;
	for (i = 8; i < 12; i++) {
		transfer_value_w[i] = (uint8_t)(AW_CHECK_EN_VAL >> ((3 - j) * 8));
		j++;
	}

	j = 0;
	for (i = 12; i < 16; i++) {
		transfer_value_w[i] = p_checksum[j];
		j++;
	}

	for (i = 0; i < 4; i++) {
		AWLOGD(aw9620x->dev, "0x%x 0x%x 0x%x 0x%x",
			transfer_value_w[i * 4 + 0], transfer_value_w[i * 4 + 1],
			transfer_value_w[i * 4 + 2], transfer_value_w[i * 4 + 3]);
	}

	ret = aw9620x_send_once_cmd(aw9620x,
				UPDATE_MODULE, UPDATE_TRANSFER_CMD,
				transfer_value_w, AW_EN_TR_CHECK_VALUE_LEN);

	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "aw9620x_write_firmware_checksum err");
		return -AW_ERR;
	}

	ret = aw9620x_reg_read_val(aw9620x, &r_check_en, AW_FW_CHECK_EN_ADDR);
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "aw9620x_read check_en err");
		return -AW_ERR;
	}

	ret = aw9620x_reg_read_val(aw9620x, &r_checksum, AW_FW_CHECK_CODE_ADDR);
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "aw9620x_read check_en err");
		return -AW_ERR;
	}

	w_checksum = AW_GET_32_DATA(p_checksum[0], p_checksum[1], p_checksum[2], p_checksum[3]);

	AWLOGD(aw9620x->dev, "r_check_en :0x%08x, r_checksum 0x%08x", r_check_en, r_checksum);

	if ((r_check_en == AW_CHECK_EN_VAL) && (r_checksum == w_checksum)) {
		AWLOGD(aw9620x->dev, "Consistent reading and writing");
		return AW_OK;

	} else {
		AWLOGE(aw9620x->dev, "err ! r_check_en != AW_CHECK_EN_VAL || r_checksum != w_checksum");
		return -AW_ERR;
	}
}

static int32_t aw9620x_get_fw_and_bt_info(struct aw9620x *aw9620x,
				uint8_t *fw_data,uint32_t fw_len,
				uint8_t *p_fw_check_sum)
{
	uint32_t fw_checksum = 0;
	int32_t ret = 0;

	aw9620x->bt_fw_info.update_flag = AW_TURE;

	ret = aw9620x_i2c_read(aw9620x, AW_BT_VER_INF_VERSION, &aw9620x->bt_fw_info.bt_version);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "read AW_BT_VER_INF_VERSION err");
		return -AW_ERR;
	}

	ret = aw9620x_i2c_read(aw9620x, AW_BT_VER_INF_DATE, &aw9620x->bt_fw_info.bt_date);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "read AW_BT_VER_INF_DATE err");
		return -AW_ERR;
	}

	ret = aw9620x_reg_read_all_val(aw9620x, &aw9620x->bt_fw_info.bt_checksum, WORD_LEN,
				FLASH_ADDR_BOOT, AW_BT_CHECK_SUM_ADDR);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "read bt_checksum err");
		return -AW_ERR;
	}
	AWLOGI(aw9620x->dev, "boot version:0x%08x, date:0x%08x, checksum 0x%08x",
			aw9620x->bt_fw_info.bt_version, aw9620x->bt_fw_info.bt_date, aw9620x->bt_fw_info.bt_checksum);

	fw_checksum = aw9620x_get_bin_checksum(fw_data, fw_len, AW_FW_CHECK_LEN);
	p_fw_check_sum[0] = (uint8_t)(fw_checksum >> 24);
	p_fw_check_sum[1] = (uint8_t)(fw_checksum >> 16);
	p_fw_check_sum[2] = (uint8_t)(fw_checksum >> 8);
	p_fw_check_sum[3] = (uint8_t)(fw_checksum >> 0);

	aw9620x->bt_fw_info.fw_checksum = fw_checksum;

	AWLOGI(aw9620x->dev, "firmwarw checksum 0x%08x", fw_checksum);

	return AW_OK;
}

static int32_t aw9620x_send_online_cmd(struct aw9620x *aw9620x)
{
	uint8_t i = 0;
	int32_t ret = 0;
	uint32_t reg_wst_val = 0;

	ret = aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_SLEEP_MODE);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "REG_CMD_SEND_TIRG err");
		return -AW_ERR;
	}

	for (i = 0; i < AW_WAIT_ENTER_SLEEP_MODE; i++) {
		ret = aw9620x_i2c_read(aw9620x, REG_WST, &reg_wst_val);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "REG_CMD_SEND_TIRG err");
			return -AW_ERR;
		}
		if (reg_wst_val == AW_REG_WST_MODE_SLEEP) {
			AWLOGD(aw9620x->dev, "enter SLEEP MODE OK!");
			break;
		}
		msleep(1);
	}

	if (i == AW_WAIT_ENTER_SLEEP_MODE) {
		ret = aw9620x_i2c_read(aw9620x, AW_PUB_VER_REG_WST, &reg_wst_val);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "REG_CMD_SEND_TIRG err");
			return -AW_ERR;
		}
		if (reg_wst_val == AW_REG_WST_MODE_SLEEP) {
			AWLOGD(aw9620x->dev, "enterA W_PUB_VER SLEEP MODE OK!");
		} else {
			AWLOGE(aw9620x->dev, "enter AW_PUB_VER SLEEP MODE err");
			return -AW_ERR;
		}
	}

	ret = aw9620x_i2c_write(aw9620x, AW_BT_HOST2CPU_TRIG, AW_BT_HOST2CPU_TRIG_ONLINE_UPGRADE_CMD);
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "REG_CMD_SEND_TIRG err");
		return -AW_ERR;
	}

	msleep(SEND_UPDTAE_CMD_DELAY_MS);

	ret = aw9620x_read_ack_irq(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "read_ack_irq err");
		return -AW_ERR;
	}

	return AW_OK;
}

//erase 63 sector ()
static int32_t aw9620x_flash_erase_sector(struct aw9620x *aw9620x)
{
	uint8_t i = 0;
	uint32_t erase_addr = AW_FLASH_ERASE_SECTOR_START_ADDR;
	uint8_t addr_buf[4] = { 0 };
	int32_t ret = 0;

	for (i = 0; i < AW_ERASE_SECTOR_CNT; i++) {
		addr_buf[0] = (uint8_t)(erase_addr >> OFFSET_BIT_24);
		addr_buf[1] = (uint8_t)(erase_addr >> OFFSET_BIT_16);
		addr_buf[2] = (uint8_t)(erase_addr >> OFFSET_BIT_8);
		addr_buf[3] = (uint8_t)(erase_addr);
		aw9620x->prot_update_fw_flag = SEND_ERASE_SECTOR_CMD;
		ret = aw9620x_send_once_cmd(aw9620x,
					FLASH_MODULE, FLASH_ERASE_SECTOR_CMD,
					addr_buf, sizeof(addr_buf));
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "send_UPDATE_START_CMD_cmd err i = %d", i);
			break;
		}
		AWLOGD(aw9620x->dev, "cnt : %d, addr = 0x%x", i, erase_addr);
		erase_addr += AW_SECTOR_SIZE;
	}

	return ret;
}

static int32_t aw9620x_flash_erase_last_sector(struct aw9620x *aw9620x)
{
	uint32_t erase_addr = AW_FLASH_ERASE_SECTOR_START_ADDR + AW_SECTOR_SIZE * AW_ERASE_SECTOR_CNT;
	uint8_t addr_buf[4] = { 0 };
	int32_t ret = 0;

	addr_buf[0] = (uint8_t)(erase_addr >> OFFSET_BIT_24);
	addr_buf[1] = (uint8_t)(erase_addr >> OFFSET_BIT_16);
	addr_buf[2] = (uint8_t)(erase_addr >> OFFSET_BIT_8);
	addr_buf[3] = (uint8_t)(erase_addr);
	aw9620x->prot_update_fw_flag = SEND_ERASE_SECTOR_CMD;
	ret = aw9620x_send_once_cmd(aw9620x,
				FLASH_MODULE, FLASH_ERASE_SECTOR_CMD,
				addr_buf, sizeof(addr_buf));
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "send_UPDATE_START_CMD_cmd");
		return -AW_ERR;
	}
	AWLOGD(aw9620x->dev, "cnt : %d, addr = 0x%x", 64, erase_addr);

	return AW_OK;
}

static int32_t aw9620x_send_all_update_cmd(struct aw9620x *aw9620x,
					uint8_t *fw_data,
					uint32_t fw_len,
					uint8_t direct_update_flag)
{
	int8_t update_flag = AW_TURE;
	int32_t ret = -AW_ERR;
	uint32_t data_tmp = 0;
	uint8_t fw_check_sum[4] = { 0 };
	uint32_t reg_boot_loader_active_val = 0;

	AWLOGD(aw9620x->dev, "enter");

	do {
		//1.Send online upgrade command
		AWLOGD(aw9620x->dev, "1.Send online upgrade command");
		ret = aw9620x_send_online_cmd(aw9620x);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "1.Send online upgrade command err");
			update_flag = AW_FALSE;
			break;
		}

		ret = aw9620x_get_fw_and_bt_info(aw9620x, fw_data, fw_len, fw_check_sum);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "aw9620x_get_fw_and_bt_infor");
			update_flag = AW_FALSE;
			break;
		}

		//2.send start cmd
		AWLOGD(aw9620x->dev, "2.send start cmd");
		aw9620x->prot_update_fw_flag = SEND_START_CMD;
		ret = aw9620x_send_once_cmd(aw9620x,
				UPDATE_MODULE, UPDATE_START_CMD,
				P_AW_START_CMD_SEND_VALUE, AW_START_CMD_SEND_VALUE_LEN);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "send_UPDATE_START_CMD_cmd err");
			update_flag = AW_FALSE;
			break;
		}

		AWLOGD(aw9620x->dev, "3.a en fw check erase_last_sector");
		ret = aw9620x_flash_erase_last_sector(aw9620x);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "aw9620x_flash_erase_sector err");
			update_flag = AW_FALSE;
			break;
		}

		AWLOGD(aw9620x->dev, "3.b en fw check");
		aw9620x->prot_update_fw_flag = SEND_UPDATE_CHECK_CODE_CMD;
		ret = aw9620x_write_firmware_checksum(aw9620x, fw_check_sum);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "aw9620x_write_firmware_checksum err");
			update_flag = AW_FALSE;
			break;
		}

		//3.send Erase Chip Cmd
		AWLOGD(aw9620x->dev, "4.send Erase Chip Cmd");
		ret = aw9620x_flash_erase_sector(aw9620x);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "aw9620x_flash_erase_sector err");
			update_flag = AW_FALSE;
			break;
		}

		//4.Cycle write firmware
		AWLOGD(aw9620x->dev, "5.Cycle write firmware");
		aw9620x->prot_update_fw_flag = SEND_UPDATE_FW_CMD;
		ret = aw9620x_cycle_write_firmware(aw9620x, fw_data, fw_len, AW_FLASH_HEAD_ADDR);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "cycle_write_firmware err");
			update_flag = AW_FALSE;
			break;
		}

		//6.send stop
		AWLOGD(aw9620x->dev, "6.send stop");
		aw9620x->prot_update_fw_flag = SEND_STOP_CMD;
		ret = aw9620x_send_stop_cmd(aw9620x);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "stop err");
			update_flag = AW_FALSE;
			break;
		} else {
			AWLOGD(aw9620x->dev, "aw9620x_send_all_update_cmd ok, update firmware ok!");
		}
	} while(0);

	if (update_flag == AW_FALSE) {
		AWLOGD(aw9620x->dev, "Write through protocol failed, start write through register");

		ret = aw9620x_i2c_write(aw9620x, AW_REG_ACESS_EN, REG_OPEN_APB_ACCESS_EN);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "REG_APB_ACCESS_EN err");
			return -AW_ERR;
		}

		ret = aw9620x_i2c_read(aw9620x, REG_BOOT_LOADER_ACTIVE, &reg_boot_loader_active_val);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "read REG_BOOT_LOADER_ACTIVE err");
			return -AW_ERR;
		}
		if (reg_boot_loader_active_val != 0) {
			ret = aw9620x_i2c_write(aw9620x, REG_BOOT_LOADER_ACTIVE, 0);
				if (ret != AW_OK) {
				AWLOGE(aw9620x->dev, "write REG_BOOT_LOADER_ACTIVE err");
				return -AW_ERR;
			}
		}

		ret = aw9620x_i2c_read(aw9620x, REG_UPDATA_DIS, &data_tmp);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "0x4744 read err");
			return -AW_ERR;
		}
		//if ((data_tmp & 0x00ffffff) != 0) {
		if (((data_tmp >> 24) & 0xff) != 0) {
			ret = aw9620x_i2c_write(aw9620x, REG_UPDATA_DIS, data_tmp & AW_REG_UPDATA_DIS_MASK);
			if (ret != AW_OK) {
				AWLOGE(aw9620x->dev, "0x4744 wr err");
				return -AW_ERR;
			}
		}

		ret = aw9620x_i2c_write(aw9620x, AW_REG_ACESS_EN, REG_ACCESSEN_CLOSE);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "REG_APB_ACCESS_EN wr err");
			return -AW_ERR;
		}

		if (direct_update_flag == AW_TURE) {
			AWLOGD(aw9620x->dev, "aw9620x->direct_updata_flag = AW_TURE");
			return -AW_ERR;
		}

		return -AW_PROT_UPDATE_ERR;
	}

	AWLOGE(aw9620x->dev, "prot update fw ok!!!");

	return AW_OK;
}

int32_t aw9620x_fw_update(struct aw9620x *aw9620x,
			uint8_t direct_update_flag, uint8_t *p_fw_data,
			uint32_t fw_len, uint32_t app_version)
{
	int8_t cmp_val = 0;

	struct aw_fw_load_info fw_load_info = {
		.fw_len = fw_len,
		.p_fw_data = p_fw_data,
		.app_version = app_version,
		.direct_update_flag = direct_update_flag,
	};

	AWLOGE(aw9620x->dev, "fw_len = %d,app_version = 0x%x", fw_load_info.fw_len, fw_load_info.app_version);
	//direct updata_fw
	if (fw_load_info.direct_update_flag == AW_FALSE) {
		aw9620x_fw_version_cmp(aw9620x, &cmp_val, fw_load_info.app_version);
		if (cmp_val != BIN_VER_GREATER_THAN_SOC_VER) {
	 		AWLOGD(aw9620x->dev, "bin fw not greater than soc fw, no update!");
	 		return AW_OK;
		}
	}

	return aw9620x_send_all_update_cmd(aw9620x, fw_load_info.p_fw_data, fw_load_info.fw_len, direct_update_flag);
}

/*****************aw_protocol_transfer end**************************/
#ifdef AW_USE_IRQ_FLAG
static int32_t aw9620x_input_init(struct aw9620x *aw9620x, uint8_t *err_num)
{
	uint32_t i = 0;
	int32_t ret = 0;
	uint32_t j = 0;

	aw9620x->channels_arr = devm_kzalloc(aw9620x->dev,
				sizeof(struct aw_channels_info) *
				AW_CHANNEL_NUM_MAX,
				GFP_KERNEL);
	if (aw9620x->channels_arr == NULL) {
		AWLOGE(aw9620x->dev, "devm_kzalloc err");
		return -AW_ERR;
	}

	for (i = 0; i < AW_CHANNEL_NUM_MAX; i++) {
		snprintf(aw9620x->channels_arr[i].name,
				sizeof(aw9620x->channels_arr->name),
				"nubia_tgk_aw_sar%d_ch%d",
				aw9620x->sar_num, i);

		aw9620x->channels_arr[i].last_channel_info = TRIGGER_FAR;

		if ((aw9620x->channel_use_flag >> i) & 0x01) {
			aw9620x->channels_arr[i].used = AW_TURE;
			aw9620x->channels_arr[i].input = input_allocate_device();
			if (aw9620x->channels_arr[i].input == NULL) {
				*err_num = i;
				goto exit_input;
			}
			aw9620x->channels_arr[i].input->name = aw9620x->channels_arr[i].name;
			__set_bit(EV_KEY, aw9620x->channels_arr[i].input->evbit);
			__set_bit(EV_SYN, aw9620x->channels_arr[i].input->evbit);
			__set_bit(KEY_F1, aw9620x->channels_arr[i].input->keybit);
			if(aw9620x->sar_num == 0) {
				__set_bit(KEY_F7, aw9620x->channels_arr[i].input->keybit);
				AWLOGI(aw9620x->dev, "F7 event init");
			}
			else if(aw9620x->sar_num == 1) {
				__set_bit(KEY_F8, aw9620x->channels_arr[i].input->keybit);
				AWLOGI(aw9620x->dev, "F8 event init");
			}
			input_set_abs_params(aw9620x->channels_arr[i].input,
						ABS_DISTANCE, -1, 100, 0, 0);
			ret = input_register_device(aw9620x->channels_arr[i].input);
			if (ret) {
				AWLOGE(aw9620x->dev, "failed to register input device");
				goto exit_input;
			}
		} else {
			aw9620x->channels_arr[i].used = AW_FALSE;
			aw9620x->channels_arr[i].input = NULL;
		}
	}

	return AW_OK;

exit_input:
	for (j = 0; j < i; j++) {
		if(aw9620x->channels_arr[j].input != NULL) {
			input_free_device(aw9620x->channels_arr[j].input);
		}
	}
	return -AW_ERR;
}

void aw9620x_irq_handle_func(struct aw9620x *aw9620x)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint32_t irq_status = 0;
	uint32_t curr_status_val = 0;
	uint32_t curr_status = 0;

	ret = aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &irq_status);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "i2c IO error");
		return;
	}
	//AWLOGI(aw9620x->dev, "IRQSRC = 0x%x", irq_status);

	if ((irq_status & 0x01) == 1) {
		aw_init_irq_flag [aw9620x->sar_num] = AW_TURE;
	}
	ret = aw9620x_i2c_read(aw9620x, REG_STAT0, &curr_status_val);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "i2c IO error");
		return;
	}
	//AWLOGI(aw9620x->dev, "STAT0 = 0x%x", curr_status_val);

	for (i = 0; i < AW_CHANNEL_NUM_MAX; i++) {
		curr_status = (((curr_status_val >> (OFFSET_BIT_8 + i)) & 0x1) << 1) |
				(((curr_status_val >> (i)) & 0x1));
		if (aw9620x->channels_arr[i].used == AW_FALSE) {
			//AWLOGD(aw9620x->dev, "channels_arr[%d] no user", i);
			continue;
		}
		if (aw9620x->channels_arr[i].last_channel_info == curr_status) {
			continue;
		}

		switch (curr_status) {
		case TRIGGER_FAR:
			input_report_abs(aw9620x->channels_arr[i].input, ABS_DISTANCE, TRIGGER_FAR);
			if(aw9620x->sar_num == 0) {
				input_report_key(aw9620x->channels_arr[i].input, KEY_F7, 0);
				AWLOGI(aw9620x->dev, "TRIGGER_FAR: F7 event report");
			}

			else if(aw9620x->sar_num == 1) {
				input_report_key(aw9620x->channels_arr[i].input, KEY_F8, 0);
				AWLOGI(aw9620x->dev, "TRIGGER_FAR: F8 event report");
			}
			break;
		case TRIGGER_TH0:
			input_report_abs(aw9620x->channels_arr[i].input, ABS_DISTANCE, TRIGGER_TH0);
			if(aw9620x->sar_num == 0) {
				input_report_key(aw9620x->channels_arr[i].input, KEY_F7, 1);
				AWLOGI(aw9620x->dev, "TRIGGER_TH0: F7 event report");
			}
			else if(aw9620x->sar_num == 1) {
				input_report_key(aw9620x->channels_arr[i].input, KEY_F8, 1);
				AWLOGI(aw9620x->dev, "TRIGGER_TH0: F8 event report");
			}
			break;
		case TRIGGER_TH1:
			input_report_abs(aw9620x->channels_arr[i].input, ABS_DISTANCE, TRIGGER_TH1);
			break;
		default:
			AWLOGE(aw9620x->dev, "error abs distance");
			return;
		}
		input_sync(aw9620x->channels_arr[i].input);

		aw9620x->channels_arr[i].last_channel_info = curr_status;
	}
}

static irqreturn_t aw9620x_irq(int irq, void *data)
{
	struct aw9620x *aw9620x = data;

	aw9620x_irq_handle_func(aw9620x);

	return IRQ_HANDLED;
}

static int32_t aw9620x_irq_init(struct aw9620x *aw9620x)
{
	int32_t irq_flags = 0;
	int32_t ret = 0;
	uint8_t irq_gpio_name[30] = { 0 };

	AWLOGD(aw9620x->dev, "enter");

	snprintf(irq_gpio_name, sizeof(irq_gpio_name),
					"aw_sar%d_irq", aw9620x->sar_num);

	if (gpio_is_valid(aw9620x->irq_gpio)) {
		AWLOGD(aw9620x->dev, "gpio_is_valid-sar%d",aw9620x->sar_num);
		aw9620x->to_irq = gpio_to_irq(aw9620x->irq_gpio);
		AWLOGD(aw9620x->dev, "gpio_to_irq-sar%d",aw9620x->sar_num);
		ret = devm_gpio_request_one(aw9620x->dev,
					aw9620x->irq_gpio,
					GPIOF_DIR_IN | GPIOF_INIT_HIGH,
					irq_gpio_name);
		AWLOGD(aw9620x->dev, "devm_gpio_request_one-sar%d",aw9620x->sar_num);
		if (ret) {
			AWLOGE(aw9620x->dev,
				"request irq gpio failed, ret = %d", ret);
			ret = -AW_ERR;
		} else {
			/* register irq handler */
			irq_flags = IRQF_ONESHOT |IRQF_TRIGGER_LOW;

			ret = devm_request_threaded_irq(aw9620x->dev,
							aw9620x->to_irq, NULL,
							aw9620x_irq, irq_flags,
							"aw9620x_irq", aw9620x);
			if (ret != 0) {
				AWLOGE(aw9620x->dev,
						"failed to request IRQ %d: %d",
						aw9620x->to_irq, ret);
				ret = -AW_ERR;
			} else {
				AWLOGI(aw9620x->dev,
					"IRQ request successfully!");
				ret = AW_OK;
			}
		}
	} else {
		AWLOGE(aw9620x->dev, "irq gpio invalid!");
		return -AW_ERR;
	}

	AWLOGI(aw9620x->dev, "disable_irq");
	disable_irq(aw9620x->to_irq);
	aw9620x->host_irq_stat = IRQ_DISABLE;

	return ret;
}
#endif

static int32_t aw9620x_awrw_data_analysis(struct aw9620x *aw9620x,
						const char *buf, uint8_t len)
{
	uint8_t i = 0 ;
	uint8_t data_temp[2] = { 0 };
	uint8_t index = 0;
	uint32_t tranfar_data_temp = 0;
	uint32_t theory_len = len * AW9620X_AWRW_DATA_WIDTH +
							AW9620X_AWRW_OffSET;
	uint32_t actual_len = strlen(buf);

	AWLOGD(aw9620x->dev, "enter");

	if (theory_len != actual_len) {
		AWLOGD(aw9620x->dev,
			"error theory_len = %d actual_len = %d",
					theory_len, actual_len);
		return -AW_ERR;
	}

	for (i = 0; i < len * AW9620X_AWRW_DATA_WIDTH;
					i += AW9620X_AWRW_DATA_WIDTH) {
		data_temp[0] = buf[AW9620X_AWRW_OffSET + i + AW_DATA_OffSET_2];
		data_temp[1] = buf[AW9620X_AWRW_OffSET + i + AW_DATA_OffSET_3];

		if (sscanf(data_temp, "%02x", &tranfar_data_temp) == 1) {
			aw9620x->awrw_info.p_i2c_tranfar_data[index] =
						(uint8_t)tranfar_data_temp;
			AWLOGD(aw9620x->dev, "tranfar_data = 0x%2x",
				aw9620x->awrw_info.p_i2c_tranfar_data[index]);
		}
		index++;
	}

	return 0;
}

static int32_t aw9620x_awrw_write(struct aw9620x *aw9620x, const char *buf)
{
	int32_t ret = 0;

	ret = aw9620x_awrw_data_analysis(aw9620x, buf,
				aw9620x->awrw_info.i2c_tranfar_data_len);
	if (ret == 0)
		i2c_write_seq(aw9620x);

	return ret;
}

static int32_t aw9620x_awrw_read(struct aw9620x *aw9620x, const char *buf)
{
	int32_t ret = 0;
	uint8_t *p_buf = aw9620x->awrw_info.p_i2c_tranfar_data + aw9620x->awrw_info.addr_len;
	uint32_t len = (uint16_t)(aw9620x->awrw_info.data_len * aw9620x->awrw_info.reg_num);

	ret = aw9620x_awrw_data_analysis(aw9620x, buf, aw9620x->awrw_info.addr_len);
	if (ret == 0) {
		ret = i2c_read_seq(aw9620x);
		if (ret != AW_OK) {
			memset(p_buf, 0xff, len);
		}
	}

	return ret;
}

static ssize_t aw9620x_awrw_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint i = 0;
	ssize_t len = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (aw9620x->awrw_info.p_i2c_tranfar_data == NULL) {
		AWLOGE(aw9620x->dev, "p_i2c_tranfar_data is NULL");
		return len;
	}

	for (i = 0; i < (aw9620x->awrw_info.data_len) *
					(aw9620x->awrw_info.reg_num); i++)
		len += snprintf(buf + len, PAGE_SIZE - len,
			"0x%02x,",
			aw9620x->awrw_info.p_i2c_tranfar_data[aw9620x->awrw_info.addr_len + i]);

	len += snprintf(buf + len - 1, PAGE_SIZE - len, "\n");

	devm_kfree(aw9620x->dev, aw9620x->awrw_info.p_i2c_tranfar_data);
	aw9620x->awrw_info.p_i2c_tranfar_data = NULL;

	return len - 1;
}

static ssize_t aw9620x_awrw_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	uint32_t rw_flag = 0;
	uint32_t addr_len = 0;
	uint32_t data_len = 0;
	uint32_t reg_num = 0;
	int32_t ret = 0;
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "0x%02x 0x%02x 0x%02x 0x%02x", &rw_flag, &addr_len,
					&data_len, &reg_num) != 4) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	AWLOGD(aw9620x->dev, "aw9620x 0x%02x 0x%02x 0x%02x 0x%02x",
				rw_flag, addr_len, data_len, reg_num);

	if (rw_flag != I2C_WR && rw_flag != I2C_RD) {
		AWLOGE(aw9620x->dev, "parameter err");
		return count;
	}

	aw9620x->awrw_info.rw_flag = (uint8_t)rw_flag;
	aw9620x->awrw_info.addr_len = (uint8_t)addr_len;
	aw9620x->awrw_info.data_len = (uint8_t)data_len;
	aw9620x->awrw_info.reg_num = (uint8_t)reg_num;

	aw9620x->awrw_info.i2c_tranfar_data_len =
					aw9620x->awrw_info.addr_len +
					aw9620x->awrw_info.data_len *
					aw9620x->awrw_info.reg_num;

	if (aw9620x->awrw_info.p_i2c_tranfar_data != NULL) {
		devm_kfree(aw9620x->dev, aw9620x->awrw_info.p_i2c_tranfar_data);
		aw9620x->awrw_info.p_i2c_tranfar_data = NULL;
	}

	aw9620x->awrw_info.p_i2c_tranfar_data =
			devm_kzalloc(aw9620x->dev,
			aw9620x->awrw_info.i2c_tranfar_data_len, GFP_KERNEL);
	if (aw9620x->awrw_info.p_i2c_tranfar_data == NULL) {
		AWLOGE(aw9620x->dev, "devm_kzalloc error");
		return count;
	}

	if (aw9620x->awrw_info.rw_flag == I2C_WR) {
		ret = aw9620x_awrw_write(aw9620x, buf);
		if (ret != 0)
			AWLOGE(aw9620x->dev, "awrw_write error");
		if (aw9620x->awrw_info.p_i2c_tranfar_data != NULL) {
			devm_kfree(aw9620x->dev,
					aw9620x->awrw_info.p_i2c_tranfar_data);
			aw9620x->awrw_info.p_i2c_tranfar_data = NULL;
		}
	} else if (aw9620x->awrw_info.rw_flag == I2C_RD) {
		ret = aw9620x_awrw_read(aw9620x, buf);
		if (ret != 0)
			AWLOGE(aw9620x->dev, "awrw_read error");
	} else {
		return count;
	}

	return count;
}

static int32_t aw9620x_en_active(struct aw9620x *aw9620x)
{
	int32_t ret = 0;

	AWLOGD(aw9620x->dev, "enter");

	//Nubian customer demand
	ret = aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_SLEEP_MODE);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "write REG_CMD err");
		return -AW_ERR;
	} else {
		aw9620x->old_mode = AW9620X_SLEEP_MODE;
		AWLOGD(aw9620x->dev, "addr: 0x%x data: 0x%x", REG_CMD, 1);
	}

	ret = aw9620x_i2c_write(aw9620x, REG_HOSTIRQEN, aw9620x->hostirqen);
	if (ret != AW_OK) {
		AWLOGD(aw9620x->dev, "addr: 0x%x data: 0x%x",
					REG_HOSTIRQEN, aw9620x->hostirqen);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "addr: 0x%x data: 0x%x", REG_HOSTIRQEN, aw9620x->hostirqen);
	}

	if (aw9620x->host_irq_stat == IRQ_DISABLE) {
		AWLOGD(aw9620x->dev, "enable_irq ok!!!");
		enable_irq(aw9620x->to_irq);
		aw9620x->host_irq_stat = IRQ_ENABLE;
	}

	return AW_OK;
}

static void aw9620x_para_loaded(struct aw9620x *aw9620x)
{
	int32_t i = 0;
	int32_t len = ARRAY_SIZE(aw9620x_reg_default);

	AWLOGD(aw9620x->dev, "start to download para!");

	aw9620x->use_defaule_reg_arr_flag = AW_TURE;

	for (i = 0; i < len; i = i + 2) {
		if (aw9620x_reg_default[i] == REG_HOSTIRQEN) {
			aw9620x->hostirqen = aw9620x_reg_default[i+1];
			continue;
		}
		aw9620x_i2c_write(aw9620x,
				(uint16_t)aw9620x_reg_default[i],
				aw9620x_reg_default[i+1]);
		AWLOGI(aw9620x->dev, "reg_addr = 0x%04x, reg_data = 0x%08x",
						aw9620x_reg_default[i],
						aw9620x_reg_default[i+1]);
	}

	aw9620x_en_active(aw9620x);

	AWLOGI(aw9620x->dev, "para writen completely");
}

static int32_t aw9620x_load_bin(struct aw9620x *aw9620x,
						struct aw_bin *aw_bin)
{
	uint32_t i = 0;
	int32_t ret = 0;
	uint16_t reg_addr = 0;
	uint32_t reg_data = 0;
	uint32_t start_addr = aw_bin->header_info[0].valid_data_addr;

	for (i = 0; i < aw_bin->header_info[0].valid_data_len;
						i += 6, start_addr += 6) {
		reg_addr = (aw_bin->info.data[start_addr]) |
				aw_bin->info.data[start_addr + 1] << OFFSET_BIT_8;
		reg_data = aw_bin->info.data[start_addr + 2] |
			(aw_bin->info.data[start_addr + 3] << OFFSET_BIT_8) |
			(aw_bin->info.data[start_addr + 4] << OFFSET_BIT_16) |
			(aw_bin->info.data[start_addr + 5] << OFFSET_BIT_24);

		if (reg_addr == REG_HOSTIRQEN) {
			aw9620x->hostirqen = reg_data;
			continue;
		}
		ret = aw9620x_i2c_write(aw9620x, reg_addr, reg_data);
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "i2c write err");
			return -AW_ERR;
		}

		AWLOGI(aw9620x->dev,
			"reg_addr = 0x%04x, reg_data = 0x%08x",
						reg_addr, reg_data);
	}

	return AW_OK;
}

static int32_t aw9620x_parse_def_reg_bin(const struct firmware *cont, struct aw9620x *aw9620x)
{
	struct aw_bin *aw_bin = NULL;
	int32_t ret = -AW_ERR;

	if (!cont) {
		AWLOGE(aw9620x->dev, "def_reg_bin request failed");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "def_reg_bin request successfully");
	}

	AWLOGD(aw9620x->dev, "enter, cont->size = %d", (uint32_t)cont->size);

	aw_bin = kzalloc(cont->size + sizeof(struct aw_bin), GFP_KERNEL);
	if (!aw_bin) {
		kfree(aw_bin);
		release_firmware(cont);
		AWLOGE(aw9620x->dev, "failed to allcating memory!");
		return -AW_ERR;
	}

	aw_bin->info.len = cont->size;
	memcpy(aw_bin->info.data, cont->data, cont->size);

	if (cont != NULL) {
		AWLOGE(aw9620x->dev, "cont is null err");
		release_firmware(cont);
	}

	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "aw9620x parse bin fail! ret = %d", ret);
		goto err;
	}

	AWLOGE(aw9620x->dev, "reg chip name: %s, soc chip name: %s, len = %d",
			aw9620x->chip_name, aw_bin->header_info[0].chip_type, aw_bin->info.len);

	ret = strcmp(aw9620x->chip_name, aw_bin->header_info[0].chip_type);
	if (ret != 0) {
		AWLOGE(aw9620x->dev,
			"chip name(%s) incompatible with chip type(%s)",
			aw9620x->chip_name, aw_bin->header_info[0].chip_type);
		goto err;
	}

	ret = aw9620x_load_bin(aw9620x, aw_bin);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "load_bin err");
		goto err;
	}

	ret = aw9620x_en_active(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "en_active err");
		goto err;
	}

	if (aw_bin != NULL) {
		kfree(aw_bin);
	}

	AWLOGE(aw9620x->dev, "load_def_reg_bin ok!!!");

	return AW_OK;
err:
	if (aw_bin != NULL) {
		kfree(aw_bin);
	}
	return -AW_ERR;
}

static int32_t aw9620x_load_def_reg_bin(struct aw9620x *aw9620x)
{
	int32_t ret = -AW_ERR;
	const struct firmware *fw = NULL;

	AWLOGD(aw9620x->dev, "enter");

	snprintf(aw9620x->reg_name, sizeof(aw9620x->reg_name),
				"aw9620x_reg_%d.bin", aw9620x->sar_num);

	AWLOGD(aw9620x->dev, "name :%s", aw9620x->reg_name);
	ret = request_firmware(&fw, aw9620x->reg_name, aw9620x->dev);
	AWLOGD(aw9620x->dev, "ret = %d", ret);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "parse %s err!", aw9620x->reg_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "parse %s ok!", aw9620x->reg_name);
	}

	ret = aw9620x_parse_def_reg_bin(fw, aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "reg_bin %s load err!", aw9620x->reg_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "reg_bin %s load ok!", aw9620x->reg_name);
	}

	return AW_OK;
}

static int32_t aw9620x_update_boot_to_flash(struct aw9620x *aw9620x, struct aw_bin *aw_bin)
{
	uint32_t start_index = aw_bin->header_info[0].valid_data_addr;

	AWLOGD(aw9620x->dev, "enter");

	return aw9620x_update_bt_to_flash(aw9620x, &(aw_bin->info.data[start_index]), aw_bin->header_info[0].valid_data_len);
}

static int32_t aw9620x_reg_parse_boot(const struct firmware *cont, struct aw9620x *aw9620x)
{
	struct aw_bin *aw_bin = NULL;
	int32_t ret = -AW_ERR;

	AWLOGD(aw9620x->dev, "enter");

	if (!cont) {
		AWLOGE(aw9620x->dev, "frimware request failed");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "frimware request successfully");
	}

	AWLOGD(aw9620x->dev, "enter, cont->size = %d", (uint32_t)cont->size);

	aw_bin = kzalloc(cont->size + sizeof(struct aw_bin), GFP_KERNEL);
	if (!aw_bin) {
		kfree(aw_bin);
		release_firmware(cont);
		AWLOGE(aw9620x->dev, "failed to allcating memory!");
		return -AW_ERR;
	}

	aw_bin->info.len = cont->size;
	memcpy(aw_bin->info.data, cont->data, cont->size);

	if (cont != NULL) {
		release_firmware(cont);
	}

	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "aw9620x parse bin fail! ret = %d", ret);
		kfree(aw_bin);
		return -AW_ERR;
	}

	AWLOGE(aw9620x->dev, "chip name: %s, len = %d, version:0x%08x",
				aw_bin->header_info[0].chip_type, aw_bin->info.len,  aw_bin->header_info[0].app_version);

	ret = aw9620x_update_boot_to_flash(aw9620x, aw_bin);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "update_firmware err!");
	} else {
		AWLOGD(aw9620x->dev, "update_firmware ok!");
	}

	if (aw_bin != NULL) {
		kfree(aw_bin);
	}

	return ret;
}

static int32_t aw9620x_reg_update_boot_work(struct aw9620x *aw9620x)
{
	int32_t ret = -AW_OK;
	const struct firmware *fw = NULL;

	AWLOGE(aw9620x->dev, "enter");

	snprintf(aw9620x->bt_name, sizeof(aw9620x->bt_name),
				"aw9620x_bt_%d.bin", aw9620x->sar_num);

	AWLOGD(aw9620x->dev, "name :%s", aw9620x->bt_name);

	ret = request_firmware(&fw, aw9620x->bt_name, aw9620x->dev);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "parse %s err!", aw9620x->bt_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "parse %s ok!", aw9620x->bt_name);
	}

	ret = aw9620x_reg_parse_boot(fw, aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "boot %s err!", aw9620x->bt_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "boot %s ok!", aw9620x->bt_name);
	}

	return AW_OK;
}

static int32_t aw9620x_reg_update_fw_to_flash(struct aw9620x *aw9620x,
						struct aw_bin *aw_bin)
{
	uint32_t start_index = aw_bin->header_info[0].valid_data_addr;

	return aw9620x_update_fw_to_flash(aw9620x,
					&(aw_bin->info.data[start_index]),
					aw_bin->header_info[0].valid_data_len);
}

static int32_t aw9620x_update_firmware(struct aw9620x *aw9620x, struct aw_bin *aw_bin)
{

	uint32_t start_index = aw_bin->header_info[0].valid_data_addr;
	int32_t ret = 0;

	AWLOGD(aw9620x->dev, "enter");

	aw9620x->bt_fw_info.fw_bin_version = aw_bin->header_info[0].app_version;

	ret = aw9620x_fw_update(aw9620x,
				 aw9620x->direct_updata_flag,
				 &(aw_bin->info.data[start_index]),
				 aw_bin->header_info[0].valid_data_len,
				 aw_bin->header_info[0].app_version);
	if (ret == -AW_PROT_UPDATE_ERR) {
		ret = aw9620x_reg_update_boot_work(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg_update_boot err");
			return -AW_ERR;
		} else {
			AWLOGD(aw9620x->dev, "reg_update_boot ok");
		}

		ret = aw9620x_reg_update_fw_to_flash(aw9620x, aw_bin);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg_update_fw err");
			return -AW_ERR;
		} else {
			AWLOGD(aw9620x->dev, "reg_update_fw ok");
		}

		aw9620x_soft_reset(aw9620x);
	}

	return AW_OK;
}

static int32_t aw9620x_parse_frimware(const struct firmware *cont, struct aw9620x *aw9620x)
{
	struct aw_bin *aw_bin = NULL;
	int32_t ret = -AW_ERR;

	if (!cont) {
		AWLOGE(aw9620x->dev, "frimware request failed");
		return -AW_ERR;
	} else {
		AWLOGI(aw9620x->dev, "frimware request successfully");
	}

	AWLOGD(aw9620x->dev, "enter, cont->size = %d", (uint32_t)cont->size);

	aw_bin = kzalloc(cont->size + sizeof(struct aw_bin), GFP_KERNEL);
	if (!aw_bin) {
		kfree(aw_bin);
		release_firmware(cont);
		AWLOGE(aw9620x->dev, "failed to allcating memory!");
		return -AW_ERR;
	}

	aw_bin->info.len = cont->size;
	memcpy(aw_bin->info.data, cont->data, cont->size);

	if (cont != NULL) {
		AWLOGE(aw9620x->dev, "release_firmware");
		release_firmware(cont);
	}

	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "aw9620x parse bin fail! ret = %d", ret);
		goto fw_err;
	}

	AWLOGE(aw9620x->dev, "reg chip name: %s, soc chip name: %s, len = %d",
			aw9620x->chip_name, aw_bin->header_info[0].chip_type, aw_bin->info.len);

	if (aw9620x->update_fw_mode == AGREEMENT_UPDATE_FW) {
		ret = aw9620x_update_firmware(aw9620x, aw_bin);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "update_firmware err!");
			goto fw_err;
		} else {
			AWLOGD(aw9620x->dev, "update_firmware ok!");
		}
	} else if (aw9620x->update_fw_mode == REG_UPDATE_FW) {
		ret = aw9620x_reg_update_fw_to_flash(aw9620x, aw_bin);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg update_firmware err!");
			goto fw_err;
		} else {
			AWLOGD(aw9620x->dev, "reg update_firmware ok!");
		}
	}

fw_err:
	if (aw_bin != NULL) {
		kfree(aw_bin);
		aw_bin = NULL;
	}

	return ret;
}

int32_t aw9620x_load_frimware_work(struct aw9620x *aw9620x)
{
	int32_t ret = 0;
	const struct firmware *fw = NULL;

	AWLOGD(aw9620x->dev, "enter");

	snprintf(aw9620x->fw_name, sizeof(aw9620x->fw_name),
				"aw9620x_fw_%d.bin", aw9620x->sar_num);

	AWLOGD(aw9620x->dev, "name :%s", aw9620x->fw_name);

	ret = request_firmware(&fw, aw9620x->fw_name, aw9620x->dev);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "parse %s err!", aw9620x->fw_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "parse %s ok!", aw9620x->fw_name);
	}

	ret = aw9620x_parse_frimware(fw, aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "reg_bin %s load err!", aw9620x->fw_name);
		return -AW_ERR;
	} else {
		AWLOGD(aw9620x->dev, "reg_bin %s load ok!", aw9620x->fw_name);
	}

	return AW_OK;
}

static void aw9620x_update_work(struct work_struct *work)
{
	int32_t ret = -AW_ERR;

	struct aw9620x *aw9620x = container_of(work, struct aw9620x, update_work.work);

	mutex_lock(&aw_lock);

	AWLOGD(aw9620x->dev, "enter");

	//error handling
	if ((aw9620x->init_err_flag == AW_ERR_CHIPID ||
			aw9620x->init_err_flag == AW_ERR_IRQ_INIT)) {
		AWLOGD(aw9620x->dev, "error handling");
		//1.reg_update_boot
		ret = aw9620x_reg_update_boot_work(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg_update_boot err");
			goto update_err;
		} else {
			AWLOGD(aw9620x->dev, "reg_update_boot ok");
		}

		//2.reg update fw
		aw9620x->update_fw_mode = REG_UPDATE_FW;
		ret = aw9620x_load_frimware_work(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "reg frimware load err!");
			goto update_err;
		}

		ret = aw9620x_soft_reset(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "soft_reset err");
			goto update_err;
		}

		//3.Calibration chip
		ret = aw9620x_read_chipid(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "read_chipid err");
			goto update_err;
		}

		ret = aw9620x_read_init_over_irq(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "init_over_irq err");
			goto update_err;
		}

		//4.update def_reg, active chip
		if (aw9620x->arr_update_flag == AW_TURE) {
			aw9620x->arr_update_flag = AW_FALSE;
			aw9620x_para_loaded(aw9620x);
		} else {
			ret = aw9620x_load_def_reg_bin(aw9620x);
			if (ret != AW_OK) {
				AWLOGE(aw9620x->dev, "reg_bin load err!");
				goto update_err;
			}
		}
	} else {
		//1.Agreement update fw
		ret = aw9620x_load_frimware_work(aw9620x);
		if (ret != AW_OK) {
			AWLOGE(aw9620x->dev, "frimware load err!");
		}

		//2.update def_reg, active chip
		if (aw9620x->updata_flag != AW_TURE) {
			if (aw9620x->arr_update_flag == AW_TURE) {
				aw9620x->arr_update_flag = AW_FALSE;
				aw9620x_para_loaded(aw9620x);
			} else {
				ret = aw9620x_load_def_reg_bin(aw9620x);
				if (ret != AW_OK) {
					AWLOGE(aw9620x->dev, "reg_bin load err!");
					goto update_err;
				}
			}

		}
	}

update_err:

	mutex_unlock(&aw_lock);

	return;
}

void aw9620x_update(struct aw9620x *aw9620x)
{
	AWLOGD(aw9620x->dev, "aw9620x update enter");

	INIT_DELAYED_WORK(&aw9620x->update_work, aw9620x_update_work);
	schedule_delayed_work(&aw9620x->update_work,
					msecs_to_jiffies(AW_POWER_ON_SYSFS_DELAY_MS));
}

static ssize_t aw9620x_soft_rst_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	if (flag == SOFT_RST_OK)
		aw9620x_soft_reset(aw9620x);

	return count;
}

static ssize_t aw9620x_mode_operation_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);
	uint32_t mode = 0;
	int32_t ret = 0;
	uint32_t curr_status = 0;
	uint32_t irq_status = 0;

	if (sscanf(buf, "%d", &mode) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	if ((mode == AW9620X_ACTIVE_MODE) &&
			(aw9620x->old_mode != AW9620X_ACTIVE_MODE)) {
		if (aw9620x->old_mode == AW9620X_DEEPSLEEP_MODE) {
			aw9620x_i2c_write(aw9620x, REG_HOSTCTRL1, 1);
		}
		
		if (aw_init_irq_flag [aw9620x->sar_num] == AW_TURE) {
			aw_init_irq_flag [aw9620x->sar_num] = AW_FALSE;
			aw9620x_load_def_reg_bin(aw9620x);
		}
		ret = aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &irq_status);
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "i2c1 IO error");
			return ret;
		}

		ret = aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_ACTIVE_MODE);
		aw9620x_i2c_write_bits(aw9620x, REG_SCANCTRL0,
				AW_SCANCTR_AOTEN_MASK, AW_SCANCTR_AOTEN_EN);
		//enable_irq(aw9620x->to_irq);
	} else if ((mode == AW9620X_SLEEP_MODE) &&
			(aw9620x->old_mode != AW9620X_SLEEP_MODE)) {
		if (aw9620x->old_mode == AW9620X_DEEPSLEEP_MODE) {
			aw9620x_i2c_write(aw9620x, REG_HOSTCTRL1, 1);
		}


		ret = aw9620x_i2c_read(aw9620x, REG_STAT0, &curr_status);
		if (ret < 0) {
			AWLOGE(aw9620x->dev, "i2c IO error");
			return ret;
		}else{
			AWLOGD(aw9620x->dev, "REG_STAT0 = 0x%x!",curr_status);
		}

		if((curr_status & 0XFFFF) != 0) {
			if(aw9620x->sar_num == 0) {
				input_report_key(aw9620x->channels_arr[0].input, KEY_F7, 0);
				AWLOGI(aw9620x->dev, "operation: F7 event report");
			}
			else if(aw9620x->sar_num == 1) {
				input_report_key(aw9620x->channels_arr[0].input, KEY_F8, 0);
				AWLOGI(aw9620x->dev, "operation: F8 event report");
			}
			input_sync(aw9620x->channels_arr[0].input);
		}
		//disable_irq(aw9620x->to_irq);

		aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_SLEEP_MODE);
	} else if ((mode == AW9620X_DEEPSLEEP_MODE) &&
			(aw9620x->old_mode != AW9620X_DEEPSLEEP_MODE)) {
		aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_DEEPSLEEP_MODE);
	} else {
		AWLOGD(aw9620x->dev, " repeat write failed to operation mode!");
	}

	aw9620x->old_mode = mode;
	AWLOGD(aw9620x->dev, "mode = %d!",mode);
	AWLOGD(aw9620x->dev, "aw9620x->old_mode = %d!",aw9620x->old_mode);

	return count;
}

static ssize_t aw9620x_mode_operation_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	uint32_t data = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	aw9620x_i2c_read(aw9620x, REG_WST, &data);

	len = snprintf(buf, PAGE_SIZE, "mode : %d, REG_WST(0x1a14) :0x%x \n",
				aw9620x->old_mode, data);

	return len;
}

static ssize_t aw9620x_update_fw_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	uint32_t firmware_version = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);
	int32_t ret = 0;

	ret = aw9620x_i2c_read(aw9620x, REG_FWVER, &firmware_version);
	if (ret < 0) {
		AWLOGD(aw9620x->dev, "read firmware version err");
	}

	len += snprintf(buf + len, PAGE_SIZE - len,
				"reg firmware_version:0x%08x\n",
				 firmware_version);

	return len;
}

static ssize_t aw9620x_update_fw_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	mutex_lock(&aw_update_lock);

	if (flag == UPDATE_FRIMWARE_OK) {
		aw9620x->update_fw_mode = AGREEMENT_UPDATE_FW;
		aw9620x->direct_updata_flag = AW_TURE;
		if (aw9620x->host_irq_stat == IRQ_ENABLE) {
			disable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_DISABLE;
		}

		aw9620x_load_frimware_work(aw9620x);

		if (aw9620x->host_irq_stat == IRQ_DISABLE) {
			enable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_ENABLE;
		}

	}

	mutex_unlock(&aw_update_lock);

	return count;
}

static ssize_t aw9620x_update_reg_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	mutex_lock(&aw_update_lock);

	if (flag == UPDATE_FRIMWARE_OK) {
		if (aw9620x->host_irq_stat == IRQ_ENABLE) {
			disable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_DISABLE;
		}

		aw9620x_load_def_reg_bin(aw9620x);

		if (aw9620x->host_irq_stat == IRQ_DISABLE) {
			enable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_ENABLE;
		}
	}

	mutex_unlock(&aw_update_lock);

	return count;
}

static ssize_t aw9620x_reg_update_bt_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	mutex_lock(&aw_update_lock);

	if (flag == UPDATE_FRIMWARE_OK) {
		if (aw9620x->host_irq_stat == IRQ_ENABLE) {
			disable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_DISABLE;
		}

		aw9620x_reg_update_boot_work(aw9620x);

		if (aw9620x->host_irq_stat == IRQ_DISABLE) {
			enable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_ENABLE;
		}
	}

	mutex_unlock(&aw_update_lock);

	return count;
}

static ssize_t aw9620x_reg_update_fw_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}

	mutex_lock(&aw_update_lock);

	if (flag == UPDATE_FRIMWARE_OK) {
		aw9620x->update_fw_mode = REG_UPDATE_FW;

		if (aw9620x->host_irq_stat == IRQ_ENABLE) {
			disable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_DISABLE;
		}

		aw9620x_load_frimware_work(aw9620x);

		if (aw9620x->host_irq_stat == IRQ_DISABLE) {
			enable_irq(aw9620x->to_irq);
			aw9620x->host_irq_stat = IRQ_ENABLE;
		}
	}

	mutex_unlock(&aw_update_lock);

	return count;
}

static ssize_t aw9620x_raw_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_RAW_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_lpf_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_LPFDATA_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_valid_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_VALID_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_diff_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_DIFF_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_deltavalid_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_DELTAVALID_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_baseline_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_BASELINE_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_initial_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	uint32_t data = 0;
	int32_t data_tmp = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < 8; i++) {
		aw9620x_i2c_read(aw9620x, REG_INITVALUE_CH0 + i * 84, &data);
		data_tmp = (int32_t)data / 1024;
		len += snprintf(buf + len, PAGE_SIZE - len,
			"(ch%d):%d\n",
			i, data_tmp);
	}

	return len;
}

static ssize_t aw9620x_reg_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint16_t i = 0;
	uint32_t reg_num = ARRAY_SIZE(aw9620x_reg_access);
	uint32_t reg_data = 0;
	int32_t ret = 0;
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < reg_num; i++) {
		if (aw9620x_reg_access[i].rw & REG_RD_ACCESS) {
			ret = aw9620x_i2c_read(aw9620x,
				aw9620x_reg_access[i].reg, &reg_data);
			if (ret < 0) {
				len += snprintf(buf + len,
						PAGE_SIZE - len,
						"i2c read error ret = %d\n", ret);
			}
			len += snprintf(buf + len, PAGE_SIZE - len,
						"0x%04x,0x%08x\n",
						aw9620x_reg_access[i].reg,
						reg_data);
		}
	}

	return len;
}

static ssize_t aw9620x_reg_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint16_t i = 0;
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	uint32_t reg_num = ARRAY_SIZE(aw9620x_reg_access);
	uint32_t databuf[2] = { 0, 0 };
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) != 2)
		return count;

	for (i = 0; i < reg_num; i++) {
		if ((uint16_t)databuf[0] == aw9620x_reg_access[i].reg) {
			if (aw9620x_reg_access[i].rw & REG_WR_ACCESS) {
				aw9620x_i2c_write(aw9620x,
					(uint16_t)databuf[0], (uint32_t)databuf[1]);
			}
			break;
		}
	}

	return count;
}

static ssize_t aw9620x_aot_cail_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	uint32_t cali_flag = 0;
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &cali_flag) != 1)
		return count;

	if (cali_flag == AW_TURE) {
		aw9620x_i2c_write_bits(aw9620x, REG_SCANCTRL0,
					AW_SCANCTR_AOTEN_MASK, AW_SCANCTR_AOTEN_EN);
	} else {
		AWLOGE(aw9620x->dev, "fail to set aot cali");
	}

	return count;
}

static ssize_t aw9620x_parasitic_data_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t i = 0;
	uint32_t reg_data_cfg_val = 0;
	uint32_t reg_data_cfg_tmp0 = 0;
	uint32_t reg_data_cfg_tmp1 = 0;
	uint32_t cfg_int = 0;
	uint32_t cfg_float = 0;
	uint32_t reg_data_cfg = 0;
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	for (i = 0; i < AW_CHANNEL_NUM_MAX; i++) {
		aw9620x_i2c_read(aw9620x, REG_AFECFG1_CH0 + 84 * i, &reg_data_cfg_val);
		AWLOGD(aw9620x->dev, "ch%d addr:0x%08x val=0x%08x", i, REG_AFECFG1_CH0 + 84 * i, reg_data_cfg_val);
		if (((reg_data_cfg_val >> 10) & 0x1) == 0) { //220pf
			reg_data_cfg_tmp0 = ((reg_data_cfg_val >> 16) & 0xff) * AW_FINE_ADJUST_STEP0;
			reg_data_cfg_tmp1 = ((reg_data_cfg_val >> 24) & 0xff) * AW_COARSE_ADJUST_STEP0;
		} else {
			reg_data_cfg_tmp0 = ((reg_data_cfg_val >> 16) & 0xff) * AW_FINE_ADJUST_STEP1;
			reg_data_cfg_tmp1 = ((reg_data_cfg_val >> 24) & 0xff) * AW_COARSE_ADJUST_STEP1;
		}
		reg_data_cfg = reg_data_cfg_tmp0 + reg_data_cfg_tmp1;
		cfg_int = reg_data_cfg / AW_PARA_TIMES;
		cfg_float = reg_data_cfg % AW_PARA_TIMES;

		len += snprintf(buf + len, PAGE_SIZE - len, "ch%d parasitic_data %d.%d pf\n", i, cfg_int, cfg_float);
	}

	return len;
}

static ssize_t aw9620x_chip_info_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	//struct aw9620x *aw9620x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t reg_data = 0;
		struct led_classdev *cdev = dev_get_drvdata(dev);
         struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	len += snprintf(buf + len, PAGE_SIZE - len, "sar%d , driver version %s\n", aw9620x->sar_num, AW9620X_DRIVER_VERSION);

	aw9620x_i2c_read(aw9620x, REG_CHIP_ID, &reg_data);
	len += snprintf(buf + len, PAGE_SIZE - len, "chipid is 0x%08x\n", reg_data);

	aw9620x_i2c_read(aw9620x, REG_FWVER, &aw9620x->bt_fw_info.fw_version);
	len += snprintf(buf + len, PAGE_SIZE - len, "chip firmware ver is 0x%08x\n", aw9620x->bt_fw_info.fw_version);
	len += snprintf(buf + len, PAGE_SIZE - len, "bin  firmware ver is 0x%08x\n", aw9620x->bt_fw_info.fw_bin_version);

	aw9620x_i2c_read(aw9620x, REG_HOSTIRQEN, &reg_data);
	len += snprintf(buf + len, PAGE_SIZE - len, "REG_HOSTIRQEN is 0x%08x\n", reg_data);

	if (aw9620x->use_defaule_reg_arr_flag == AW_TURE) {
		len += snprintf(buf + len, PAGE_SIZE - len, "Error updating register bin file, update with default array\n");
	}

	if (aw9620x->bt_fw_info.update_flag == AW_TURE) {
		len += snprintf(buf + len, PAGE_SIZE - len, "boot ver is 0x%08x, date is 0x%08x, checksum is 0x%08x\n",
				aw9620x->bt_fw_info.bt_version, aw9620x->bt_fw_info.bt_date, aw9620x->bt_fw_info.bt_checksum);
		len += snprintf(buf + len, PAGE_SIZE - len, "fw checksum is 0x%08x\n", aw9620x->bt_fw_info.fw_checksum);
	}

	if (aw9620x->bt_fw_info.bt_bin_version != 0) {
		len += snprintf(buf + len, PAGE_SIZE - len, "boot bin ver is 0x%08x\n",aw9620x->bt_fw_info.bt_bin_version);
	}

	return len;
}

//1:Low sensitivity, 2: Medium sensitivity, 3: high
static ssize_t aw9620x_sensy_config_get(struct device *dev,
						struct device_attribute *attr,char *buf)
{
	ssize_t len = 0;
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",aw9620x->cur_sen);
	return len;
}


//1:Low sensitivity, 2: Medium sensitivity, 3: high
static ssize_t aw9620x_sensy_config_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	uint32_t flag = 0;
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw9620x *aw9620x = container_of(cdev, struct aw9620x, cdev);

	if (sscanf(buf, "%d", &flag) != 1) {
		AWLOGE(aw9620x->dev, "sscanf parse err");
		return count;
	}
   AWLOGD(aw9620x->dev, "flag = %d", flag);
	mutex_lock(&aw_update_lock);

	if (flag == SENSY_LOW) {
		aw9620x_i2c_write(aw9620x, REG_SENSY_CONFIG, REG_SENSY_CONFIG_VAL_LOW);
		aw9620x->cur_sen = SENSY_LOW;
	} else if (flag == SENSY_MEDIUM) {
		aw9620x_i2c_write(aw9620x, REG_SENSY_CONFIG, REG_SENSY_CONFIG_VAL_MED);
		aw9620x->cur_sen = SENSY_MEDIUM;
	} else if (flag == SENSY_HIGH) {
		aw9620x_i2c_write(aw9620x, REG_SENSY_CONFIG, REG_SENSY_CONFIG_VAL_HIGH);
		aw9620x->cur_sen = SENSY_HIGH;
	} else {
		AWLOGE(aw9620x->dev, "input err");
	}

	mutex_unlock(&aw_update_lock);

	return count;
}

static DEVICE_ATTR(awrw, 0664, aw9620x_awrw_get, aw9620x_awrw_set);
static DEVICE_ATTR(soft_rst, 0664, NULL, aw9620x_soft_rst_set);
static DEVICE_ATTR(mode_operation, 0664, aw9620x_mode_operation_get, aw9620x_mode_operation_set);
static DEVICE_ATTR(update_fw, 0664, aw9620x_update_fw_get, aw9620x_update_fw_set);
static DEVICE_ATTR(update_reg, 0664, NULL, aw9620x_update_reg_set);
static DEVICE_ATTR(reg_update_bt, 0664, NULL, aw9620x_reg_update_bt_set);
static DEVICE_ATTR(reg_update_fw, 0664, NULL, aw9620x_reg_update_fw_set);
static DEVICE_ATTR(raw, 0664, aw9620x_raw_get, NULL);
static DEVICE_ATTR(lpf, 0664, aw9620x_lpf_get, NULL);
static DEVICE_ATTR(valid, 0664, aw9620x_valid_get, NULL);
static DEVICE_ATTR(baseline, 0664, aw9620x_baseline_get, NULL);
static DEVICE_ATTR(diff, 0664, aw9620x_diff_get, NULL);
static DEVICE_ATTR(deltavalid, 0664, aw9620x_deltavalid_get, NULL);
static DEVICE_ATTR(initial, 0664, aw9620x_initial_get, NULL);
static DEVICE_ATTR(reg, 0664, aw9620x_reg_get, aw9620x_reg_set);

static DEVICE_ATTR(aot_cail, 0664, NULL, aw9620x_aot_cail_set);
static DEVICE_ATTR(parasitic_data, 0664, aw9620x_parasitic_data_get, NULL);
static DEVICE_ATTR(chip_info, 0664, aw9620x_chip_info_get, NULL);
static DEVICE_ATTR(sensy_config, 0664, aw9620x_sensy_config_get, aw9620x_sensy_config_set);

static struct attribute *aw9620x_touch_attributes[] = {
	&dev_attr_awrw.attr,
	&dev_attr_soft_rst.attr,
	&dev_attr_mode_operation.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_update_reg.attr,
	&dev_attr_reg_update_bt.attr,
	&dev_attr_reg_update_fw.attr,
	&dev_attr_raw.attr,
	&dev_attr_lpf.attr,
	&dev_attr_valid.attr,
	&dev_attr_diff.attr,
	&dev_attr_deltavalid.attr,
	&dev_attr_baseline.attr,
	&dev_attr_initial.attr,
	&dev_attr_reg.attr,
	&dev_attr_aot_cail.attr,
	&dev_attr_parasitic_data.attr,
	&dev_attr_chip_info.attr,
	&dev_attr_sensy_config.attr,
	NULL
};

static struct attribute_group aw9620x_touch_attribute_group = {
	.attrs = aw9620x_touch_attributes
};

static int aw9620x_old_boot_nvr_modify(struct aw9620x *aw9620x)
{
	uint32_t reg_val = 0;
	uint32_t ret = 0;

	AWLOGD(aw9620x->dev, "enter");

	//Judge the starting position
	AWLOGD(aw9620x->dev, "i2c addr0: 0x%x", aw9620x->i2c->addr);
	ret = aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_MCFG00_CPU_HALT);
	if (ret != AW_OK) {
		if (aw9620x->i2c->addr != 0x12) {
			aw9620x->i2c->addr = 0x12;
		}
		AWLOGD(aw9620x->dev, "i2c addr0: 0x%x", aw9620x->i2c->addr);
		ret = aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_MCFG00_CPU_HALT);
		if (ret != AW_OK) {
			AWLOGD(aw9620x->dev, "REG_MCFG00 err!");
			return -AW_ERR;
		}
	}
	aw9620x_i2c_write(aw9620x, REG_APB_ACCESS_EN, REG_APB_ACCESS_EN_ACCESS);

	aw9620x_i2c_read(aw9620x, REG_BOOT_LOADER_ACTIVE, &reg_val);
	AWLOGD(aw9620x->dev, "0x4748 = 0x%08x", reg_val);
	if (reg_val != 0) {
		//Turn off hardware BT mapping mode
		aw9620x_i2c_write(aw9620x, REG_BOOT_LOADER_ACTIVE, REG_BOOT_LOADER_ACTIVE_CLOSE);
		//update nvr
		aw9620x_i2c_write(aw9620x, NVR3_PROTECT, NVR3_PROTECT_OPEN);
		aw9620x_i2c_write(aw9620x, REG_PMU_CFG, REG_SET_PMU_CFG);
		aw9620x_i2c_write(aw9620x, AW_REG_FLASH_WAKE_UP, ISP_CR_OPEN);
		aw9620x_i2c_write(aw9620x, REG_T_ERASE, REG_T_ERASE_OPEN);
		aw9620x_i2c_write(aw9620x, REG_ISP_ADDR, REG_ISP_ADDR_W_VAL);
		aw9620x_i2c_write(aw9620x, REG_ISP_CMD, REG_ISP_CMD_W_VAL);
		aw9620x_i2c_write(aw9620x, REG_T_RCV, REG_SET_T_RCV);
		aw9620x_i2c_write(aw9620x, REG_ISP_GO, REG_SET_ISP_GO);

		msleep(5);

		aw9620x_i2c_write(aw9620x, REG_ISP_ADDR, REG_ISP_ADDR_W_VAL);
		aw9620x_i2c_write(aw9620x, AW_FW_W_DATA_ADDR, AW_FW_W_DATA_ADDR_STA);
		aw9620x_i2c_write(aw9620x, REG_ISP_CMD, REG_ISP_CMD_W_VAL_EN);
		aw9620x_i2c_write(aw9620x, REG_ISP_GO, REG_ISP_GO_W_VAL);

		ret = AW_OLD_BT;
	}else {
		ret = AW_NEW_BT;
		aw9620x_i2c_write(aw9620x, REG_MCFG00, REG_MCFG00_EN);
		aw9620x_i2c_write(aw9620x, REG_RSTNALL, REG_RSTNALL_VAL);
		msleep(AW_POWER_ON_DELAY_MS);
	}

	return ret;
}

static void aw9620x_set_brightness(struct led_classdev *cdev,
           enum led_brightness brightness)
{
	return;
}


static int aw9620x_pinctrl_init(struct aw9620x *aw9620x)
{
	int retval = 0;
    AWLOGD(aw9620x->dev, "%s enter",__func__);
	/* Get pinctrl if target uses pinctrl */
	aw9620x->ts_pinctrl = devm_pinctrl_get(aw9620x->dev);
	if (IS_ERR_OR_NULL(aw9620x->ts_pinctrl)) {
		retval = PTR_ERR(aw9620x->ts_pinctrl);
		AWLOGE(aw9620x->dev,"%s Target does not use pinctrl %d\n", __func__, retval);
		goto err_pinctrl_get;
	}
	if(aw9620x->sar_num == 0){
		aw9620x->pinctrl_state_default
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_default_sar0");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_default)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_default);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_default_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}


		aw9620x->pinctrl_state_active
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_active_sar0");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_active)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_active);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_active_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}

		aw9620x->pinctrl_state_suspend
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_suspend_sar0");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_suspend)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_suspend);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_suspend_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}
	}
	else if(aw9620x->sar_num == 1){
		aw9620x->pinctrl_state_default
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_default_sar1");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_default)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_default);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_default_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}


		aw9620x->pinctrl_state_active
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_active_sar1");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_active)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_active);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_active_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}

		aw9620x->pinctrl_state_suspend
			= pinctrl_lookup_state(aw9620x->ts_pinctrl, "aw_int_suspend_sar1");
		if (IS_ERR_OR_NULL(aw9620x->pinctrl_state_suspend)) {
			retval = PTR_ERR(aw9620x->pinctrl_state_suspend);
			AWLOGE(aw9620x->dev,"%s Can not lookup aw_int_suspend_sar%d pinstate %d\n", __func__,aw9620x->sar_num, retval);
			goto err_pinctrl_lookup;
		}

	}

	return retval;

err_pinctrl_lookup:
	devm_pinctrl_put(aw9620x->ts_pinctrl);
err_pinctrl_get:
	aw9620x->ts_pinctrl = NULL;
	return retval;
}



static int aw9620x_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct aw9620x *aw9620x = NULL;
	int32_t ret = -1;
	uint8_t i = 0;
	uint8_t err_num = 0;

	pr_info("%s enter", __func__);

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		AWLOGE(&i2c->dev, "check_functionality failed");
		return -EIO;
	}

	aw9620x = devm_kzalloc(&i2c->dev, sizeof(struct aw9620x), GFP_KERNEL);
	if (aw9620x == NULL) {
		AWLOGE(&i2c->dev, "failed to malloc memory!");
		ret = -AW_ERR;
		goto err_malloc;
	}

	aw9620x->dev = &i2c->dev;
	aw9620x->i2c = i2c;
	i2c_set_clientdata(i2c, aw9620x);
	mutex_init(&aw_lock);
	mutex_init(&aw_update_lock);

	//1.parse dts
	ret = aw9620x_parse_dt(&i2c->dev, aw9620x, i2c->dev.of_node);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "irq gpio error!, ret = %d", ret);
		goto err_pase_dt;
	}

	ret = aw9620x_pinctrl_init(aw9620x);
	if (!ret && aw9620x->ts_pinctrl) {
		ret = pinctrl_select_state(
				aw9620x->ts_pinctrl,
				aw9620x->pinctrl_state_active);
		if (ret < 0) {
			AWLOGE(aw9620x->dev,"%s: Failed to select aw_int_active_sar%d pinstate %d\n", __func__,aw9620x->sar_num, ret);
		}else{
			AWLOGD(aw9620x->dev,"%s: Successed to select aw_int_active_sar%d pinstate %d\n", __func__,aw9620x->sar_num, ret);
		}
	}


#ifdef AW_POWER_ON
	ret = aw9620x_power_init(aw9620x);
	if (ret) {
		AWLOGE(&i2c->dev, "aw9620x power init failed");
		goto err_get_voltage;
	}
	aw9620x_power_enable(aw9620x, AW_TURE);

	ret = regulator_is_get_voltage(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "get_voltage failed");
		goto err_get_voltage;
	}
#endif

	//2.power on dalay 25ms
	msleep(AW_POWER_ON_DELAY_MS);

	if (aw9620x->sar_num == 0){
		aw9620x->cdev.name = "sar0";
	} else {
		aw9620x->cdev.name = "sar1";
	}

	aw9620x->cdev.brightness_set = aw9620x_set_brightness;

	ret = devm_led_classdev_register(aw9620x->dev, &aw9620x->cdev);
	if (ret < 0) {
		dev_err(aw9620x->dev,
			"unable to register led ret=%d\n", ret);
		return ret;
	}
	//3.Create file node
	ret = sysfs_create_group(&aw9620x->cdev.dev->kobj,
						&aw9620x_touch_attribute_group);
	if (ret < 0) {
		AWLOGE(aw9620x->dev, "error creating sysfs attr files");
		goto err_sysfs;
	}

#ifdef AW_USE_IRQ_FLAG
	//4.input、irq init
	ret = aw9620x_input_init(aw9620x, &err_num);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "error inputs init");
		goto err_input_init;
	}

	ret = aw9620x_irq_init(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "error irq init");
		goto err_irq_init;
	}
#endif

	ret = aw9620x_old_boot_nvr_modify(aw9620x);
	if (ret == AW_OLD_BT) {
		AWLOGE(aw9620x->dev, "old boot");
		aw9620x->init_err_flag = AW_ERR_CHIPID;
		aw9620x-> old_bt_flag = true;
		goto update_chip;
	} else if (ret == -AW_ERR){
		AWLOGE(aw9620x->dev, "i2c err!");
		goto err_chipid;
	} else {
		AWLOGE(aw9620x->dev, "new boot");
	}

	//5.read chip id
	ret = aw9620x_read_chipid(aw9620x);
	if (ret == -AW_ERR) {
		AWLOGE(aw9620x->dev, "read chipid failed, ret=%d, ", ret);
		goto err_chipid;
	} else if (ret == -AW_ERR_CHIPID) {
		AWLOGE(aw9620x->dev, "read chipid failed, ret=%d, ", ret);
		aw9620x->init_err_flag = AW_ERR_CHIPID;
		goto update_chip;
	} else {
		AWLOGE(aw9620x->dev, "read chipid ok!");
	}

	//6.soft reset
	ret = aw9620x_soft_reset(aw9620x);
	if (ret != AW_OK) {
		AWLOGE(aw9620x->dev, "soft reset failed, ret=%d", ret);
		goto err_soft_reset;
	}

	//7.read init over irq stat
	ret = aw9620x_read_init_over_irq(aw9620x);
	if (ret == -AW_ERR) {
		AWLOGE(aw9620x->dev, "read chipid failed");
		goto err_init_over_irq;
	} else if (ret == -AW_ERR_IRQ_INIT) {
		AWLOGE(aw9620x->dev, "_read_init_over_irq, ret=%d, ", ret);
		aw9620x->init_err_flag = AW_ERR_CHIPID;
		goto update_chip;
	} else {
		AWLOGE(aw9620x->dev, "read_init_over_irq ok!");
	}

	//8.update firmware
	aw9620x->updata_flag = AW_FALSE;
	aw9620x->update_fw_mode = AGREEMENT_UPDATE_FW;

update_chip:
	aw9620x_update(aw9620x);
	aw9620x->cur_sen = SENSY_MEDIUM;
	return 0;

err_init_over_irq:
err_soft_reset:
err_chipid:
#ifdef AW_USE_IRQ_FLAG
err_irq_init:
	/*if (gpio_is_valid(aw9620x->irq_gpio))
		devm_gpio_free(&i2c->dev, aw9620x->irq_gpio);*/
err_input_init:
	for (i = 0; i < err_num; i++) {
		input_unregister_device(aw9620x->channels_arr[i].input);
	}
	for (i = 0; i < AW_CHANNEL_NUM_MAX; i++) {
		if (aw9620x->channels_arr[i].input != NULL)
			input_free_device(aw9620x->channels_arr[i].input);
	}
#endif
err_sysfs:
	sysfs_remove_group(&i2c->dev.kobj, &aw9620x_touch_attribute_group);
#ifdef AW_POWER_ON
err_get_voltage:
	if (aw9620x->power_enable) {
		regulator_disable(aw9620x->vcc);
		regulator_put(aw9620x->vcc);
	}
#endif
err_pase_dt:
err_malloc:
	return ret;

}

static void aw9620x_i2c_remove(struct i2c_client *i2c)
{
	uint32_t i = 0;
	struct aw9620x *aw9620x = i2c_get_clientdata(i2c);

	/*if (gpio_is_valid(aw9620x->irq_gpio))
		devm_gpio_free(&i2c->dev, aw9620x->irq_gpio);*/

	for (i = 0; i < AW_CHANNEL_NUM_MAX; i++) {
		input_unregister_device(aw9620x->channels_arr[i].input);
		if (aw9620x->channels_arr[i].input != NULL)
			input_free_device(aw9620x->channels_arr[i].input);
	}

	/*if (gpio_is_valid(aw9620x->irq_gpio))
			devm_gpio_free(&i2c->dev, aw9620x->irq_gpio);*/

	sysfs_remove_group(&i2c->dev.kobj, &aw9620x_touch_attribute_group);

	if (aw9620x->power_enable) {
			regulator_disable(aw9620x->vcc);
			regulator_put(aw9620x->vcc);
	}

	//return 0;
}

static void aw9620x_i2c_shutdown(struct i2c_client *i2c)
{
	struct aw9620x *aw9620x = i2c_get_clientdata(i2c);
	uint32_t reg_val = 0;

	pr_info("%s enter", __func__);

	if (aw9620x->host_irq_stat == IRQ_ENABLE) {
		disable_irq(aw9620x->to_irq);
		aw9620x->host_irq_stat = IRQ_DISABLE;
	}
	aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &reg_val);

	aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_SLEEP_MODE);
}

#ifdef	PM_OPS_USE
static int aw9620x_suspend(struct device *dev)
{
	uint32_t reg_val = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct aw9620x *aw9620x = i2c_get_clientdata(client);

	AWLOGD(aw9620x->dev, "suspend enter");

	if (aw9620x->host_irq_stat == IRQ_ENABLE) {
		disable_irq(aw9620x->to_irq);
		aw9620x->host_irq_stat = IRQ_DISABLE;
	}
	aw9620x_i2c_read(aw9620x, REG_HOSTIRQSRC, &reg_val);

	aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_SLEEP_MODE);

	return 0;
}

static int aw9620x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw9620x *aw9620x = i2c_get_clientdata(client);

	AWLOGD(aw9620x->dev, "resume enter");

	if (aw9620x->host_irq_stat == IRQ_DISABLE) {
		enable_irq(aw9620x->to_irq);
		aw9620x->host_irq_stat = IRQ_ENABLE;
	}

	aw9620x_i2c_write(aw9620x, REG_CMD, AW9620X_ACTIVE_MODE);

	return 0;
}

static const struct dev_pm_ops aw9620x_pm_ops = {
	.suspend = aw9620x_suspend,
	.resume = aw9620x_resume,
};
#endif

static const struct of_device_id aw9620x_dt_match[] = {
	{ .compatible = "awinic,aw9620x_sar_0" },
	{ .compatible = "awinic,aw9620x_sar_1" },
	{ },
};

static const struct i2c_device_id aw9620x_i2c_id[] = {
	{ AW9620X_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9620x_i2c_id);

static struct i2c_driver aw9620x_i2c_driver = {
	.driver = {
		.name = AW9620X_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw9620x_dt_match),
#ifdef	PM_OPS_USE
		//.pm = &aw9620x_pm_ops,
#endif
	},
	.probe = aw9620x_i2c_probe,
	.remove = aw9620x_i2c_remove,
	.shutdown = aw9620x_i2c_shutdown,
	.id_table = aw9620x_i2c_id,
};

static int __init aw9620x_i2c_init(void)
{
	int32_t ret = 0;

	pr_info("aw9620x driver version %s\n", AW9620X_DRIVER_VERSION);

	ret = i2c_add_driver(&aw9620x_i2c_driver);
	if (ret) {
		pr_err("fail to add aw9620x device into i2c\n");
		return ret;
	}

	return 0;
}

late_initcall(aw9620x_i2c_init);
static void __exit aw9620x_i2c_exit(void)
{
	i2c_del_driver(&aw9620x_i2c_driver);
}
module_exit(aw9620x_i2c_exit);
MODULE_DESCRIPTION("aw9620x SAR Driver");

MODULE_LICENSE("GPL v2");
