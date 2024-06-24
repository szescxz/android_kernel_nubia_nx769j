 /*
  * Goodix Touchscreen Driver
  * Copyright (C) 2020 - 2021 Goodix, Inc.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be a reference
  * to you, when you are integrating the GOODiX's CTP IC into your system,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * General Public License for more details.
  *
  */
#include "goodix_ts_core.h"

/* berlin_A SPI mode setting */
#define GOODIX_SPI_MODE_REG			0xC900
#define GOODIX_SPI_NORMAL_MODE_0	0x01

/* berlin_A D12 setting */
#define GOODIX_REG_CLK_STA0			0xD807
#define GOODIX_CLK_STA0_ENABLE		0xFF
#define GOODIX_REG_CLK_STA1			0xD806
#define GOODIX_CLK_STA1_ENABLE		0x77
#define GOODIX_REG_TRIM_D12			0xD006
#define GOODIX_TRIM_D12_LEVEL		0x3C
#define GOODIX_REG_RESET			0xD808
#define GOODIX_RESET_EN				0xFA
#define HOLD_CPU_REG_W				0x0002
#define HOLD_CPU_REG_R				0x2000

#define DEV_CONFIRM_VAL				0xAA
#define BOOTOPTION_ADDR				0x10000
#define FW_VERSION_INFO_ADDR_BRA	0x1000C
#define FW_VERSION_INFO_ADDR		0x10014

#define GOODIX_IC_INFO_MAX_LEN		1024
#define GOODIX_IC_INFO_ADDR_BRA		0x10068
#define GOODIX_IC_INFO_ADDR			0x10070


enum brl_request_code {
	BRL_REQUEST_CODE_CONFIG = 0x01,
	BRL_REQUEST_CODE_REF_ERR = 0x02,
	BRL_REQUEST_CODE_RESET = 0x03,
	BRL_REQUEST_CODE_CLOCK = 0x04,
};

static int brl_select_spi_mode(struct goodix_ts_core *cd)
{
	int ret;
	int i;
	u8 w_value = GOODIX_SPI_NORMAL_MODE_0;
	u8 r_value;

	if (cd->bus->bus_type == GOODIX_BUS_TYPE_I2C ||
			cd->bus->ic_type != IC_TYPE_BERLIN_A)
		return 0;

	for (i = 0; i < GOODIX_RETRY_5; i++) {
		cd->hw_ops->write(cd, GOODIX_SPI_MODE_REG,
				&w_value, 1);
		ret = cd->hw_ops->read(cd, GOODIX_SPI_MODE_REG,
				&r_value, 1);
		if (!ret && r_value == w_value)
			return 0;
	}
	ts_err("failed switch SPI mode, ret:%d r_value:%02x", ret, r_value);
	return -EINVAL;
}

static int brl_dev_confirm(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;
	int retry = GOODIX_RETRY_3;
	u8 tx_buf[8] = {0};
	u8 rx_buf[8] = {0};

	memset(tx_buf, DEV_CONFIRM_VAL, sizeof(tx_buf));
	while (retry--) {
		ret = hw_ops->write(cd, BOOTOPTION_ADDR,
			tx_buf, sizeof(tx_buf));
		if (ret < 0)
			return ret;
		ret = hw_ops->read(cd, BOOTOPTION_ADDR,
			rx_buf, sizeof(rx_buf));
		if (ret < 0)
			return ret;
		if (!memcmp(tx_buf, rx_buf, sizeof(tx_buf)))
			break;
		usleep_range(5000, 5100);
	}

	if (retry < 0) {
		ts_err("device confirm failed, rx_buf:%*ph", 8, rx_buf);
		return -EINVAL;
	}

	ts_info("device connected");
	return ret;
}

static int brl_reset_after(struct goodix_ts_core *cd)
{
	u8 reg_val[2] = {0};
	u8 temp_buf[12] = {0};
	int ret;
	int retry;

	if (cd->bus->ic_type != IC_TYPE_BERLIN_A)
		return 0;

	ts_info("IN");

	/* select spi mode */
	ret = brl_select_spi_mode(cd);
	if (ret < 0)
		return ret;

	/* hold cpu */
	retry = GOODIX_RETRY_10;
	while (retry--) {
		reg_val[0] = 0x01;
		reg_val[1] = 0x00;
		ret = cd->hw_ops->write(cd, HOLD_CPU_REG_W, reg_val, 2);
		ret |= cd->hw_ops->read(cd, HOLD_CPU_REG_R, &temp_buf[0], 4);
		ret |= cd->hw_ops->read(cd, HOLD_CPU_REG_R, &temp_buf[4], 4);
		ret |= cd->hw_ops->read(cd, HOLD_CPU_REG_R, &temp_buf[8], 4);
		if (!ret && !memcmp(&temp_buf[0], &temp_buf[4], 4) &&
			!memcmp(&temp_buf[4], &temp_buf[8], 4) &&
			!memcmp(&temp_buf[0], &temp_buf[8], 4)) {
			break;
		}
	}
	if (retry < 0) {
		ts_err("failed to hold cpu, status:%*ph", 12, temp_buf);
		return -EINVAL;
	}

	/* enable sta0 clk */
	retry = GOODIX_RETRY_5;
	while (retry--) {
		reg_val[0] = GOODIX_CLK_STA0_ENABLE;
		ret = cd->hw_ops->write(cd, GOODIX_REG_CLK_STA0, reg_val, 1);
		ret |= cd->hw_ops->read(cd, GOODIX_REG_CLK_STA0, temp_buf, 1);
		if (!ret && temp_buf[0] == GOODIX_CLK_STA0_ENABLE)
			break;
	}
	if (retry < 0) {
		ts_err("failed to enable group0 clock, ret:%d status:%02x",
				ret, temp_buf[0]);
		return -EINVAL;
	}

	/* enable sta1 clk */
	retry = GOODIX_RETRY_5;
	while (retry--) {
		reg_val[0] = GOODIX_CLK_STA1_ENABLE;
		ret = cd->hw_ops->write(cd, GOODIX_REG_CLK_STA1, reg_val, 1);
		ret |= cd->hw_ops->read(cd, GOODIX_REG_CLK_STA1, temp_buf, 1);
		if (!ret && temp_buf[0] == GOODIX_CLK_STA1_ENABLE)
			break;
	}
	if (retry < 0) {
		ts_err("failed to enable group1 clock, ret:%d status:%02x",
				ret, temp_buf[0]);
		return -EINVAL;
	}

	/* set D12 level */
	retry = GOODIX_RETRY_5;
	while (retry--) {
		reg_val[0] = GOODIX_TRIM_D12_LEVEL;
		ret = cd->hw_ops->write(cd, GOODIX_REG_TRIM_D12, reg_val, 1);
		ret |= cd->hw_ops->read(cd, GOODIX_REG_TRIM_D12, temp_buf, 1);
		if (!ret && temp_buf[0] == GOODIX_TRIM_D12_LEVEL)
			break;
	}
	if (retry < 0) {
		ts_err("failed to set D12, ret:%d status:%02x",
				ret, temp_buf[0]);
		return -EINVAL;
	}

	usleep_range(5000, 5100);
	/* soft reset */
	reg_val[0] = GOODIX_RESET_EN;
	ret = cd->hw_ops->write(cd, GOODIX_REG_RESET, reg_val, 1);
	if (ret < 0)
		return ret;

	/* select spi mode */
	ret = brl_select_spi_mode(cd);
	if (ret < 0)
		return ret;

	ts_info("OUT");

	return 0;
}

static int brl_power_on(struct goodix_ts_core *cd, bool on)
{
	int ret = 0;
	static bool dev_confirmed = false;
	bool dev_confirmed_result = true;

	if (on) {
		/* must guarantee iovdd enbaled before avdd */
		ret = gpio_direction_output(cd->iovdd_enable_gpio, 1);

		if (ret) {
			ts_err("Failed to enable iovdd:%d", ret);
			return ret;
		}
		usleep_range(3000, 3100);

		ret = regulator_enable(cd->avdd);
		if (ret) {
			ts_err("Failed to enable avdd power:%d", ret);
			return ret;
		}
		ts_info("regulator enable SUCCESS");
		if (!dev_confirmed) {
			usleep_range(12000, 12100);
			gpio_direction_output(cd->board_data.reset_gpio, 1);
			msleep(100);
			dev_confirmed = true;
			ret = brl_dev_confirm(cd);
			if (ret < 0) {
				dev_confirmed_result = false;
				goto power_off;
			}
			ret = brl_reset_after(cd);
			if (ret < 0) {
				dev_confirmed_result = false;
				goto power_off;
			}
			msleep(GOODIX_NORMAL_RESET_DELAY_MS);
		}
		return 0;
	}

power_off:
	/*power off process */
	ret = gpio_direction_output(cd->iovdd_enable_gpio, 0);

	if (ret)
		ts_err("Failed to disable iovdd:%d", ret);
	ret = regulator_disable(cd->avdd);
	if (!ret)
		ts_info("power disable SUCCESS");
	else
		ts_err("Failed to disable analog power:%d", ret);
	gpio_direction_output(cd->board_data.reset_gpio, 0);
	usleep_range(10000, 11000);
	if (!dev_confirmed_result)
		ret = -EIO;
	return ret;
}

