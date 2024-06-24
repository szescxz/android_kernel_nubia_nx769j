/*
* ak09973.c
*
* Version:
*
* Copyright (c) 2021 ZTE
*
* Author:
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *****************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/fcntl.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#define WAKELOCK_HOLD_TIME 1000 /* in ms*/
#define RECHECK_FAR_DELAY 220 /* in ms */
#define CASE_RECHECK_DELAY 2000 /* in ms */

#define CHIP_POWER_ON
//#define INPUT_EVENT_SUPPORTED
#define KEY_UEVENT_SUPPORTED
//#define RA9530_WLS_USED

//#define GET_HALL_STATUS_EN
#ifdef GET_HALL_STATUS_EN
//#define FAR_RECHECK_SUPPORTED
//#define CONFIG_ZTE_DOCK_KEYBOARD
#ifdef CONFIG_ZTE_DOCK_KEYBOARD
//#define CASE_RECHECK_SUPPORTED
#endif
#endif

static struct ak09973_data *pdata;

#ifdef ZTE_CMB_QC_BASE_MODULE_SEQUENCE
extern void sqc_sequence_load_init(int (*func_handle)(struct work_struct *work), unsigned char num, bool force_exit);
#endif

#ifdef RA9530_WLS_USED
extern int enable_ra9530_wls_tx(bool enable, bool pen_connect);
#endif

#ifdef CONFIG_ZTE_DOCK_KEYBOARD
extern bool zte_case_type;
#endif

#ifdef SAR_MANUAL_CALI_EN
extern void aw9610x_manual_calibrate(void);
#endif

#define akdbgprt printk

/*
 * Register definitions, as well as various shifts and masks to get at the
 * individual fields of the registers.
 */
// REGISTER MAP
#define AK09973_REG_WIA			0x00
#define AK09973_DEVICE_ID		0x48C1

#define AK09973_REG_WORD_ST             0x10
#define AK09973_REG_WORD_ST_X           0x11
#define AK09973_REG_WORD_ST_Y           0x12
#define AK09973_REG_WORD_ST_Y_X         0x13
#define AK09973_REG_WORD_ST_Z           0x14
#define AK09973_REG_WORD_ST_Z_X         0x15
#define AK09973_REG_WORD_ST_Z_Y         0x16
#define AK09973_REG_WORD_ST_Z_Y_X       0x17
#define AK09973_REG_BYTE_ST_V           0x18
#define AK09973_REG_BYTE_ST_X           0x19
#define AK09973_REG_BYTE_ST_Y           0x1A
#define AK09973_REG_BYTE_ST_Y_X         0x1B
#define AK09973_REG_BYTE_ST_Z           0x1C
#define AK09973_REG_BYTE_ST_Z_X         0x1D
#define AK09973_REG_BYTE_ST_Z_Y         0x1E
#define AK09973_REG_BYTE_ST_Z_Y_X       0x1F
#define AK09973_REG_CNTL1               0x20
#define AK09973_REG_CNTL2               0x21
#define AK09973_REG_THX                 0x22
#define AK09973_REG_THY                 0x23
#define AK09973_REG_THZ                 0x24
#define AK09973_REG_THV                 0x25
#define AK09973_REG_SRST                0x30

#define AK09973_MAX_REGS      AK09973_REG_SRST

#define AK09973_MEASUREMENT_WAIT_TIME   2

#define STATUS_NEAR						0
#define STATUS_FAR						1

#ifdef KEY_UEVENT_SUPPORTED
#define TOUCH_PEN_ON "touch_pen_event_on=true"  // touch pen detected
#define TOUCH_PEN_OFF "touch_pen_event_off=true"  // touch pen not detected
#endif

static int modeBitTable[] = {
	0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE, 0x10
};
// 0 : Power Down 
// 1 : Measurement Mode 1
// 2 : Measurement Mode 2
// 3 : Measurement Mode 3
// 4 : Measurement Mode 4
// 5 : Measurement Mode 5
// 6 : Measurement Mode 6
// 7 : Measurement Mode 7

static int measurementFreqTable[] = {
	0,     5,    10,    20,    50,   100,    500,    1000,   2000
//	0.0,  5Hz,  10Hz,  20Hz,  50Hz,  100Hz,  500Hz,  1000Hz, 2000
};

enum {
	AK09973_MSRNO_WORD_ST = 0,
	AK09973_MSRNO_WORD_ST_X,     // 1
	AK09973_MSRNO_WORD_ST_Y,     // 2
	AK09973_MSRNO_WORD_ST_Y_X,   // 3
	AK09973_MSRNO_WORD_ST_Z,     // 4
	AK09973_MSRNO_WORD_ST_Z_X,   // 5
	AK09973_MSRNO_WORD_ST_Z_Y,   // 6
	AK09973_MSRNO_WORD_ST_Z_Y_X, // 7
	AK09973_MSRNO_WORD_ST_V,     // 8
	AK09973_MSRNO_BYTE_ST_X,     // 9
	AK09973_MSRNO_BYTE_ST_Y,     // 10
	AK09973_MSRNO_BYTE_ST_Y_X,   // 11
	AK09973_MSRNO_BYTE_ST_Z,     // 12
	AK09973_MSRNO_BYTE_ST_Z_X,   // 13
	AK09973_MSRNO_BYTE_ST_Z_Y,   // 14
	AK09973_MSRNO_BYTE_ST_Z_Y_X, // 15
};

static int msrDataBytesTable[] = {
	1,  // AK09973_REG_WORD_ST
	3,  // AK09973_REG_WORD_ST_X
	3,  // AK09973_REG_WORD_ST_Y
	5,  // AK09973_REG_WORD_ST_Y_X
	3,  // AK09973_REG_WORD_ST_Z
	5,  // AK09973_REG_WORD_ST_Z_X
	5,  // AK09973_REG_WORD_ST_Z_Y
	7,  // AK09973_REG_WORD_ST_Z_Y_X
	5,  // AK09973_REG_WORD_ST_V
	2,  // AK09973_REG_BYTE_ST_X
	2,  // AK09973_REG_BYTE_ST_Y
	3,  // AK09973_REG_BYTE_ST_Y_X
	2,  // AK09973_REG_BYTE_ST_Z
	3,  // AK09973_REG_BYTE_ST_Z_X
	3,  // AK09973_REG_BYTE_ST_Z_Y
	4,  // AK09973_REG_BYTE_ST_Z_Y_X
};