int brl_suspend(struct goodix_ts_core *cd)
{
	int ret = 0;
	u32 cmd_reg = cd->ic_info.misc.cmd_addr;
	u8 sleep_cmd[] = {0x00, 0x00, 0x04, 0x84, 0x88, 0x00};
	if ((cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap) {
		ret = cd->hw_ops->write(cd, cmd_reg, sleep_cmd, sizeof(sleep_cmd));
	}
	else {
		brl_power_on(cd, false);
	}
	return ret;
}

int brl_resume(struct goodix_ts_core *cd)
{
	int ret = 0;

	if ((cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap) {
		ts_info("gesture mode on");
        }
	else {
		brl_power_on(cd, true);
	}
	ret = cd->hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	return ret;
}

#define GOODIX_GESTURE_CMD_BA	0x12
#define GOODIX_GESTURE_CMD		0xA6
int brl_gesture(struct goodix_ts_core *cd, int gesture_type)
{
	struct goodix_ts_cmd cmd;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A)
		cmd.cmd = GOODIX_GESTURE_CMD_BA;
	else
		cmd.cmd = GOODIX_GESTURE_CMD;
	cmd.len = 5;
	cmd.data[0] = gesture_type;
	if (cd->hw_ops->send_cmd(cd, &cmd))
		ts_err("failed send gesture cmd");

	return 0;
}

static int brl_reset(struct goodix_ts_core *cd, int delay)
{
	ts_info("chip_reset");

	gpio_direction_output(cd->board_data.reset_gpio, 0);
	usleep_range(2000, 2100);
	gpio_direction_output(cd->board_data.reset_gpio, 1);
	if (delay < 20)
		usleep_range(delay * 1000, delay * 1000 + 100);
	else
		msleep(delay);

	return brl_select_spi_mode(cd);
}

static int brl_irq_enbale(struct goodix_ts_core *cd, bool enable)
{
	if (enable && !atomic_cmpxchg(&cd->irq_enabled, 0, 1)) {
		enable_irq(cd->irq);
		ts_debug("Irq enabled");
		return 0;
	}

	if (!enable && atomic_cmpxchg(&cd->irq_enabled, 1, 0)) {
		disable_irq(cd->irq);
		ts_debug("Irq disabled");
		return 0;
	}
	ts_info("warnning: irq deepth inbalance!");
	return 0;
}

static int brl_read(struct goodix_ts_core *cd, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->read(bus->dev, addr, data, len);
}

static int brl_write(struct goodix_ts_core *cd, unsigned int addr,
		 unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->write(bus->dev, addr, data, len);
}

/* command ack info */
#define CMD_ACK_IDLE             0x01
#define CMD_ACK_BUSY             0x02
#define CMD_ACK_BUFFER_OVERFLOW  0x03
#define CMD_ACK_CHECKSUM_ERROR   0x04
#define CMD_ACK_OK               0x80

#define GOODIX_CMD_RETRY 6
static DEFINE_MUTEX(cmd_mutex);
static int brl_send_cmd(struct goodix_ts_core *cd,
	struct goodix_ts_cmd *cmd)
{
	int ret, retry, i;
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	mutex_lock(&cmd_mutex);

	cmd->state = 0;
	cmd->ack = 0;
	goodix_append_checksum(&(cmd->buf[2]), cmd->len - 2,
		CHECKSUM_MODE_U8_LE);
	ts_debug("cmd data %*ph", cmd->len, &(cmd->buf[2]));

	retry = 0;
	while (retry++ < GOODIX_CMD_RETRY) {
		ret = hw_ops->write(cd, misc->cmd_addr,
				    cmd->buf, sizeof(*cmd));
		if (ret < 0) {
			ts_err("failed write command");
			goto exit;
		}
		for (i = 0; i < GOODIX_CMD_RETRY; i++) {
			/* check command result */
			ret = hw_ops->read(cd, misc->cmd_addr,
				cmd_ack.buf, sizeof(cmd_ack));
			if (ret < 0) {
				ts_err("failed read command ack, %d", ret);
				goto exit;
			}
			ts_debug("cmd ack data %*ph",
				 (int)sizeof(cmd_ack), cmd_ack.buf);
			if (cmd_ack.ack == CMD_ACK_OK) {
				msleep(40);		// wait for cmd response
				ret = 0;
				goto exit;
			}
			if (cmd_ack.ack == CMD_ACK_BUSY ||
			    cmd_ack.ack == 0x00) {
				usleep_range(1000, 1100);
				continue;
			}
			if (cmd_ack.ack == CMD_ACK_BUFFER_OVERFLOW)
				usleep_range(10000, 11000);
			usleep_range(1000, 1100);
			break;
		}
	}
	ts_err("failed get valid cmd ack");
	ret = -EINVAL;
exit:
	mutex_unlock(&cmd_mutex);
	return ret;
}

/* read from flash */
#define FLASH_CMD_R_START           0x09 
#define FLASH_CMD_W_START           0x0A
#define FLASH_CMD_RW_FINISH         0x0B
#define FLASH_CMD_STATE_READY       0x04
#define FLASH_CMD_STATE_CHECKERR    0x05
#define FLASH_CMD_STATE_DENY        0x06
#define FLASH_CMD_STATE_OKAY        0x07
static int goodix_flash_cmd(struct goodix_ts_core *cd,
						uint8_t cmd, uint8_t status,
						int retry_count)
{
	u32 cmd_addr = cd->ic_info.misc.cmd_addr;
	struct goodix_ts_cmd temp_cmd;
    int ret;
    int i;
    u8 rcv_buf[2];

	temp_cmd.state = 0;
	temp_cmd.ack = 0;
    temp_cmd.len = 4;
    temp_cmd.cmd = cmd;
	goodix_append_checksum(&temp_cmd.buf[2], temp_cmd.len - 2,
		CHECKSUM_MODE_U8_LE);
	ret = brl_write(cd, cmd_addr, temp_cmd.buf, temp_cmd.len + 2);
	if (ret < 0) {
		ts_err("send flash cmd[%x] failed", cmd);
		return ret;
	}

    for (i = 0; i < retry_count; i++) {
		msleep(20);
        ret = brl_read(cd, cmd_addr, rcv_buf, 2);
        if (rcv_buf[0] == status && rcv_buf[1] == 0x80)
            return 0;
    }

    ts_err("r_sta[0x%x] != status[0x%x]", rcv_buf[0], status);
    return -EINVAL;
}

static int brl_flash_read(struct goodix_ts_core *cd,
						unsigned int addr, unsigned char *buf,
						unsigned int len)
{
    int i;
    int ret;
    u8 *tmp_buf;
    u32 buffer_addr = cd->ic_info.misc.fw_buffer_addr;
    struct goodix_ts_cmd temp_cmd;
    uint32_t checksum = 0;
    struct flash_head head_info;
    u8 *p = (u8 *)&head_info.address;

    tmp_buf = kzalloc(len + sizeof(head_info), GFP_KERNEL);
    if (!tmp_buf)
        return -ENOMEM;

    head_info.address = cpu_to_le32(addr);
    head_info.length = cpu_to_le32(len);
    for (i = 0; i < 8; i += 2)
        checksum += p[i] | (p[i + 1] << 8);
    head_info.checksum = checksum;

    ret = goodix_flash_cmd(cd, FLASH_CMD_R_START, FLASH_CMD_STATE_READY, 15);
    if (ret < 0) {
        ts_err("failed enter flash read state");
        goto read_end;
    }

    ret = brl_write(cd, buffer_addr, (u8 *)&head_info, sizeof(head_info));
    if (ret < 0) {
        ts_err("failed write flash head info");
        goto read_end;   
    }

    ret = goodix_flash_cmd(cd, FLASH_CMD_RW_FINISH, FLASH_CMD_STATE_OKAY, 50);
    if (ret) {
        ts_err("faild read flash ready state");
        goto read_end;
    }

    ret = brl_read(cd, buffer_addr, tmp_buf, len + sizeof(head_info));
    if (ret < 0) {
        ts_err("failed read data len %lu", len + sizeof(head_info));
        goto read_end;
    }

    checksum = 0;
    for (i = 0; i < len + sizeof(head_info) - 4; i += 2)
        checksum += tmp_buf[4 + i] | (tmp_buf[5 + i] << 8);

    if (checksum != le32_to_cpup((__le32 *)tmp_buf)) {
        ts_err("read back data checksum error");
        ret = -EINVAL;
        goto read_end;
    }

    memcpy(buf, tmp_buf + sizeof(head_info), len);
    ret = 0;    
read_end:
    temp_cmd.len = 4;
    temp_cmd.cmd = 0x0C;
    brl_send_cmd(cd, &temp_cmd);
    return ret;
}

#pragma  pack(1)
struct goodix_config_head {
	union {
		struct {
			u8 panel_name[8];
			u8 fw_pid[8];
			u8 fw_vid[4];
			u8 project_name[8];
			u8 file_ver[2];
			u32 cfg_id;
			u8 cfg_ver;
			u8 cfg_time[8];
			u8 reserved[15];
			u8 flag;
			u16 cfg_len;
			u8 cfg_num;
			u16 checksum;
		};
		u8 buf[64];
	};
};
#pragma pack()

#define CONFIG_CND_LEN			4
#define CONFIG_CMD_START		0x04
#define CONFIG_CMD_WRITE		0x05
#define CONFIG_CMD_EXIT			0x06
#define CONFIG_CMD_READ_START	0x07
#define CONFIG_CMD_READ_EXIT	0x08

#define CONFIG_CMD_STATUS_PASS	0x80
#define CONFIG_CMD_WAIT_RETRY	20

static int wait_cmd_status(struct goodix_ts_core *cd,
	u8 target_status, int retry)
{
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int i, ret;

	for (i = 0; i < retry; i++) {
		ret = hw_ops->read(cd, misc->cmd_addr, cmd_ack.buf,
			sizeof(cmd_ack));
		if (!ret && cmd_ack.state == target_status) {
			ts_debug("status check pass");
			return 0;
		}
		ts_debug("cmd buf %*ph", (int)sizeof(cmd_ack), cmd_ack.buf);
		msleep(20);
	}

	ts_err("cmd status not ready, retry %d, ack 0x%x, status 0x%x, ret %d",
			i, cmd_ack.ack, cmd_ack.state, ret);
	return -EINVAL;
}

static int send_cfg_cmd(struct goodix_ts_core *cd,
	struct goodix_ts_cmd *cfg_cmd)
{
	int ret;

	ret = cd->hw_ops->send_cmd(cd, cfg_cmd);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d", ret);
		return ret;
	}
	ret = wait_cmd_status(cd, CONFIG_CMD_STATUS_PASS,
		CONFIG_CMD_WAIT_RETRY);
	if (ret) {
		ts_err("failed wait for fw ready for config, %d", ret);
		return ret;
	}
	return 0;
}

static int brl_send_config(struct goodix_ts_core *cd, u8 *cfg, int len)
{
	int ret;
	u8 *tmp_buf;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (len > misc->fw_buffer_max_len) {
		ts_err("config len exceed limit %d > %d",
			len, misc->fw_buffer_max_len);
		return -EINVAL;
	}

	tmp_buf = kzalloc(len, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_START;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d", ret);
		goto exit;
	}

	ts_debug("try send config to 0x%x, len %d", misc->fw_buffer_addr, len);
	ret = hw_ops->write(cd, misc->fw_buffer_addr, cfg, len);
	if (ret) {
		ts_err("failed write config data, %d", ret);
		goto exit;
	}
	ret = hw_ops->read(cd, misc->fw_buffer_addr, tmp_buf, len);
	if (ret) {
		ts_err("failed read back config data");
		goto exit;
	}

	if (memcmp(cfg, tmp_buf, len)) {
		ts_err("config data read back compare file");
		ret = -EINVAL;
		goto exit;
	}
	/* notify fw for recive config */
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_WRITE;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret)
		ts_err("failed send config data ready cmd %d", ret);

exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_EXIT;
	if (send_cfg_cmd(cd, &cfg_cmd)) {
		ts_err("failed send config write end command");
		ret = -EINVAL;
	}

	if (!ret) {
		ts_info("success send config");
		msleep(100);
	}

	kfree(tmp_buf);
	return ret;
}

/*
 * return: return config length on succes, other wise return < 0
 **/
static int brl_read_config(struct goodix_ts_core *cd, u8 *cfg, int size)
{
	int ret;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_config_head cfg_head;

	if (!cfg)
		return -EINVAL;

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_START;
	ret = send_cfg_cmd(cd, &cfg_cmd);
	if (ret) {
		ts_err("failed send config read prepare command");
		return ret;
	}

	ret = hw_ops->read(cd, misc->fw_buffer_addr,
			   cfg_head.buf, sizeof(cfg_head));
	if (ret) {
		ts_err("failed read config head %d", ret);
		goto exit;
	}

	if (checksum_cmp(cfg_head.buf, sizeof(cfg_head), CHECKSUM_MODE_U8_LE)) {
		ts_err("config head checksum error");
		ret = -EINVAL;
		goto exit;
	}

	cfg_head.cfg_len = le16_to_cpu(cfg_head.cfg_len);
	if (cfg_head.cfg_len > misc->fw_buffer_max_len ||
	    cfg_head.cfg_len > size) {
		ts_err("cfg len exceed buffer size %d > %d", cfg_head.cfg_len,
			 misc->fw_buffer_max_len);
		ret = -EINVAL;
		goto exit;
	}

	memcpy(cfg, cfg_head.buf, sizeof(cfg_head));
	ret = hw_ops->read(cd, misc->fw_buffer_addr + sizeof(cfg_head),
			   cfg + sizeof(cfg_head), cfg_head.cfg_len);
	if (ret) {
		ts_err("failed read cfg pack, %d", ret);
		goto exit;
	}

	ts_info("config len %d", cfg_head.cfg_len);
	if (checksum_cmp(cfg + sizeof(cfg_head),
			 cfg_head.cfg_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("config body checksum error");
		ret = -EINVAL;
		goto exit;
	}
	ts_info("success read config data: len %zu",
		cfg_head.cfg_len + sizeof(cfg_head));
exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_EXIT;
	if (send_cfg_cmd(cd, &cfg_cmd)) {
		ts_err("failed send config read finish command");
		ret = -EINVAL;
	}
	if (ret)
		return -EINVAL;
	return cfg_head.cfg_len + sizeof(cfg_head);
}

/*
 *	return: 0 for no error.
 *	GOODIX_EBUS when encounter a bus error
 *	GOODIX_ECHECKSUM version checksum error
 *	GOODIX_EVERSION  patch ID compare failed,
 *	in this case the sensorID is valid.
 */
static int brl_read_version(struct goodix_ts_core *cd,
			struct goodix_fw_version *version)
{
	int ret, i;
	u32 fw_addr;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	u8 buf[sizeof(struct goodix_fw_version)] = {0};
	u8 temp_pid[8] = {0};

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A)
		fw_addr = FW_VERSION_INFO_ADDR_BRA;
	else
		fw_addr = FW_VERSION_INFO_ADDR;

	for (i = 0; i < 2; i++) {
		ret = hw_ops->read(cd, fw_addr, buf, sizeof(buf));
		if (ret) {
			ts_info("read fw version: %d, retry %d", ret, i);
			ret = -GOODIX_EBUS;
			usleep_range(5000, 5100);
			continue;
		}

		if (!checksum_cmp(buf, sizeof(buf), CHECKSUM_MODE_U8_LE))
			break;

		ts_info("invalid fw version: checksum error!");
		ts_info("fw version:%*ph", (int)sizeof(buf), buf);
		ret = -GOODIX_ECHECKSUM;
		usleep_range(10000, 11000);
	}
	if (ret) {
		ts_err("failed get valied fw version");
		return ret;
	}
	memcpy(version, buf, sizeof(*version));
	memcpy(temp_pid, version->rom_pid, sizeof(version->rom_pid));
	ts_info("rom_pid:%s", temp_pid);
	ts_info("rom_vid:%*ph", (int)sizeof(version->rom_vid),
		version->rom_vid);
	ts_info("pid:%s", version->patch_pid);
	ts_info("vid:%*ph", (int)sizeof(version->patch_vid),
		version->patch_vid);
	ts_info("sensor_id:%d", version->sensor_id);

	return 0;
}