/*
 * Per-instance context data for the device.
 */
struct ak09973_data {
	struct i2c_client	*client;
	struct device *dev;
#ifdef INPUT_EVENT_SUPPORTED
	struct input_dev *input;
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	struct wakeup_source *hall_wakelock;
#else
	struct wake_lock hall_wakelock;
#endif

#ifdef FAR_RECHECK_SUPPORTED
	struct delayed_work dworker;
#endif
	struct delayed_work irq_dworker;
#ifdef CASE_RECHECK_SUPPORTED
	struct delayed_work case_recheck_dworker;
#endif

#ifdef CHIP_POWER_ON
	struct regulator *vdd;
#endif

	bool is_suspend;
	int is_hall_open;

	int  int_gpio; 
	int	 irq;

	int status;
	int pre_status;

	int bopv;
	int brpv;

	u8  mode;
	s16 numMode;

	u8 msrNo;

	u8 DRDYENbit;
	u8 SWXENbit;
	u8 SWYENbit;
	u8 SWZENbit;
	u8 SWVENbit;
	u8 ERRENbit;

	u8 POLXbit;
	u8 POLYbit;
	u8 POLZbit;
	u8 POLVbit;

	u8 SDRbit;
	u8 SMRbit;

	u16 BOPXbits;
	u16 BRPXbits;

	u16 BOPYbits;
	u16 BRPYbits;

	u16 BOPZbits;
	u16 BRPZbits;

	u16 BOPVbits;
	u16 BRPVbits;
	u16 BOPV2bits;
	u16 BRPV2bits;
};

// ****************** Register R/W  ************************************
static int ak09973_i2c_read(struct i2c_client *client, u8 address)
{
	int ret;
	ret = i2c_smbus_read_byte_data(client, address);
	if (ret < 0) {
		dev_err(&client->dev, "[AK09973] I2C Read Error\n");
	}
	return ret;
}

static int ak09973_i2c_reads(struct i2c_client *client, u8 *reg,
					int reglen, u8 *rdata, int datalen)
{
	struct i2c_msg xfer[2];
	int ret;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = reglen;
	xfer[0].buf = reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = datalen;
	xfer[1].buf = rdata;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret == 2)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int ak09973_i2c_read8(struct i2c_client *client,
						u8 address, int rLen, u8 *rdata)
{
	u8  tx[1];
	int i, ret;

	if (rLen > 8) {
		dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, rLen);
		return -EINVAL;
	}

	tx[0] = address;
	ret = ak09973_i2c_reads(client, tx, 1, rdata, rLen);
	if (ret < 0) {
		dev_err(&client->dev, "[AK09973] I2C Read Error\n");
		for (i = 0 ; i < rLen ; i ++) {
			rdata[i] = 0;
		}
	}
	return ret;
}

static int ak09973_i2c_read16(struct i2c_client *client,
								u8 address, int wordLen, u16 *rdata)
{
	u8  tx[1];
	u8  rx[8];
	int i, ret;

	if (wordLen > 4) {
		dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, wordLen);
		return -EINVAL;
	}

	tx[0] = address;
	ret = ak09973_i2c_reads(client, tx, 1, rx, (2 * wordLen));
	if (ret < 0) {
		dev_err(&client->dev, "[AK09973] I2C Read Error %d\n", ret);
	} else {
		//akdbgprt("[AK09973] %s rx[0]=%02X rx[1]=%02X\n",__func__, (int)rx[0], (int)rx[1]);
		for (i = 0 ; i < wordLen ; i ++) {
			rdata[i] = ((u16)rx[2*i] << 8) + (u16)rx[2*i + 1];
		}
	}
	return(ret);
}

// for read measurement data
// 1st  read data : Stat 1byte
// 2nd- read data : Stat 2byte
static int ak09973_i2c_read8_16(struct i2c_client *client,
							u8 address, int wordLen, u16 *rdata)
{
	u8  tx[1];
	u8  rx[8];
	int i, ret;

	if ((wordLen < 1) || (wordLen > 4)) {
		dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, wordLen);
		return -EINVAL;
	}

	tx[0] = address;
	ret = ak09973_i2c_reads(client, tx, 1, rx, ((2 * wordLen) - 1));
	if (ret < 0) {
		dev_err(&client->dev, "[AK09973] I2C Read Error\n");
	} else {
		rdata[0] = (u16)rx[0]; 
		for (i = 1; i < wordLen; i++) {
			rdata[i] = ((u16)rx[2*i-1] << 8) + (u16)rx[2*i];
		}
	}

	return(ret);
}

static int ak09973_i2c_writes(struct i2c_client *client,
							const u8 *tx, size_t wlen)
{
	int ret;

	ret = i2c_master_send(client, tx, wlen);
	if(ret != wlen) {
		pr_err("%s: comm error, ret %d, wlen %d\n", __func__, ret, (int)wlen);
	}

	return ret;
}

static int ak09973_i2c_write8(struct i2c_client *client,
											u8 address, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, address, value);
	if (ret < 0) {
		pr_err("%s: comm error, ret= %d\n", __func__, ret);
		//dbg_show(__func__);
	}

	return ret;
}

static s32 ak09973_i2c_write16(struct i2c_client *client, 
				u8 address, int valueNum, u16 value1, u16 value2)
{
	u8  tx[5];
	s32 ret;
	int n;

	if (( valueNum != 1 ) &&  ( valueNum != 2 )) {
		pr_err("%s: valueNum error, valueNum= %d\n", __func__, valueNum);
		return -EINVAL;
	}

	n = 0;
	tx[n++] = address;
	tx[n++] = (u8)((0xFF00 & value1) >> 8);
	tx[n++] = (u8)(0xFF & value1);

	if (valueNum == 2) {
		tx[n++] = (u8)((0xFF00 & value2) >> 8);
		tx[n++] = (u8)(0xFF & value2);
	}

	ret = ak09973_i2c_writes(client, tx, n);
	return ret;
}