#define LE16_TO_CPU(x)  (x = le16_to_cpu(x))
#define LE32_TO_CPU(x)  (x = le32_to_cpu(x))
static int convert_ic_info(struct goodix_ic_info *info, const u8 *data)
{
	int i;
	struct goodix_ic_info_version *version = &info->version;
	struct goodix_ic_info_feature *feature = &info->feature;
	struct goodix_ic_info_param *parm = &info->parm;
	struct goodix_ic_info_misc *misc = &info->misc;

	info->length = le16_to_cpup((__le16 *)data);

	data += 2;
	memcpy(version, data, sizeof(*version));
	version->config_id = le32_to_cpu(version->config_id);

	data += sizeof(struct goodix_ic_info_version);
	memcpy(feature, data, sizeof(*feature));
	feature->freqhop_feature =
		le16_to_cpu(feature->freqhop_feature);
	feature->calibration_feature =
		le16_to_cpu(feature->calibration_feature);
	feature->gesture_feature =
		le16_to_cpu(feature->gesture_feature);
	feature->side_touch_feature =
		le16_to_cpu(feature->side_touch_feature);
	feature->stylus_feature =
		le16_to_cpu(feature->stylus_feature);

	data += sizeof(struct goodix_ic_info_feature);
	parm->drv_num = *(data++);
	parm->sen_num = *(data++);
	parm->button_num = *(data++);
	parm->force_num = *(data++);
	parm->active_scan_rate_num = *(data++);
	if (parm->active_scan_rate_num > MAX_SCAN_RATE_NUM) {
		ts_err("invalid scan rate num %d > %d",
			parm->active_scan_rate_num, MAX_SCAN_RATE_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->active_scan_rate_num; i++)
		parm->active_scan_rate[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->active_scan_rate_num * 2;
	parm->mutual_freq_num = *(data++);
	if (parm->mutual_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid mntual freq num %d > %d",
			parm->mutual_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->mutual_freq_num; i++)
		parm->mutual_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->mutual_freq_num * 2;
	parm->self_tx_freq_num = *(data++);
	if (parm->self_tx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid tx freq num %d > %d",
			parm->self_tx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_tx_freq_num; i++)
		parm->self_tx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_tx_freq_num * 2;
	parm->self_rx_freq_num = *(data++);
	if (parm->self_rx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid rx freq num %d > %d",
			parm->self_rx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_rx_freq_num; i++)
		parm->self_rx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_rx_freq_num * 2;
	parm->stylus_freq_num = *(data++);
	if (parm->stylus_freq_num > MAX_FREQ_NUM_STYLUS) {
		ts_err("invalid stylus freq num %d > %d",
			parm->stylus_freq_num, MAX_FREQ_NUM_STYLUS);
		return -EINVAL;
	}
	for (i = 0; i < parm->stylus_freq_num; i++)
		parm->stylus_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->stylus_freq_num * 2;
	memcpy(misc, data, sizeof(*misc));
	misc->cmd_addr = le32_to_cpu(misc->cmd_addr);
	misc->cmd_max_len = le16_to_cpu(misc->cmd_max_len);
	misc->cmd_reply_addr = le32_to_cpu(misc->cmd_reply_addr);
	misc->cmd_reply_len = le16_to_cpu(misc->cmd_reply_len);
	misc->fw_state_addr = le32_to_cpu(misc->fw_state_addr);
	misc->fw_state_len = le16_to_cpu(misc->fw_state_len);
	misc->fw_buffer_addr = le32_to_cpu(misc->fw_buffer_addr);
	misc->fw_buffer_max_len = le16_to_cpu(misc->fw_buffer_max_len);
	misc->frame_data_addr = le32_to_cpu(misc->frame_data_addr);
	misc->frame_data_head_len = le16_to_cpu(misc->frame_data_head_len);

	misc->fw_attr_len = le16_to_cpu(misc->fw_attr_len);
	misc->fw_log_len = le16_to_cpu(misc->fw_log_len);
	misc->stylus_struct_len = le16_to_cpu(misc->stylus_struct_len);
	misc->mutual_struct_len = le16_to_cpu(misc->mutual_struct_len);
	misc->self_struct_len = le16_to_cpu(misc->self_struct_len);
	misc->noise_struct_len = le16_to_cpu(misc->noise_struct_len);
	misc->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	misc->touch_data_head_len = le16_to_cpu(misc->touch_data_head_len);
	misc->point_struct_len = le16_to_cpu(misc->point_struct_len);
	LE32_TO_CPU(misc->mutual_rawdata_addr);
	LE32_TO_CPU(misc->mutual_diffdata_addr);
	LE32_TO_CPU(misc->mutual_refdata_addr);
	LE32_TO_CPU(misc->self_rawdata_addr);
	LE32_TO_CPU(misc->self_diffdata_addr);
	LE32_TO_CPU(misc->self_refdata_addr);
	LE32_TO_CPU(misc->iq_rawdata_addr);
	LE32_TO_CPU(misc->iq_refdata_addr);
	LE32_TO_CPU(misc->im_rawdata_addr);
	LE16_TO_CPU(misc->im_readata_len);
	LE32_TO_CPU(misc->noise_rawdata_addr);
	LE16_TO_CPU(misc->noise_rawdata_len);
	LE32_TO_CPU(misc->stylus_rawdata_addr);
	LE16_TO_CPU(misc->stylus_rawdata_len);
	LE32_TO_CPU(misc->noise_data_addr);
	LE32_TO_CPU(misc->esd_addr);

	return 0;
}

static void print_ic_info(struct goodix_ic_info *ic_info)
{
	struct goodix_ic_info_version *version = &ic_info->version;
	struct goodix_ic_info_feature *feature = &ic_info->feature;
	struct goodix_ic_info_param *parm = &ic_info->parm;
	struct goodix_ic_info_misc *misc = &ic_info->misc;

	ts_info("ic_info_length:                %d",
		ic_info->length);
	ts_info("info_customer_id:              0x%01X",
		version->info_customer_id);
	ts_info("info_version_id:               0x%01X",
		version->info_version_id);
	ts_info("ic_die_id:                     0x%01X",
		version->ic_die_id);
	ts_info("ic_version_id:                 0x%01X",
		version->ic_version_id);
	ts_info("config_id:                     0x%4X",
		version->config_id);
	ts_info("config_version:                0x%01X",
		version->config_version);
	ts_info("frame_data_customer_id:        0x%01X",
		version->frame_data_customer_id);
	ts_info("frame_data_version_id:         0x%01X",
		version->frame_data_version_id);
	ts_info("touch_data_customer_id:        0x%01X",
		version->touch_data_customer_id);
	ts_info("touch_data_version_id:         0x%01X",
		version->touch_data_version_id);

	ts_info("freqhop_feature:               0x%04X",
		feature->freqhop_feature);
	ts_info("calibration_feature:           0x%04X",
		feature->calibration_feature);
	ts_info("gesture_feature:               0x%04X",
		feature->gesture_feature);
	ts_info("side_touch_feature:            0x%04X",
		feature->side_touch_feature);
	ts_info("stylus_feature:                0x%04X",
		feature->stylus_feature);

	ts_info("Drv*Sen,Button,Force num:      %d x %d, %d, %d",
		parm->drv_num, parm->sen_num,
		parm->button_num, parm->force_num);

	ts_info("Cmd:                           0x%04X, %d",
		misc->cmd_addr, misc->cmd_max_len);
	ts_info("Cmd-Reply:                     0x%04X, %d",
		misc->cmd_reply_addr, misc->cmd_reply_len);
	ts_info("FW-State:                      0x%04X, %d",
		misc->fw_state_addr, misc->fw_state_len);
	ts_info("FW-Buffer:                     0x%04X, %d",
		misc->fw_buffer_addr, misc->fw_buffer_max_len);
	ts_info("Touch-Data:                    0x%04X, %d",
		misc->touch_data_addr, misc->touch_data_head_len);
	ts_info("point_struct_len:              %d",
		misc->point_struct_len);
	ts_info("mutual_rawdata_addr:           0x%04X",
		misc->mutual_rawdata_addr);
	ts_info("mutual_diffdata_addr:          0x%04X",
		misc->mutual_diffdata_addr);
	ts_info("self_rawdata_addr:             0x%04X",
		misc->self_rawdata_addr);
	ts_info("self_diffdata_addr:            0x%04X",
		misc->self_diffdata_addr);
	ts_info("stylus_rawdata_addr:           0x%04X, %d",
		misc->stylus_rawdata_addr, misc->stylus_rawdata_len);
	ts_info("esd_addr:                      0x%04X",
		misc->esd_addr);
}

static int brl_get_ic_info(struct goodix_ts_core *cd,
	struct goodix_ic_info *ic_info)
{
	int ret, i;
	u16 length = 0;
	u32 ic_addr;
	u8 afe_data[GOODIX_IC_INFO_MAX_LEN] = {0};
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A)
		ic_addr = GOODIX_IC_INFO_ADDR_BRA;
	else
		ic_addr = GOODIX_IC_INFO_ADDR;

	for (i = 0; i < GOODIX_RETRY_3; i++) {
		ret = hw_ops->read(cd, ic_addr,
				   (u8 *)&length, sizeof(length));
		if (ret) {
			ts_info("failed get ic info length, %d", ret);
			usleep_range(5000, 5100);
			continue;
		}
		length = le16_to_cpu(length);
		if (length >= GOODIX_IC_INFO_MAX_LEN) {
			ts_info("invalid ic info length %d, retry %d",
				length, i);
			continue;
		}

		ret = hw_ops->read(cd, ic_addr, afe_data, length);
		if (ret) {
			ts_info("failed get ic info data, %d", ret);
			usleep_range(5000, 5100);
			continue;
		}
		/* judge whether the data is valid */
		if (is_risk_data((const uint8_t *)afe_data, length)) {
			ts_info("fw info data invalid");
			usleep_range(5000, 5100);
			continue;
		}
		if (checksum_cmp((const uint8_t *)afe_data,
					length, CHECKSUM_MODE_U8_LE)) {
			ts_info("fw info checksum error!");
			usleep_range(5000, 5100);
			continue;
		}
		break;
	}
	if (i == GOODIX_RETRY_3) {
		ts_err("failed get ic info");
		return -EINVAL;
	}

	ret = convert_ic_info(ic_info, afe_data);
	if (ret) {
		ts_err("convert ic info encounter error");
		return ret;
	}

	print_ic_info(ic_info);

	/* check some key info */
	if (!ic_info->misc.cmd_addr || !ic_info->misc.fw_buffer_addr ||
	    !ic_info->misc.touch_data_addr) {
		ts_err("cmd_addr fw_buf_addr and touch_data_addr is null");
		return -EINVAL;
	}

	return 0;
}

#define GOODIX_ESD_TICK_WRITE_DATA	0xAA
static int brl_esd_check(struct goodix_ts_core *cd)
{
	int ret;
	u32 esd_addr;
	u8 esd_value;

	if (!cd->ic_info.misc.esd_addr)
		return 0;

	esd_addr = cd->ic_info.misc.esd_addr;
	ret = cd->hw_ops->read(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed get esd value, %d", ret);
		return ret;
	}

	if (esd_value == GOODIX_ESD_TICK_WRITE_DATA) {
		ts_err("esd check failed, 0x%x", esd_value);
		return -EINVAL;
	}
	esd_value = GOODIX_ESD_TICK_WRITE_DATA;
	ret = cd->hw_ops->write(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed refrash esd value");
		return ret;
	}
	return 0;
}

#define IRQ_EVENT_HEAD_LEN			8
#define BYTES_PER_POINT				8
#define COOR_DATA_CHECKSUM_SIZE		2

#define GOODIX_TOUCH_EVENT			0x80
#define GOODIX_REQUEST_EVENT		0x40
#define GOODIX_GESTURE_EVENT		0x20
#define GOODIX_FP_EVENT				0x08
#define POINT_TYPE_STYLUS_HOVER		0x01
#define POINT_TYPE_STYLUS			0x03

extern int is_recovery;

static void goodix_parse_finger(struct goodix_touch_data *touch_data,
				u8 *buf, int touch_num)
{
	unsigned int id = 0, x = 0, y = 0, w = 0;
	u8 *coor_data;
	int i;

	coor_data = &buf[IRQ_EVENT_HEAD_LEN];
	for (i = 0; i < touch_num; i++) {
		id = (coor_data[0] >> 4) & 0x0F;
		if (id >= GOODIX_MAX_TOUCH) {
			ts_info("invalid finger id =%d", id);
			touch_data->touch_num = 0;
			return;
		}
		x = le16_to_cpup((__le16 *)(coor_data + 2));
		y = le16_to_cpup((__le16 *)(coor_data + 4));
		w = le16_to_cpup((__le16 *)(coor_data + 6));
		touch_data->coords[id].status = TS_TOUCH;
		if (is_recovery == 4) {
			touch_data->coords[id].x = x / ZTE_GOODIX_SR;//16
			touch_data->coords[id].y = y / ZTE_GOODIX_SR;
			touch_data->coords[id].w = w / ZTE_GOODIX_SR;
		} else {
			touch_data->coords[id].x = x;
			touch_data->coords[id].y = y;
			touch_data->coords[id].w = w;
		}
		coor_data += BYTES_PER_POINT;
	}
	touch_data->touch_num = touch_num;
}

static unsigned int goodix_pen_btn_code[] = {BTN_STYLUS, BTN_STYLUS2};
static void goodix_parse_pen(struct goodix_pen_data *pen_data,
	u8 *buf, int touch_num)
{
	unsigned int id = 0;
	u8 cur_key_map = 0;
	u8 *coor_data;
	int16_t x_angle, y_angle;
	int i;

	pen_data->coords.tool_type = BTN_TOOL_PEN;

	if (touch_num) {
		pen_data->coords.status = TS_TOUCH;
		coor_data = &buf[IRQ_EVENT_HEAD_LEN];

		id = (coor_data[0] >> 4) & 0x0F;
		pen_data->coords.x = le16_to_cpup((__le16 *)(coor_data + 2));
		pen_data->coords.y = le16_to_cpup((__le16 *)(coor_data + 4));
		pen_data->coords.p = le16_to_cpup((__le16 *)(coor_data + 6));
		x_angle = le16_to_cpup((__le16 *)(coor_data + 8));
		y_angle = le16_to_cpup((__le16 *)(coor_data + 10));
		pen_data->coords.tilt_x = x_angle / 100;
		pen_data->coords.tilt_y = y_angle / 100;
	} else {
		pen_data->coords.status = TS_RELEASE;
	}

	cur_key_map = (buf[3] & 0x0F) >> 1;
	for (i = 0; i < GOODIX_MAX_PEN_KEY; i++) {
		pen_data->keys[i].code = goodix_pen_btn_code[i];
		if (!(cur_key_map & (1 << i)))
			continue;
		pen_data->keys[i].status = TS_TOUCH;
	}
}

static int goodix_touch_handler(struct goodix_ts_core *cd,
				struct goodix_ts_event *ts_event,
				u8 *pre_buf, u32 pre_buf_len)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_touch_data *touch_data = &ts_event->touch_data;
	struct goodix_pen_data *pen_data = &ts_event->pen_data;
	static u8 buffer[IRQ_EVENT_HEAD_LEN +
			 BYTES_PER_POINT * GOODIX_MAX_TOUCH + 2];
	u8 touch_num = 0;
	int ret = 0;
	u8 point_type = 0;
	static u8 pre_finger_num;
	static u8 pre_pen_num;

	/* clean event buffer */
	memset(ts_event, 0, sizeof(*ts_event));
	/* copy pre-data to buffer */
	memcpy(buffer, pre_buf, pre_buf_len);

	touch_num = buffer[2] & 0x0F;

	if (touch_num > GOODIX_MAX_TOUCH) {
		ts_debug("invalid touch num %d", touch_num);
		return -EINVAL;
	}

	if (unlikely(touch_num > 2)) {
		ret = hw_ops->read(cd,
				misc->touch_data_addr + pre_buf_len,
				&buffer[pre_buf_len],
				(touch_num - 2) * BYTES_PER_POINT);
		if (ret) {
			ts_debug("failed get touch data");
			return ret;
		}
	}

	/* read done */
	hw_ops->after_event_handler(cd);

	if (touch_num > 0) {
		point_type = buffer[IRQ_EVENT_HEAD_LEN] & 0x0F;
		if (point_type == POINT_TYPE_STYLUS ||
				point_type == POINT_TYPE_STYLUS_HOVER) {
			ret = checksum_cmp(&buffer[IRQ_EVENT_HEAD_LEN],
					BYTES_PER_POINT * 2 + 2,
					CHECKSUM_MODE_U8_LE);
			if (ret) {
				ts_debug("touch data checksum error");
				ts_debug("data:%*ph", BYTES_PER_POINT * 2 + 2,
						&buffer[IRQ_EVENT_HEAD_LEN]);
				return -EINVAL;
			}
		} else {
			ret = checksum_cmp(&buffer[IRQ_EVENT_HEAD_LEN],
					touch_num * BYTES_PER_POINT + 2,
					CHECKSUM_MODE_U8_LE);
			if (ret) {
				ts_debug("touch data checksum error");
				ts_debug("data:%*ph",
						touch_num * BYTES_PER_POINT + 2,
						&buffer[IRQ_EVENT_HEAD_LEN]);
				return -EINVAL;
			}
		}
	}

	ts_event->fp_flag = pre_buf[0] & GOODIX_FP_EVENT;

	if (touch_num > 0 && (point_type == POINT_TYPE_STYLUS
				|| point_type == POINT_TYPE_STYLUS_HOVER)) {
		/* stylus info */
		if (pre_finger_num) {
			ts_event->event_type = EVENT_TOUCH;
			goodix_parse_finger(touch_data, buffer, 0);
			pre_finger_num = 0;
		} else {
			pre_pen_num = 1;
			ts_event->event_type = EVENT_PEN;
			goodix_parse_pen(pen_data, buffer, touch_num);
		}
	} else {
		/* finger info */
		if (pre_pen_num) {
			ts_event->event_type = EVENT_PEN;
			goodix_parse_pen(pen_data, buffer, 0);
			pre_pen_num = 0;
		} else {
			ts_event->event_type = EVENT_TOUCH;
			goodix_parse_finger(touch_data,
					    buffer, touch_num);
			pre_finger_num = touch_num;
		}
	}

	/* process custom info */
	if (buffer[3] & 0x01)
		ts_debug("TODO add custom info process function");

	return 0;
}

static int brl_event_handler(struct goodix_ts_core *cd,
			 struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	int pre_read_len;
	u8 pre_buf[32];
	u8 event_status;
	int ret;

	pre_read_len = IRQ_EVENT_HEAD_LEN +
		BYTES_PER_POINT * 2 + COOR_DATA_CHECKSUM_SIZE;
	ret = hw_ops->read(cd, misc->touch_data_addr,
			   pre_buf, pre_read_len);
	if (ret) {
		ts_debug("failed get event head data");
		return ret;
	}

	if (pre_buf[0] == 0x00) {
		ts_debug("invalid touch head");
		return -EINVAL;
	}

	if (checksum_cmp(pre_buf, IRQ_EVENT_HEAD_LEN, CHECKSUM_MODE_U8_LE)) {
		ts_debug("touch head checksum err[%*ph]",
				IRQ_EVENT_HEAD_LEN, pre_buf);
		return -EINVAL;
	}

	event_status = pre_buf[0];
	if (event_status & GOODIX_TOUCH_EVENT){
#if 0
		if (event_status & GOODIX_FP_EVENT) {
			report_ufp_uevent(UFP_FP_DOWN);
		} else {
			report_ufp_uevent(UFP_FP_UP);
		}
#endif
		return goodix_touch_handler(cd, ts_event,
					    pre_buf, pre_read_len);
	}

	if (event_status & GOODIX_REQUEST_EVENT) {
		ts_event->event_type = EVENT_REQUEST;
		if (pre_buf[2] == BRL_REQUEST_CODE_CONFIG)
			ts_event->request_code = REQUEST_TYPE_CONFIG;
		else if (pre_buf[2] == BRL_REQUEST_CODE_RESET)
			ts_event->request_code = REQUEST_TYPE_RESET;
		else
			ts_debug("unsupported request code 0x%x", pre_buf[2]);
	}

	if (event_status & GOODIX_GESTURE_EVENT) {
		ts_event->event_type = EVENT_GESTURE;
		ts_event->gesture_type = pre_buf[4];
		memcpy(ts_event->gesture_data, &pre_buf[8],
				GOODIX_GESTURE_DATA_LEN);
	}
	/* read done */
	hw_ops->after_event_handler(cd);

	return 0;
}