//*********************************************************************
static ssize_t ak09973_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09973_data *akm = dev_get_drvdata(dev);
	u8  cntl2[1], stat[1];
	u16 rdata[2];
	s16 msrData[12];
	int i, j, n;
	int ret;
	ssize_t len = 0;

	ret = ak09973_i2c_read8(akm->client, AK09973_REG_WORD_ST, 1, stat);
	if (ret < 0)
		return ret;
	len += snprintf(buf + len, PAGE_SIZE - len, "ST(10H)=%02XH\n", (int)stat[0]);

	ak09973_i2c_read16(akm->client, AK09973_REG_CNTL1, 1, rdata);
	len += snprintf(buf + len, PAGE_SIZE - len, "CNTL1(20H)=%04XH\n", (int)rdata[0]);

	ak09973_i2c_read8(akm->client, AK09973_REG_CNTL2, 1, cntl2);
	len += snprintf(buf + len, PAGE_SIZE - len, "CNTL2(21H)=%02XH\n", (int)cntl2[0]);

	n = 0;
	for (i = AK09973_REG_THX; i <= AK09973_REG_THV; i++) {
		ak09973_i2c_read16(akm->client, i, 2, rdata);
		for (j = 0; j < 2; j++) {
			if (rdata[j] < 32768) {
				msrData[n] = rdata[j];
			} else {
				msrData[n] = -((s16)(~rdata[j]) + 1);
			}
			n++;
		}
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "(22H) BOPX=%d, BRPX=%d\n", (int)msrData[0], (int)msrData[1]);
	len += snprintf(buf + len, PAGE_SIZE - len, "(23H) BOPY=%d, BRPY=%d\n", (int)msrData[2], (int)msrData[3]);
	len += snprintf(buf + len, PAGE_SIZE - len, "(24H) BOPZ=%d, BRPZ=%d\n", (int)msrData[4], (int)msrData[5]);
	len += snprintf(buf + len, PAGE_SIZE - len, "(25H) BOPV=%d, BRPV=%d\n", (int)msrData[6], (int)msrData[7]);

	return len;
}

static ssize_t ak09973_reg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ak09973_data *akm = dev_get_drvdata(dev);
	char *ptr_data = (char *)buf;
	char *p;
	int  pt_count = 0;
	long val[3];
	unsigned char wdata[10];
	int n;
	int ret;

	dev_dbg(dev, "[AK09973] %s called: '%s'(%zu)", __func__, buf, count);

	if (buf == NULL)
		return -EINVAL;

	if (count == 0)
		return 0;

	for (n = 0 ; n < 3 ; n ++) {
		val[n] = 0;
	}
	while ((p = strsep(&ptr_data, ","))) {
		if (!*p)
			break;

		if (pt_count >= 3)
			break;

		if ((pt_count != 0) && (val[0] > 0x21)) {
			val[pt_count] = simple_strtol(p, NULL, 10);
		} else {
			val[pt_count] = simple_strtol(p, NULL, 16);
		}

		pt_count++;
	}

	if ((pt_count < 1) ||
			(((val[0] >= 0x20) && (val[0] != 0x21)) && (pt_count < 3))) {
		dev_err(dev, "[AK09973] %s pt_count = %d, Error", __func__, pt_count);
		return -EINVAL;
	}

	n = 0;
	wdata[n++] = val[0];
	ret = 0;

	if ((val[0] >= 0x20) && (pt_count > 1)) {
		switch(val[0]) {
			case 0x20:
				wdata[n++] = (unsigned char)(0xFF & val[1]);
				wdata[n++] = (unsigned char)(0xFF & val[2]);
				break;
			case 0x21:
				wdata[n++] = (unsigned char)(0xFF & val[1]);
				break;
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
				if (val[1] < 0) val[1] +=  65536;
 				wdata[n++] = (unsigned char)((0xFF00 & val[1]) >> 8);
				wdata[n++] = (unsigned char)(0xFF & val[1]);
				if (val[2] < 0) val[2] +=  65536;
 				wdata[n++] = (unsigned char)((0xFF00 & val[2]) >> 8);
				wdata[n++] = (unsigned char)(0xFF & val[2]);
				break;
			default:
				dev_err(dev, "[AK09973] %s Address Error", __func__);
				return -EINVAL;
				break;
		}
		ret = ak09973_i2c_writes(akm->client, wdata, n);
	} else if ((val[0] >= AK09973_REG_WORD_ST) 
							&& (val[0] <= AK09973_REG_BYTE_ST_Z_Y_X)) {
		akm->msrNo = val[0] - AK09973_REG_WORD_ST;
		akdbgprt("[AK09973] %smeasurement No = %d, address=%XH\n",
					__func__, akm->msrNo, (int)val[0]);
	}

	if (ret < 0)
		return ret;

	return count;
}

static int ak09973_write_mode(struct ak09973_data *akm, int modeBit)
{
	int mode;
	int ret;

	mode = (akm->SMRbit << 6) + (akm->SDRbit << 5) + modeBit;
	akdbgprt("[AK09973] %s Reg[%d] = %d mode = %d freq = %d\n", __func__, AK09973_REG_CNTL2, mode, akm->mode, measurementFreqTable[modeBit]);
	ret = ak09973_i2c_write8(akm->client, AK09973_REG_CNTL2, mode);
	return ret;
}

static int ak09973_set_mode(struct ak09973_data *akm, int freq)
{
	int n;

	if (freq < 0) {
		dev_err(&akm->client->dev, "[AK09973] %s freq = %d Error\n", __func__, freq);
		return -EINVAL;
	}

	n = 0;
	while ((freq > measurementFreqTable[n]) && (n < (akm->numMode - 1))) {
		n++;
	};

	akm->mode = n;
	ak09973_write_mode(akm, modeBitTable[akm->mode]);

	akdbgprt("[AK09973] %s freq = %d mode = %d\n", __func__, freq, akm->mode);
	return 0;
}