static int brl_after_event_handler(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	u8 sync_clean = 0;

	if (cd->tools_ctrl_sync)
		return 0;
	return hw_ops->write(cd, misc->touch_data_addr,
		&sync_clean, 1);
}

static int brld_get_framedata(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	int ret;
	unsigned char val;
	int retry = 20;
	// struct frame_head *frame_head;
	unsigned char frame_buf[GOODIX_MAX_FRAMEDATA_LEN];
	unsigned char *cur_ptr;
	unsigned int flag_addr = cd->ic_info.misc.frame_data_addr;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit!");
		return ret;
	}

	while (retry--) {
		usleep_range(2000, 2100);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("framedata is not ready val:0x%02x, exit!", val);
		return -EINVAL;
	}

	ret = brl_read(cd, flag_addr, frame_buf, GOODIX_MAX_FRAMEDATA_LEN);
	if (ret < 0) {
		ts_err("read frame data failed");
		return ret;
	}

	if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len,
			CHECKSUM_MODE_U8_LE)) {
		ts_err("frame head checksum error");
		return -EINVAL;
	}

	// frame_head = (struct frame_head *)frame_buf;
	// if (checksum_cmp(frame_buf, frame_head->cur_frame_len,
	// 		CHECKSUM_MODE_U16_LE)) {
	// 	ts_err("frame body checksum error");
	// 	return -EINVAL;
	// }
	cur_ptr = frame_buf;
	cur_ptr += cd->ic_info.misc.frame_data_head_len;
	cur_ptr += cd->ic_info.misc.fw_attr_len;
	cur_ptr += cd->ic_info.misc.fw_log_len;
	memcpy((u8 *)(info->buff + info->used_size), cur_ptr + 8,
			tx * rx * 2);

	return 0;
}

static int brld_get_cap_data(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	struct goodix_ts_cmd temp_cmd;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	int size = tx * rx;
	int ret;

	/* disable irq & close esd */
	brl_irq_enbale(cd, false);
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

	info->buff[0] = rx;
	info->buff[1] = tx;
	info->used_size = 2;

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x81;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("report rawdata failed, exit!");
		goto exit;
	}

	ret = brld_get_framedata(cd, info);
	if (ret < 0) {
		ts_err("brld get rawdata failed");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x82;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("report diffdata failed, exit!");
		goto exit;
	}

	ret = brld_get_framedata(cd, info);
	if (ret < 0) {
		ts_err("brld get diffdata failed");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

exit:
	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0;
	temp_cmd.len = 5;
	brl_send_cmd(cd, &temp_cmd);
	/* enable irq & esd */
	brl_irq_enbale(cd, true);
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	return ret;
}

#define GOODIX_CMD_RAWDATA	2
#define GOODIX_CMD_COORD	0
static int brl_get_capacitance_data(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	int ret;
	int retry = 20;
	struct goodix_ts_cmd temp_cmd;
	u32 flag_addr = cd->ic_info.misc.touch_data_addr;
	u32 raw_addr = cd->ic_info.misc.mutual_rawdata_addr;
	u32 diff_addr = cd->ic_info.misc.mutual_diffdata_addr;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	int size = tx * rx;
	u8 val;

	if (!info) {
		ts_err("input null ptr");
		return -EIO;
	}

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D ||
			cd->bus->ic_type == IC_TYPE_NOTTINGHAM)
		return brld_get_cap_data(cd, info);

	/* disable irq & close esd */
	brl_irq_enbale(cd, false);
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

    /* switch rawdata mode */
	temp_cmd.cmd = GOODIX_CMD_RAWDATA;
	temp_cmd.len = 4;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch rawdata mode failed, exit!");
		goto exit;
	}

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit!");
		goto exit;
	}

	while (retry--) {
		usleep_range(5000, 5100);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("rawdata is not ready val:0x%02x, exit!", val);
		goto exit;
	}

	/* obtain rawdata & diff_rawdata */
	info->buff[0] = rx;
	info->buff[1] = tx;
	info->used_size = 2;

	ret = brl_read(cd, raw_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian raw_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	ret = brl_read(cd, diff_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian diff_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

exit:
	/* switch coor mode */
	temp_cmd.cmd = GOODIX_CMD_COORD;
	temp_cmd.len = 4;
	brl_send_cmd(cd, &temp_cmd);
	/* clean touch event flag */
	val = 0;
	brl_write(cd, flag_addr, &val, 1);
	/* enable irq & esd */
	brl_irq_enbale(cd, true);
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	return ret;
}

#if 0
int zte_brl_set_hori(struct goodix_ts_core *cd, int ori)
{
	struct goodix_ts_cmd cmd;

	if (mutex_trylock(&cd->mutex_cmd)) {
		cmd.cmd = 0x17;
		cmd.len = 0x6;
		cmd.data[0] = ori;
		cmd.data[1] = 0x40;
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send gesture cmd", __func__);
			mutex_unlock(&cd->mutex_cmd);
			return -EIO;
		}
		ts_info("%s: set success %d", __func__, ori);
		mutex_unlock(&cd->mutex_cmd);
	} else {
		ts_info("%s: ingnore it", __func__);
	}

	return 0;
}

int zte_brl_set_verti(struct goodix_ts_core *cd)
{
	struct goodix_ts_cmd cmd;

	if (mutex_trylock(&cd->mutex_cmd)) {
		cmd.cmd = 0x18;
		cmd.len = 0x6;
		cmd.data[0] = 0x1C;
		cmd.data[1] = 0x40;
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send gesture cmd", __func__);
			mutex_unlock(&cd->mutex_cmd);
			return -EIO;
		}
		ts_info("%s: set success", __func__);
		mutex_unlock(&cd->mutex_cmd);
	} else {
		ts_info("%s: ingnore it", __func__);
	}

	return 0;
}

int zte_set_display_rotation(struct goodix_ts_core *cd, int mrotation)
{
	struct goodix_ts_cmd cmd;
	int level = cd->ztec.rotation_limit_level;

	cmd.cmd = 0x17;
	cmd.len = 0x6;
	switch (mrotation) {
		case mRotatin_0:
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x00;
			break;
		case mRotatin_90:
			if (level == rotation_limit_level_0) {
				cmd.data[0] = 0x40;
				cmd.data[1] = 0x00;
				ts_info("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
				cmd.data[0] = 0x40;
				cmd.data[1] = 0x40;
				ts_info("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
				cmd.data[0] = 0x40;
				cmd.data[1] = 0x80;
				ts_info("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
				cmd.data[0] = 0x40;
				cmd.data[1] = 0xc0;
				ts_info("success in rotation_limit_level_3");
			} else {
				ts_err("level is error!", level);
			}
			break;
		case mRotatin_270:
			if (level == rotation_limit_level_0) {
				cmd.data[0] = 0x80;
				cmd.data[1] = 0x00;
				ts_info("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
				cmd.data[0] = 0x80;
				cmd.data[1] = 0x40;
				ts_info("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
				cmd.data[0] = 0x80;
				cmd.data[1] = 0x80;
				ts_info("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
				cmd.data[0] = 0x80;
				cmd.data[1] = 0xc0;
				ts_info("success in rotation_limit_level_3");
			} else {
				ts_err("level is error!", level);
			}
			break;
		default:
			ts_err("mrotation is error!", mrotation);
			break;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("%s: failed send cmd", __func__);
		return -EIO;
	}
	usleep_range(5000, 5100);
	if (!cd->ztec.is_play_game) {
		if (cd->hw_ops->set_tp_report_rate(cd, cd->ztec.tp_report_rate)) {
			ts_err("set report rate mode failed!");
			return -EIO;
		}
	}
	return 0;
}

int zte_brl_gesture(struct goodix_ts_core *cd,  int status)
{
	struct goodix_ts_cmd cmd;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A) {
		cmd.cmd = GOODIX_GESTURE_CMD_BA;
		cmd.len = 6;
		switch (status) {
		case (ZTE_GOODIX_DOU_TAP | ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
			cmd.data[0] = GOODIX_GESTURE_DATA_START;
			cmd.data[1] = GOODIX_GESTURE_DATA_START;
			ts_info("single_double_finger");
			break;
		case (ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
			cmd.data[0] = GOODIX_GESTURE_DATA_CLOSE_DOUBLE;
			cmd.data[1] = GOODIX_GESTURE_DATA_START;
			ts_info("single_finger");
			break;
		case (ZTE_GOODIX_DOU_TAP):
			cmd.data[0] = GOODIX_GESTURE_DATA_START;
			cmd.data[1] = GOODIX_GESTURE_DATA_OPEN_DOUBLE;
			ts_info("double");
			break;
		default:
			ts_err("status is error!", status);
			/*is_init_cmd = 0;*/
			return 0;
		}
	} else if  (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		cmd.cmd = GOODIX_GESTURE_CMD;
		cmd.len = 6;
		switch (status) {
		case (ZTE_GOODIX_DOU_TAP | ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
		case (ZTE_GOODIX_DOU_TAP | ZTE_GOODIX_FP):
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x00;
			cmd.data[3] = 0xac;
			ts_info("single_double_finger");
			break;
		case (ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_DOU_TAP):
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x20;
			cmd.data[3] = 0xcc;
			ts_info("single_double");
			break;
		case (ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
		case (ZTE_GOODIX_SIN_TAP):
		case (ZTE_GOODIX_FP):
			cmd.data[0] = 0x80;
			cmd.data[1] = 0x00;
			ts_info("single_finger");
			break;
		case (ZTE_GOODIX_DOU_TAP):
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x30;
			ts_info("double");
			break;
		default:
			ts_err("status is error!", status);
			/*is_init_cmd = 0;*/
			return 0;
		}
	} else {
		ts_err("%s: failed send cmd", __func__);
		return 0;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd))
		ts_err("failed send gesture cmd");

	return 0;
}

int zte_onekey_switch(struct goodix_ts_core *cd, int onekey_type)
{
	struct goodix_ts_cmd cmd;

	ts_info("onekey  enable is %d", onekey_type ? 0 : 1);
	if (cd->bus->ic_type == IC_TYPE_BERLIN_A) {
		cmd.cmd = 0x14;
		cmd.len = 0x6;
		cmd.data[0] = 0;
		cmd.data[1] = onekey_type;
	} else if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		cmd.cmd = 0xC3;
		cmd.len = 0x5;
		if (onekey_type) {
			cmd.data[0] = 0x1;
			cmd.data[1] = 0xc9;
		} else {
			cmd.data[0] = 0x0;
			cmd.data[1] = 0xc8;
		}
	} else {
		ts_err("%s: failed send cmd", __func__);
		return 0;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd))
		ts_err("failed send gesture cmd");

	return 0;
}

int report_rate_120HZ(struct goodix_ts_core *cd, int mark)
{
	struct goodix_ts_cmd cmd;
	if (mark == 1) {
		ts_info("%s success in 120Hz", __func__);
		cmd.len = 05;
		cmd.cmd = 0x9d;
		cmd.data[0] = 0x00;/*120Hz*/
		cmd.data[1] = 0xA2;
		cmd.data[2] = 0x00;
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send cmd", __func__);
			return -EIO;
		}
	}
	return 0;
}
int report_rate_240HZ(struct goodix_ts_core *cd, int mark)
{
	struct goodix_ts_cmd cmd;
	if (mark == 1) {
		ts_info("%s success in 240Hz", __func__);
		cmd.len = 05;
		cmd.cmd = 0x9d;
		cmd.data[0] = 0x01;/*240Hz*/
		cmd.data[1] = 0xA3;
		cmd.data[2] = 0x00;
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send cmd", __func__);
			return -EIO;
		}
	}
	return 0;
}
int report_rate_480HZ(struct goodix_ts_core *cd, int mark)
{
	struct goodix_ts_cmd cmd;

	cmd.len = 06;
	cmd.cmd = 0xC0;

	if (mark == 1) {
		ts_info("%s success in 480Hz", __func__);
		cmd.data[0] = 0x01;/*480Hz*/
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC7;
	} else {
		ts_info("%s success exit 480HZ", __func__);
		cmd.data[0] = 0x00;/*exit480Hz*/
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC6;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("%s: failed send cmd", __func__);
		return -EIO;
	}
	return 0;
}
int report_rate_960HZ(struct goodix_ts_core *cd, int mark)
{
	struct goodix_ts_cmd cmd;

	cmd.len = 06;
	cmd.cmd = 0xC1;

	if (mark == 1) {
		ts_info("%s success in 960Hz", __func__);
		cmd.data[0] = 0x01;/*960Hz*/
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC8;
	} else {
		ts_info("%s success exit 960HZ", __func__);
		cmd.data[0] = 0x00;/*exit960Hz*/
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC7;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("%s: failed send cmd", __func__);
		return -EIO;
	}
	return 0;
}

int zte_tp_set_report_rate(struct goodix_ts_core *cd, int enable)
{
	static int current_report_mode = tp_freq_240Hz;
	int ret = 0;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		switch (enable) {
			case tp_freq_120Hz:
				if (current_report_mode == tp_freq_960Hz) {
					ret = report_rate_960HZ(cd, 0);
					if (ret < 0)
						return ret;
					ret = report_rate_480HZ(cd, 0);
					if (ret < 0)
						return ret;
				}
				if (current_report_mode == tp_freq_480Hz) {
					ret = report_rate_480HZ(cd, 0);
					if (ret < 0)
						return ret;
				}
				ret = report_rate_120HZ(cd, 1);
				if (ret < 0)
					return ret;
				current_report_mode = tp_freq_120Hz;
				break;
			case tp_freq_240Hz:
				if (current_report_mode == tp_freq_960Hz) {
					ret = report_rate_960HZ(cd, 0);
					if (ret < 0)
						return ret;
					ret = report_rate_480HZ(cd, 0);
					if (ret < 0)
						return ret;
				}
				if (current_report_mode == tp_freq_480Hz) {
					ret = report_rate_480HZ(cd, 0);
					if (ret < 0)
						return ret;
				}
				ret = report_rate_240HZ(cd, 1);
				if (ret < 0)
					return ret;
				current_report_mode = tp_freq_240Hz;
				break;
			case tp_freq_480Hz:
				if (current_report_mode == tp_freq_960Hz) {
					ret = report_rate_960HZ(cd, 0);
					ts_info("%s success in 480Hz", __func__);
					if (ret < 0)
						return ret;
				} else {
					ret = report_rate_480HZ(cd, 1);
					if (ret < 0)
						return ret;
				}
				current_report_mode = tp_freq_480Hz;
				break;
			case tp_freq_960Hz:
				if (current_report_mode == tp_freq_480Hz) {
					ret = report_rate_960HZ(cd, 1);
					if (ret < 0)
						return ret;
				} else {
					ret = report_rate_480HZ(cd, 1);
					if (ret < 0)
						return ret;
					ret = report_rate_960HZ(cd, 1);
					if (ret < 0)
						return ret;
				}
				current_report_mode = tp_freq_960Hz;
				break;
			default:
				ts_err("%s: enable not support", __func__);
				return 0;
		}
	 } else {
		ts_err("%s: not support", __func__);
		return 0;
	}

	return 0;
}


int zte_sensibility_level(struct goodix_ts_core *cd, u8 enable)
{
	struct goodix_ts_cmd cmd;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		cmd.len = 06;
		cmd.cmd = 0x27;
		switch (enable) {
		case sensibility_level_0:
			ts_info("%s success in sensibility_level_0", __func__);
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case sensibility_level_1:
			ts_info("%s success in sensibility_level_1", __func__);
			cmd.data[0] = 0x01;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case sensibility_level_2:
			ts_info("%s success in sensibility_level_2", __func__);
			cmd.data[0] = 0x02;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case sensibility_level_3:
			ts_info("%s success in sensibility_level_3", __func__);
			cmd.data[0] = 0x03;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case sensibility_level_4:
			ts_info("%s success in sensibility_level_4", __func__);
			cmd.data[0] = 0x04;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		default:
			ts_err("%s: enable not support", __func__);
			return 0;
		}
	}else {
		ts_err("%s: not support", __func__);
		return 0;
	}
	return 0;
}

int zte_follow_hand_level(struct goodix_ts_core *cd, int enable)
{
	struct goodix_ts_cmd cmd;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		cmd.len = 06;
		cmd.cmd = 0x28;
		switch (enable) {
		case follow_hand_level_0:
			ts_info("%s success in follow_hand_level_0", __func__);
			cmd.data[0] = 0x00;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case follow_hand_level_1:
			ts_info("%s success in follow_hand_level_1", __func__);
			cmd.data[0] = 0x01;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case follow_hand_level_2:
			ts_info("%s success in follow_hand_level_2", __func__);
			cmd.data[0] = 0x02;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case follow_hand_level_3:
			ts_info("%s success in follow_hand_level_3", __func__);
			cmd.data[0] = 0x03;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		case follow_hand_level_4:
			ts_info("%s success in follow_hand_level_4", __func__);
			cmd.data[0] = 0x04;
			cmd.data[1] = 0x00;
			if (cd->hw_ops->send_cmd(cd, &cmd)) {
				ts_err("%s: failed send cmd", __func__);
				return -EIO;
			}
			break;
		default:
			ts_err("%s: enable not support", __func__);
			return 0;
		}
	}else {
		ts_err("%s: not support", __func__);
		return 0;
	}
	return 0;
}

int enable_game_mode(struct goodix_ts_core *cd, int enable)
{
	struct goodix_ts_cmd cmd;

	cmd.len = 06;
	cmd.cmd = 0xC2;

	if (enable) {
		cmd.data[0] = 0x01;
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC9;
	} else {
		cmd.data[0] = 0x00;
		cmd.data[1] = 0x00;
		cmd.data[2] = 0xC8;
	}
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("%s: failed send cmd", __func__);
		return -EIO;
	}

	return 0;
}