static ssize_t ak09973_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ak09973_data *akm = dev_get_drvdata(dev);
	int freq;

	if (sscanf(buf, "%d", &freq) != 1) {
		pr_err("%s - the number of data are wrong\n", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "[AK09973] %s freq=%d enter.", __func__, freq);
	ak09973_set_mode(akm, freq); // sample_frequency set

	return count;
}

static ssize_t ak09973_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ak09973_data *akm = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "mode=%d freq=%d\n", akm->mode, measurementFreqTable[akm->mode]);
}

static ssize_t ak09973_raw_data_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ak09973_data *akm = dev_get_drvdata(dev);
	ssize_t len = 0;
	int msrDataAddr, index, v_value;
	u16 readValue[4], wordLen;

	readValue[0] = ak09973_i2c_read(akm->client, AK09973_REG_CNTL2);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"[AK09973] freq=%d mode=%d MODEbits=%X\n", measurementFreqTable[akm->mode], akm->mode, readValue[0]);

	if (akm->mode == 0) {
		ak09973_write_mode(akm, 1); // single measurement mode
		msleep(AK09973_MEASUREMENT_WAIT_TIME);
	}

	index = AK09973_MSRNO_WORD_ST_Z_Y_X;
	msrDataAddr = index + AK09973_REG_WORD_ST;
	wordLen = (msrDataBytesTable[index] + 1) / 2;
	ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"[AK09973] ST value 0x%x\n", readValue[0]);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"[AK09973] X Y Z value %d %d %d\n", (s16)readValue[3], (s16)readValue[2], (s16)readValue[1]);

	index = AK09973_MSRNO_WORD_ST_V;
	msrDataAddr = index + AK09973_REG_WORD_ST;
	wordLen = (msrDataBytesTable[index] + 1) / 2;
	ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
	v_value = readValue[1];
	v_value = (v_value << 16) | readValue[2];
	len += snprintf(buf + len, PAGE_SIZE - len, "[AK09973] V value %d\n", v_value);

	if (akm->mode == 0) {
		ak09973_write_mode(akm, 0);
	}

	return len;
}

static ssize_t ak09973_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ak09973_data *akm = pdata;
	return snprintf(buf, PAGE_SIZE, "%d\n", akm->status);
}

static DEVICE_ATTR(reg, 0664, ak09973_reg_show, ak09973_reg_store);
static DEVICE_ATTR(mode, 0644, ak09973_mode_show, ak09973_mode_store);
static DEVICE_ATTR(raw_data, 0664, ak09973_raw_data_show, NULL);
static DEVICE_ATTR(status, 0664, ak09973_status_show, NULL);

static struct attribute *ak09973_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_mode.attr,
	&dev_attr_raw_data.attr,
	&dev_attr_status.attr,
	NULL
};

static struct attribute_group ak09973_attribute_group = {
	.attrs = ak09973_attributes
};

static int ak09973_check_chip_id(struct i2c_client *client)
{
	u16  deviceID[1];
	s32 err;
	akdbgprt("[AK09973] %s enter.\n", __func__);

	//ak09973_i2c_write8(client, AK09973_REG_SRST, 1);

	err = ak09973_i2c_read16(client, AK09973_REG_WIA, 1, deviceID);
	if (err < 0) {
		pr_err("[AK09973]i2c read failure, AK09973_REG_WIA err=%d\n", err);
		return -EIO;
	}
	
	if (deviceID[0] != AK09973_DEVICE_ID) {
		pr_err("[AK09973] Device ID Error, Device ID =0x%X\n", deviceID[0]);
		return -EINVAL;
	}

	printk("[AK09973] Device ID = %X. AK09973 check success!\n", deviceID[0]);
	return 0;
}

// ********************************************************************
static int ak09973_setup(struct i2c_client *client)
{
	struct ak09973_data *akm = i2c_get_clientdata(client);
	s32 err;
	u8   value;
	u16  value1;

	akdbgprt("[AK09973] %s enter\n", __func__);

	value1 = (u16)(akm->POLXbit + (akm->POLYbit << 1) 
					+ (akm->POLZbit << 2) + (akm->POLVbit << 3));
	value1 <<= 8;

	value1 += ((u16)(akm->DRDYENbit + (akm->SWXENbit << 1) 
			   + (akm->SWYENbit << 2) + (akm->SWZENbit << 3) 
                 + (akm->SWVENbit << 4) + (akm->ERRENbit << 5))); 

	err = ak09973_i2c_write16(client, AK09973_REG_CNTL1, 1, value1, 0);

	err = ak09973_i2c_write16(client, AK09973_REG_THX, 2,
								akm->BOPXbits, akm->BRPXbits);
	err = ak09973_i2c_write16(client, AK09973_REG_THY, 2,
								akm->BOPYbits, akm->BRPYbits);
	err = ak09973_i2c_write16(client, AK09973_REG_THZ, 2,
								akm->BOPZbits, akm->BRPZbits);
	err = ak09973_i2c_write16(client, AK09973_REG_THV, 2,
								akm->BOPVbits, akm->BRPVbits);

	akm->bopv = akm->BOPVbits * akm->BOPVbits;
	akm->brpv = akm->BRPVbits * akm->BRPVbits;

	if (akm->mode < ARRAY_SIZE(modeBitTable)) {
		value = modeBitTable[akm->mode];
	} else {
		value = modeBitTable[1];
	}

	err = ak09973_write_mode(akm, value);
	return 0;
}

#ifdef GET_HALL_STATUS_EN
void ak09973_set_BOPVbits(void)
{
	struct ak09973_data *akm = pdata;
	int i = 0;

	if (akm == NULL) {
		return;
	}

	while ((akm->is_suspend == true) && (i++ < 5)){
		akdbgprt("[AK09973] %s[%d] during suspend! delay 10ms!\n", __func__, i);
		mdelay(10);
	}

	ak09973_i2c_write16(akm->client, AK09973_REG_THV, 2, akm->BOPVbits, akm->BRPVbits);
	akm->bopv = akm->BOPVbits * akm->BOPVbits;
	akm->brpv = akm->BRPVbits * akm->BRPVbits;
	akdbgprt("[AK09973] %s done\n", __func__);
}