int zte_play_game(struct goodix_ts_core *cd, int enable)
{
	int ret = 0;

	if (enable) {
		ret = enable_game_mode(cd, enable);
		if (ret < 0)
			return ret;
	}else {
		ret = zte_sensibility_level(cd, 2);
		if (ret < 0)
			return ret;
		ret = zte_follow_hand_level(cd, 2);
		if (ret < 0)
			return ret;
		ret = enable_game_mode(cd, enable);
		if (ret < 0)
			return ret;
	}

	return 0;
}

#ifdef GOODIX_USB_DETECT_GLOBAL
int zte_brl_enter_charger(struct goodix_ts_core *cd)
{
	struct goodix_ts_cmd cmd;

	if (cd->init_stage < CORE_INIT_STAGE2 || atomic_read(&cd->suspended)) {
		ts_err("%s:error, change set in before firmware update", __func__);
		return 0;
	}
	if (mutex_trylock(&cd->mutex_cmd)) {
		if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
			cmd.cmd = 0xAF;
			cmd.len = 0x5;
			cmd.data[0] = 0x01;
		} else {
			cmd.cmd = 0x10;
			cmd.len = 0x4;
		}
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send gesture cmd", __func__);
			mutex_unlock(&cd->mutex_cmd);
			return -EIO;
		}
		ts_info("%s: set success", __func__);
		mutex_unlock(&cd->mutex_cmd);
	} else {
		ts_info("%s: ingnore it", __func__);
	}

	return 0;
}

int zte_brl_leave_charger(struct goodix_ts_core *cd)
{
	struct goodix_ts_cmd cmd;

	if (mutex_trylock(&cd->mutex_cmd)) {
		if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
			cmd.cmd = 0xAF;
			cmd.len = 0x5;
			cmd.data[0] = 0x00;
		} else {
			cmd.cmd = 0x11;
			cmd.len = 0x4;
		}
		if (cd->hw_ops->send_cmd(cd, &cmd)) {
			ts_err("%s: failed send gesture cmd", __func__);
			mutex_unlock(&cd->mutex_cmd);
			return -EIO;
		}
		ts_info("%s: set success", __func__);
		mutex_unlock(&cd->mutex_cmd);
	} else {
		ts_info("%s: ingnore it", __func__);
	}

	return 0;
}
#endif
#endif

static struct goodix_ts_hw_ops brl_hw_ops = {
	.power_on = brl_power_on,
	.resume = brl_resume,
	.suspend = brl_suspend,
	.gesture = brl_gesture,
	.reset = brl_reset,
	.irq_enable = brl_irq_enbale,
	.read = brl_read,
	.write = brl_write,
	.read_flash = brl_flash_read,
	.send_cmd = brl_send_cmd,
	.send_config = brl_send_config,
	.read_config = brl_read_config,
	.read_version = brl_read_version,
	.get_ic_info = brl_get_ic_info,
	.esd_check = brl_esd_check,
	.event_handler = brl_event_handler,
	.after_event_handler = brl_after_event_handler,
	.get_capacitance_data = brl_get_capacitance_data,
#if 0
	.set_horizontal_panel = zte_brl_set_hori,
	.set_vertical_panel = zte_brl_set_verti,
	.set_display_rotation = zte_set_display_rotation,
	.set_onekey_switch = zte_onekey_switch,
	.set_tp_report_rate = zte_tp_set_report_rate,
	.set_sensibility = zte_sensibility_level,
	.set_follow_hand_level = zte_follow_hand_level,
	.set_zte_play_game = zte_play_game,
#ifdef GOODIX_USB_DETECT_GLOBAL
	.set_enter_charger = zte_brl_enter_charger,
	.set_leave_charger = zte_brl_leave_charger,
#endif
#endif
};

struct goodix_ts_hw_ops *goodix_get_hw_ops(void)
{
	return &brl_hw_ops;
}