void ak09973_set_BOPV2bits(void)
{
	struct ak09973_data *akm = pdata;
	int i = 0;

	if (akm == NULL) {
		return;
	}

	while ((akm->is_suspend == true) && (i++ < 5)) {
		akdbgprt("[AK09973] %s[%d] during suspend! delay 10ms!\n", __func__, i);
		mdelay(10);
	}

	ak09973_i2c_write16(akm->client, AK09973_REG_THV, 2, akm->BOPV2bits, akm->BRPV2bits);
	akm->bopv = akm->BOPV2bits * akm->BOPV2bits;
	akm->brpv = akm->BRPV2bits * akm->BRPV2bits;
	akdbgprt("[AK09973] %s done\n", __func__);
}
#endif

#ifdef KEY_UEVENT_SUPPORTED
static void ak09973_report_uevent(struct ak09973_data *akm, char *str)
{
	char *envp[2];

	envp[0] = str;
	envp[1] = NULL;
	kobject_uevent_env(&(akm->dev->kobj), KOBJ_CHANGE, envp);
}
#endif

static void ak09973_send_event(struct ak09973_data *akm)
{
	int wordLen, msrDataAddr, index, v_value, y_value;
	u16 readValue[4], stat;
	int i = 0;

	while ((akm->is_suspend == true) && (i++ < 5)) {
		akdbgprt("[AK09973] %s[%d] during suspend! delay 10ms!\n", __func__, i);
		mdelay(10);
	}

	index = AK09973_MSRNO_WORD_ST_Z_Y_X;
	msrDataAddr = index + AK09973_REG_WORD_ST;
	wordLen = (msrDataBytesTable[index] + 1) / 2;
	ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
	stat = readValue[0];
	y_value = (s16)readValue[2];
	akdbgprt("[AK09973] %s ST value 0x%x\n", __func__, stat);
	//akdbgprt("[AK09973] %s X Y Z value %d %d %d\n", __func__, (s16)readValue[3], (s16)readValue[2], (s16)readValue[1]);

	index = AK09973_MSRNO_WORD_ST_V;
	msrDataAddr = index + AK09973_REG_WORD_ST;
	wordLen = (msrDataBytesTable[index] + 1) / 2;
	ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
	v_value = readValue[1];
	v_value = (v_value << 16) | readValue[2];
	//akdbgprt("[AK09973] %s V value %d\n", __func__, v_value);

	if ((stat & 0x20) == 0x20) {
		akdbgprt("[AK09973] %s magnetic sensor overflow!\n", __func__);
		return;
	}

	if (v_value > akm->bopv) {
		akdbgprt("[AK09973] %s V_value = %d [BRPV BOPV] = [%d %d]. near\n", __func__, v_value, akm->brpv, akm->bopv);
		akm->status = STATUS_NEAR;
	} else if (v_value < akm->brpv) {
		akdbgprt("[AK09973] %s V_value = %d [BRPV BOPV] = [%d %d]. far(need recheck)\n", __func__, v_value, akm->brpv, akm->bopv);
#ifdef FAR_RECHECK_SUPPORTED
		if (akm->pre_status != STATUS_FAR) {
			schedule_delayed_work(&akm->dworker,  msecs_to_jiffies(RECHECK_FAR_DELAY));
			return;
		}
#else
		akm->status = STATUS_FAR;
#endif
	}

	if (akm->pre_status != akm->status) {
		akdbgprt("[AK09973] %s status change to %s! \n", __func__, (akm->status == STATUS_FAR) ? "FAR" : "NEAR");
		akm->pre_status = akm->status;

		if (akm->status == STATUS_NEAR) {
#ifdef KEY_UEVENT_SUPPORTED
			ak09973_report_uevent(akm, TOUCH_PEN_ON);
#endif
#ifdef RA9530_WLS_USED
			enable_ra9530_wls_tx(true, true);
#endif
#ifdef SAR_MANUAL_CALI_EN
			aw9610x_manual_calibrate();
#endif
		}
#ifndef FAR_RECHECK_SUPPORTED
		if (akm->status == STATUS_FAR) {
#ifdef KEY_UEVENT_SUPPORTED
			ak09973_report_uevent(akm, TOUCH_PEN_OFF);
#endif
#ifdef RA9530_WLS_USED
			enable_ra9530_wls_tx(false, false);
#endif
		}
#endif
#ifdef INPUT_EVENT_SUPPORTED
		input_report_abs(akm->input, ABS_RX, akm->status);
		input_report_abs(akm->input, ABS_RY, v_value);
		input_sync(akm->input);
#endif
	}
}

#ifdef FAR_RECHECK_SUPPORTED
static void ak09973_recheck_far_worker(struct work_struct *work)
{
	struct ak09973_data *akm = container_of(work, struct ak09973_data, dworker.work);
	int wordLen, msrDataAddr, index, v_value;
	u16 readValue[4];

	akdbgprt("[AK09973] %s enter.\n", __func__);

	index = AK09973_MSRNO_WORD_ST_V;
	msrDataAddr = index + AK09973_REG_WORD_ST;
	wordLen = (msrDataBytesTable[index] + 1) / 2;
	ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
	v_value = readValue[1];
	v_value = (v_value << 16) | readValue[2];
	akdbgprt("[AK09973] %s V_value = %d [BRPV BOPV] = [%d %d]\n", __func__, v_value, akm->brpv, akm->bopv);

	if (v_value < akm->brpv) {
		akdbgprt("[AK09973] %s Actually FAR! \n", __func__);
		akm->status = STATUS_FAR;
	}

	if (akm->pre_status != akm->status) {
		akdbgprt("[AK09973] %s status change to %s! \n", __func__, (akm->status == STATUS_FAR) ? "FAR" : "NEAR");
		akm->pre_status = akm->status;

		if (akm->status == STATUS_FAR) {
#ifdef KEY_UEVENT_SUPPORTED
			ak09973_report_uevent(akm, TOUCH_PEN_OFF);
#endif
#ifdef RA9530_WLS_USED
			enable_ra9530_wls_tx(false, false);
#endif
		}
#ifdef INPUT_EVENT_SUPPORTED
		input_report_abs(akm->input, ABS_RX, akm->status);
		input_report_abs(akm->input, ABS_RY, v_value);
		input_sync(akm->input);
#endif
	}
}
#endif

static void ak09973_irq_worker(struct work_struct *work)
{
	struct ak09973_data *akm = container_of(work, struct ak09973_data, irq_dworker.work);
	ak09973_send_event(akm);
}

static irqreturn_t ak09973_irq(int32_t irq, void *data)
{
	struct ak09973_data *akm = data;

	akdbgprt("[AK09973] %s enter\n", __func__);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	__pm_wakeup_event(akm->hall_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
#else
	wake_lock_timeout(&akm->hall_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
#endif
	schedule_delayed_work(&akm->irq_dworker, 0);
	return IRQ_HANDLED;
}

#ifdef CASE_RECHECK_SUPPORTED
static void ak09973_case_recheck_dworker(struct work_struct *work)
{
	struct ak09973_data *akm = container_of(work, struct ak09973_data, case_recheck_dworker.work);
	akdbgprt("[AK09973] %s enter.\n", __func__);

	if (akm == NULL) {
		return;
	}

#ifdef CONFIG_ZTE_DOCK_KEYBOARD
	if (zte_case_type == true) {
		dev_info(&akm->client->dev, "%s is_hall_open = %d with big case\n", __func__, akm->is_hall_open);
		ak09973_set_BOPVbits();
		return;
	}
#endif

	dev_info(&akm->client->dev, "%s is_hall_open = %d!\n", __func__, akm->is_hall_open);

	if (akm->is_hall_open)
		ak09973_set_BOPVbits();
	else
		ak09973_set_BOPV2bits();

	ak09973_send_event(akm);
}
#endif

static int ak09973_parse_dt(struct ak09973_data  *ak09973)
{
	u32 buf[8];
	struct device *dev;
	struct device_node *np;
	int ret;

	dev = &(ak09973->client->dev);
	np = dev->of_node;

	if (!np) {
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "ak09973,mode", buf, 1);
	if (ret < 0) {
		ak09973->mode = 1;
	} else {
		ak09973->mode = buf[0];
		if (buf[0] > ak09973->numMode) {
			ak09973->mode = ak09973->numMode;
		}
	}
	akdbgprt("[AK09973] %s mode=%d\n", __func__, ak09973->mode);

	ret = of_property_read_u32_array(np, "ak09973,DRDY_event", buf, 1);
	if (ret < 0) {
		ak09973->DRDYENbit = 1;
	} else {
		ak09973->DRDYENbit = buf[0];
	}

	ret = of_property_read_u32_array(np, "ak09973,ERR_event", buf, 1);
	if (ret < 0) {
		ak09973->ERRENbit = 1;
	} else {
		ak09973->ERRENbit = buf[0];
	}

	ret = of_property_read_u32_array(np, "ak09973,POL_setting", buf, 4);
	if (ret < 0) {
		ak09973->POLXbit = 0;
		ak09973->POLYbit = 0;
		ak09973->POLZbit = 0;
		ak09973->POLVbit = 0;
	} else {
		ak09973->POLXbit = buf[0];
		ak09973->POLYbit = buf[1];
		ak09973->POLZbit = buf[2];
		ak09973->POLVbit = buf[3];
	}

	ret = of_property_read_u32_array(np, "ak09973,SDR_setting", buf, 1);
	if (ret < 0) {
		ak09973->SDRbit = 0;
	} else {
		ak09973->SDRbit = buf[0];
	}

	ret = of_property_read_u32_array(np, "ak09973,SMR_setting", buf, 1);
	if (ret < 0) {
		ak09973->SMRbit = 0;
	} else {
		ak09973->SMRbit = buf[0];
	}

	ret = of_property_read_u32_array(np, "ak09973,threshold_X", buf, 2);
	if (ret < 0) {
		ak09973->BOPXbits = 0;
		ak09973->BRPXbits = 0;
	} else {
		ak09973->BOPXbits = buf[0];
		ak09973->BRPXbits = buf[1];
	}

	ret = of_property_read_u32_array(np, "ak09973,threshold_Y", buf, 2);
	if (ret < 0) {
		ak09973->BOPYbits = 0;
		ak09973->BRPYbits = 0;
	} else {
		ak09973->BOPYbits = buf[0];
		ak09973->BRPYbits = buf[1];
	}

	ret = of_property_read_u32_array(np, "ak09973,threshold_Z", buf, 2);
	if (ret < 0) {
		ak09973->BOPZbits = 0;
		ak09973->BRPZbits = 0;
	} else {
		ak09973->BOPZbits = buf[0];
		ak09973->BRPZbits = buf[1];
	}

#ifndef CONFIG_HALL_3AXIS_PARA_NEW
	ret = of_property_read_u32_array(np, "ak09973,threshold_V", buf, 2);
	if (ret < 0) {
		ak09973->BOPVbits = 0;
		ak09973->BRPVbits = 0;
	} else {
		ak09973->BOPVbits = buf[0];
		ak09973->BRPVbits = buf[1];
	}

	ret = of_property_read_u32_array(np, "ak09973,threshold_V2", buf, 2);
	if (ret < 0) {
		ak09973->BOPV2bits = 0;
		ak09973->BRPV2bits = 0;
	} else {
		ak09973->BOPV2bits = buf[0];
		ak09973->BRPV2bits = buf[1];
	}
#else
	ak09973->BOPVbits = 0xF20; //  bopv = 0xF20*0xF20 = 15 000000
	ak09973->BRPVbits = 0x8BC; //  brpv = 0x8BC*0x8BC = 5 000000
	ak09973->BOPV2bits = 0x171C; //  bopv2 = 0x171C*0x171C = 35 000000
	ak09973->BRPV2bits = 0xF20; //  brpv2 = 0xF20*0xF20=15 000000
#endif

	ret = of_property_read_u32_array(np, "ak09973,SW_event", buf, 4);
	if (ret < 0) {
		ak09973->SWXENbit = 0;
		ak09973->SWYENbit = 0;
		ak09973->SWZENbit = 0;
		ak09973->SWVENbit = 0;
	} else {
		ak09973->SWXENbit = buf[0];
		ak09973->SWYENbit = buf[1];
		ak09973->SWZENbit = buf[2];
		ak09973->SWVENbit = buf[3];
	}

	return 0;
}

static ssize_t status_show(struct class *class, struct class_attribute *attr,
		char *buf)
{
	struct ak09973_data *akm = pdata;
	return snprintf(buf, PAGE_SIZE, "%d\n", akm->status);
}
static CLASS_ATTR_RO(status);

static struct class sensor_class = {
	.name = "hall-3axis",
	.owner = THIS_MODULE,
};

#ifdef ZTE_CMB_QC_BASE_MODULE_SEQUENCE
static int ak09973_probe_work(struct work_struct *work)
{
	struct ak09973_data *akm = pdata;
	int err = 0;

	err = ak09973_setup(akm->client);
	if (err < 0) {
		dev_err(&akm->client->dev, "%s initialization fails\n", __func__);
		return err;
	}

	dev_info(&akm->client->dev, "%s initialization success!\n", __func__);
	return err;
}
#endif

#ifdef GET_HALL_STATUS_EN
void check_3axis_hall_status(bool is_hall_open)
{
	struct ak09973_data *akm = pdata;

	if (akm == NULL) {
		return;
	}

	dev_info(&akm->client->dev, "%s is_hall_open = %d!\n", __func__, is_hall_open);
	akm->is_hall_open = is_hall_open;

#ifdef CONFIG_ZTE_DOCK_KEYBOARD
	if (zte_case_type == true) {
		dev_info(&akm->client->dev, "%s is_hall_open = %d with big case\n", __func__, is_hall_open);
		ak09973_set_BOPVbits();

#ifdef CASE_RECHECK_SUPPORTED
		schedule_delayed_work(&akm->case_recheck_dworker,  msecs_to_jiffies(CASE_RECHECK_DELAY));
#endif
		return;
	}
#endif

	if (akm->is_hall_open)
		ak09973_set_BOPVbits();
	else
		ak09973_set_BOPV2bits();

	ak09973_send_event(akm);

#ifdef CASE_RECHECK_SUPPORTED
	schedule_delayed_work(&akm->case_recheck_dworker,  msecs_to_jiffies(CASE_RECHECK_DELAY));
#endif
}
EXPORT_SYMBOL_GPL(check_3axis_hall_status);

void init_is_hall_open(struct ak09973_data *akm)
{
	extern int get_hall_status(void);
	akm->is_hall_open = get_hall_status();
	dev_info(&akm->client->dev, "%s is_hall_open = %d!\n", __func__, akm->is_hall_open);
}
#endif

#ifdef CHIP_POWER_ON
int ak09973_power_on(struct ak09973_data  *akm)
{
	int ret = -1;

	if (akm == NULL) {
		return ret;
	}

	akm->vdd = devm_regulator_get(&(akm->client->dev), "vdd");
	if (IS_ERR(akm->vdd)) {
		dev_err(&akm->client->dev, "%s regulator vdd get failed %d\n", __func__, PTR_ERR(akm->vdd));
		akm->vdd = NULL;
		return ret;
	}
	dev_info(&akm->client->dev, "%s vdd get successfully!\n", __func__);

	ret = regulator_enable(akm->vdd);
	if (ret) {
		dev_err(&akm->client->dev, "%s regulator vdd enable failed %d\n", __func__, ret);
		return ret;
	}

	dev_info(&akm->client->dev, "%s vdd enable successfully!\n", __func__);
	return ret;
}
#endif

static int ak09973_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ak09973_data *akm;
	int err = 0;
	struct pinctrl *pinctrl = NULL;
	akdbgprt("[AK09973] %s enter\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%d check_functionality failed\n", __func__);
		return -EIO;
	}

	akm = devm_kzalloc(&client->dev, sizeof(struct ak09973_data), GFP_KERNEL);
	if (akm == NULL) {
		dev_err(&client->dev, "%s:failed to malloc memory for akm!\n", __func__);
		return -ENOMEM;
	}

	pdata = akm;

	akm->dev = &client->dev;
	akm->client = client;
	i2c_set_clientdata(client, akm);
	akm->pre_status = -1;
	akm->status = STATUS_FAR;
	akm->bopv = 0;
	akm->brpv = 0;
	akm->numMode = ARRAY_SIZE(measurementFreqTable);

	err = ak09973_parse_dt(akm);
	if (err < 0)
		dev_err(&client->dev, "[AK09973] Device Tree Setting was not found!\n");

#ifdef CHIP_POWER_ON
	err = ak09973_power_on(akm);
	if (err < 0) {
		return err;
	}
#endif

	err = ak09973_check_chip_id(client);
	if (err < 0)
		return err;

	pinctrl = devm_pinctrl_get_select_default(&client->dev);
	if (IS_ERR(pinctrl)) {
		pr_info("[AK09973] pins are not configured from the driver\n");
		// err = -1;
		// return err;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	akm->hall_wakelock = wakeup_source_register(NULL, "hall_wakelock");
#else
	wake_lock_init(&akm->hall_wakelock, WAKE_LOCK_SUSPEND, "hall_wakelock");
#endif

#ifdef FAR_RECHECK_SUPPORTED
	INIT_DELAYED_WORK(&akm->dworker, ak09973_recheck_far_worker);
#endif
	INIT_DELAYED_WORK(&akm->irq_dworker, ak09973_irq_worker);
#ifdef CASE_RECHECK_SUPPORTED
	INIT_DELAYED_WORK(&akm->case_recheck_dworker, ak09973_case_recheck_dworker);
#endif

	akm->int_gpio = of_get_gpio(client->dev.of_node, 0);
	if (!gpio_is_valid(akm->int_gpio)) {
		dev_err(&client->dev, "AK09973] failed to request GPIO 000 %d, error %d\n", akm->int_gpio, err);
		goto err1;
	}

	err = devm_gpio_request_one(&client->dev, akm->int_gpio, GPIOF_IN, NULL);
	if (err < 0) {
		dev_err(&client->dev, "AK09973] failed to request GPIO 111 %d, error %d\n", akm->int_gpio, err);
		goto err1;
	}
	akm->irq = gpio_to_irq(akm->int_gpio);

	err = devm_request_threaded_irq(&client->dev, akm->irq, NULL,
				ak09973_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "ak09973_irq", akm);
	if (err != 0) {
		dev_err(&client->dev, "%s: failed to request IRQ %d: %d\n", __func__, akm->irq, err);
		goto err2;
	}

#ifdef GET_HALL_STATUS_EN
	init_is_hall_open(akm);
#else
	akm->is_hall_open = 1;
#endif
	enable_irq_wake(akm->irq);

#ifndef ZTE_CMB_QC_BASE_MODULE_SEQUENCE
	err = ak09973_setup(client);
	if (err < 0) {
		dev_err(&client->dev, "%s initialization fails\n", __func__);
		goto err2;
	}
	dev_info(&akm->client->dev, "%s initialization success!\n", __func__);
#endif

#ifdef INPUT_EVENT_SUPPORTED
	akm->input = input_allocate_device();
	if (akm->input == NULL) {
		err = -1;
		goto err2;
	}

	akm->input->name = "ak09973";
	__set_bit(EV_KEY, akm->input->evbit);
	__set_bit(EV_SYN, akm->input->evbit);
	__set_bit(KEY_F1, akm->input->keybit);
	input_set_abs_params(akm->input, ABS_DISTANCE, 0, 100, 0, 0);
	input_set_capability(akm->input, EV_ABS, ABS_RX);
	input_set_capability(akm->input, EV_ABS, ABS_RY);
	input_set_capability(akm->input, EV_ABS, ABS_RZ);

	err = input_register_device(akm->input);
	if (err) {
		dev_err(&client->dev, "%s: failed to register input device: %s\n", __func__);
		goto err3;
	}
#endif

	err = sysfs_create_group(&client->dev.kobj, &ak09973_attribute_group);
	if (err < 0) {
		dev_err(&client->dev, "%s error creating sysfs attr files\n", __func__);
		goto err4;
	}

	err = class_register(&sensor_class);
	if (err < 0) {
		dev_err(&client->dev, "%s: class_register failed! err = %d\n", __func__, err);
		goto err4;
	}

	err = class_create_file(&sensor_class, &class_attr_status);
	if (err < 0) {
		dev_err(&client->dev, "%s: class_attr_status failed! err = %d\n", __func__, err);
		goto err5;
	}

	akdbgprt("[AK09973] probe sucessfully!\n");

#ifdef ZTE_CMB_QC_BASE_MODULE_SEQUENCE
	sqc_sequence_load_init(ak09973_probe_work, 4, 0);
	dev_info(&client->dev, "%s: sqc_sequence_load_init done!\n", __func__);
#endif

	return err;
err5:
	class_unregister(&sensor_class);
err4:
#ifdef INPUT_EVENT_SUPPORTED
	input_unregister_device(akm->input);
err3:
	input_free_device(akm->input);
#endif
err2:
	devm_gpio_free(&client->dev, akm->int_gpio);
err1:
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	wakeup_source_unregister(akm->hall_wakelock);
#else
	wake_lock_destroy(&akm->hall_wakelock);
#endif
	devm_kfree(&client->dev, akm);
	dev_err(&client->dev, "%s: probe failed!\n", __func__);
	return err;
}

static int ak09973_remove(struct i2c_client *client)
{
	struct ak09973_data *akm = (struct ak09973_data *)i2c_get_clientdata(client);
	class_unregister(&sensor_class);
	sysfs_remove_group(&client->dev.kobj, &ak09973_attribute_group);
#ifdef INPUT_EVENT_SUPPORTED
	input_unregister_device(akm->input);
	input_free_device(akm->input);
#endif
#ifdef FAR_RECHECK_SUPPORTED
	cancel_delayed_work_sync(&akm->dworker);
#endif
	cancel_delayed_work_sync(&akm->irq_dworker);
#ifdef CASE_RECHECK_SUPPORTED
	cancel_delayed_work_sync(&akm->case_recheck_dworker);
#endif
	devm_gpio_free(&client->dev, akm->int_gpio);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
	wakeup_source_unregister(akm->hall_wakelock);
#else
	wake_lock_destroy(&akm->hall_wakelock);
#endif
	devm_kfree(&client->dev, akm);
	ak09973_i2c_write8(client, AK09973_REG_CNTL2, 0);
	return 0;
}

static int ak09973_i2c_suspend(struct device *dev)
{
	struct ak09973_data *akm = pdata;
	akm->is_suspend = true;
	akdbgprt("[AK09973] %s enter\n", __func__);
	return 0;
}

static int ak09973_i2c_resume(struct device *dev)
{
	struct ak09973_data *akm = pdata;
	akdbgprt("[AK09973] %s enter\n", __func__);
	akm->is_suspend = false;
	return 0;
}

static const struct dev_pm_ops ak09973_i2c_pops = {
	.suspend	= ak09973_i2c_suspend,
	.resume		= ak09973_i2c_resume,
};

static const struct i2c_device_id ak09973_id[] = {
	{ "ak09973", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak09973_id);

static const struct of_device_id ak09973_of_match[] = {
	{ .compatible = "akm,ak09973"},
	{}
};
MODULE_DEVICE_TABLE(of, ak09973_of_match);

static struct i2c_driver ak09973_driver = {
	.driver = {
		.name	= "ak09973",
		.pm = &ak09973_i2c_pops,
		.of_match_table = of_match_ptr(ak09973_of_match),
	},
	.probe		= ak09973_probe,
	.remove		= ak09973_remove,
	.id_table	= ak09973_id,
};
module_i2c_driver(ak09973_driver);

MODULE_AUTHOR("ZTE");
MODULE_DESCRIPTION("AK09973 magnetometer driver");
MODULE_LICENSE("GPL v2");
