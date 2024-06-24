/***********************
 * file : tpd_fw.c
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include "ztp_core.h"
#include "ztp_common.h"

struct ztp_device *tpd_cdev = NULL;
struct proc_dir_entry *tpd_proc_dir = NULL;

struct tp_ic_vendor_info tp_ic_vendor_info_l[] = {
	{TS_CHIP_SYNAPTICS, "synaptics"},
	{TS_CHIP_FOCAL, "focal"},
	{TS_CHIP_GOODIX, "goodix"	},
	{TS_CHIP_HIMAX, "himax"},
	{TS_CHIP_NOVATEK, "novatek"},
	{TS_CHIP_ILITEK, "ilitek"},
	{TS_CHIP_TLSC, "tlsc"},
	{TS_CHIP_CHIPONE, "chipone"},
	{TS_CHIP_GCORE, "galaxycore"},
	{TS_CHIP_OMNIVISION, "omnivision"},
	{TS_CHIP_MAX, "Unknown"},
};

struct ztp_algo_info ztp_algo_info_l[] = {
	{zte_algo_enable, "algo_open"},
	{tp_jitter_check_pixel, "jitter_pixel"},
	{tp_jitter_timer, "jitter_timer"},
	{tp_edge_click_suppression_pixel, "click_pixel"},
	{tp_long_press_enable, "long_press_open"},
	{tp_long_press_timer, "long_press_timer"},
	{tp_long_press_pixel, "long_press_pixel"},
};

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
struct zlog_mod_info zlog_tp_dev = {
	.module_no = ZLOG_MODULE_TP,
	.name = "touchscreen",
	.device_name = "Unknown",
	.ic_name = "Unknown",
	.module_name = "TP",
	.fops = NULL,
};

void tpd_zlog_register(struct ztp_device *cdev)
{
	if (cdev->zlog_client) {
		TPD_ZLOG("ztp zlog already registered, no need register again!");
		return;
	}

	cdev->zlog_client = zlog_register_client(&zlog_tp_dev);
	if (!cdev->zlog_client) {
		TPD_ZLOG("%s zlog register client zlog_tp_dev fail\n", __func__);
	} else {
		cdev->ztp_zlog_buffer = vmalloc(ZLOG_INFO_LEN);
		if (!cdev->ztp_zlog_buffer) {
			TPD_ZLOG("ztp_zlog_buffer");
			memset(cdev->ztp_zlog_buffer, 0, ZLOG_INFO_LEN);
		}
		if (cdev->ztp_probe_fail_chip_id != 0xFF) {
			tpd_print_zlog("tp probe fail, chip id:%d",cdev->ztp_probe_fail_chip_id);
			tpd_zlog_record_notify(TP_PROBE_ERROR_NO);
		}
	}
	cdev->zlog_regisered = true;
}

int tpd_zlog_check(zlog_error_no error_no)
{
	struct ztp_device *cdev = tpd_cdev;
	int ret = 0;

	if ((cdev->zlog_item.count[error_no] > 0)
		&& (jiffies_to_msecs(jiffies - cdev->zlog_item.timer[error_no])) < 60000) {
		TPD_ZLOG("zlog error repeated notify, timer:%d, no:%d",
			jiffies_to_msecs(jiffies - cdev->zlog_item.timer[error_no]) ,error_no);
		ret = -EIO;
	}
	cdev->zlog_item.count[error_no]++;
	return ret;
}

void tpd_zlog_record_notify(zlog_error_no error_no)
{
	struct ztp_device *cdev = tpd_cdev;
	int len = 0;
	unsigned long after_reset_time = 0;

	if(!cdev->zlog_regisered)
		tpd_zlog_register(cdev);

	if ((cdev->zlog_client == NULL) || (cdev->ztp_zlog_buffer == NULL)) {
		TPD_ZLOG("zlog unregistered.\n");
		return;
	}
	after_reset_time = jiffies_to_msecs(jiffies - cdev->tp_reset_timer);
	len = strlen(cdev->ztp_zlog_buffer);
	snprintf(cdev->ztp_zlog_buffer + len, ZLOG_INFO_LEN - len, " IC name: %s,module name:%s, Firmware version: 0x%x",
		zlog_tp_dev.ic_name, zlog_tp_dev.device_name, cdev->ic_tpinfo.firmware_ver);
	switch (error_no) {
	case TP_I2C_R_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;

		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c read err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c read err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_R_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_R_ERROR_NO);
		break;
	case TP_I2C_W_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c write err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c write err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_W_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_W_ERROR_NO);
		break;
	case TP_I2C_R_WARN_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c read warnning,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c read warnning,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_R_WARN_NO);
		break;
	case TP_I2C_W_WARN_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd i2c write warnning,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd i2c write warnning,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_I2C_W_WARN_NO);
		break;
	case TP_SPI_R_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI read err,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI read err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_R_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_R_ERROR_NO);
		break;
	case TP_SPI_W_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI write err,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI write err,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_W_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_W_ERROR_NO);
		break;
	case TP_SPI_R_WARN_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI read warnning,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI read warnning,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_R_WARN_NO);
		break;
	case TP_SPI_W_WARN_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd SPI write warnning,count:%d.%s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd SPI write warnning,count:%d\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SPI_W_WARN_NO);
		break;
	case TP_CRC_ERROR_NO:
		if ((tpd_zlog_check(error_no) < 0) || (after_reset_time < 200))
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd crc check err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd crc check err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_CRC_ERROR_NO);
		break;
	case TP_FW_UPGRADE_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd firmware upgrade err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd firmware upgrade err,count:%d. \n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_REQUEST_FIRMWARE_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd request firmware upgrade err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd request firmware upgrade err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_ESD_CHECK_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd esd check err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd esd check err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		if (cdev->zlog_item.count[error_no] % 10)
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_WARN_NO);
		else
			zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_ESD_CHECK_WARN_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd esd check warnning,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd esd check warnning,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_WARN_NO);
		break;
	case TP_PROBE_ERROR_NO:
		TPD_ZLOG("tpd probe err. %s\n",cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd probe err.\n %s\n",cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_SUSPEND_GESTURE_OPEN_NO:
		TPD_ZLOG("tpd gesture open when suspend. %s\n",cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd tp gesture open when suspend.\n %s\n",cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_SUSPEND_GESTURE_OPEN_NO);
		break;
#ifdef TOUCH_DOWN_UP_ZLOG
	case TP_GHOST_ERROR_NO:
		if (tpd_zlog_check(error_no) < 0)
			break;
		cdev->zlog_item.timer[error_no] = jiffies;
		TPD_ZLOG("tpd ghost err,count:%d. %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_record(cdev->zlog_client, "tpd ghost err,count:%d.\n %s\n",
			cdev->zlog_item.count[error_no], cdev->ztp_zlog_buffer);
		zlog_client_notify(cdev->zlog_client,  ZLOG_TP_GHOST_ERROR_NO);
		break;
#endif
	default:
		break;
	}
	memset(cdev->ztp_zlog_buffer, 0, ZLOG_INFO_LEN);
}

static void zlog_register_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if(!cdev->zlog_regisered)
		tpd_zlog_register(cdev);
}

void zlog_register_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_DELAYED_WORK(&cdev->zlog_register_work, zlog_register_work);

}

void zlog_register_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_delayed_work_sync(&cdev->zlog_register_work);
	vfree(cdev->ztp_zlog_buffer);
	cdev->ztp_zlog_buffer = NULL;
}

void tpd_zlog_init(void)
{
	struct ztp_device *cdev = tpd_cdev;
	int i = 0;

	cdev->ztp_zlog_buffer = NULL;
	cdev->zlog_regisered = false;
	cdev->ztp_probe_fail_chip_id = 0xFF;
	cdev->tp_reset_timer = jiffies;
	for (i = 0; i < TP_ZLOG_ERROR_MAX; i++){
		cdev->zlog_item.timer[i] = jiffies;
	}
}
#endif

int get_tp_algo_item_id(char *buf)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(ztp_algo_info_l); i++) {
		if (strnstr(buf, ztp_algo_info_l[i].ztp_algo_item_name, strlen(buf))) {
			TPD_DMESG("%s: ztp_algo_item_id:%d.\n", __func__, ztp_algo_info_l[i].ztp_algo_item_id);
			return ztp_algo_info_l[i].ztp_algo_item_id;
		}
	}
	return -EIO;
}

int get_tp_chip_id(void)
{
	int i = 0;
	const char *panel_name = NULL;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s:\n", __func__);
	cdev->tp_chip_id = TS_CHIP_MAX;
	panel_name = get_lcd_panel_name();
	TPD_DMESG("%s: panel name %s.\n", __func__, panel_name);
	for (i = 0; i < ARRAY_SIZE(tp_ic_vendor_info_l); i++) {
		if (strnstr(panel_name, tp_ic_vendor_info_l[i].tp_ic_vendor_name, strlen(panel_name))) {
			cdev->tp_chip_id = tp_ic_vendor_info_l[i].tp_chip_id;
			TPD_DMESG("%s: tp_chip_id is 0x%02x.\n", __func__, cdev->tp_chip_id);
			return 0;
		}
	}
	return -EIO;
}

const char *get_lcd_panel_name(void)
{
	const char *panel_name = "Unknown_lcd";

#if 0
	/*panel_name = zte_get_lcd_panel_name();*/
	if (panel_name == NULL)
		panel_name = "Unknown_lcd";
#endif
	return panel_name;
}

void tp_free_tp_firmware_data(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_firmware != NULL) {
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
			cdev->tp_firmware->size = 0;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
	}
	cdev->fw_data_pos = 0;
}


int tp_alloc_tp_firmware_data(int buf_size)
{
	struct ztp_device *cdev = tpd_cdev;

	tp_free_tp_firmware_data();
	cdev->tp_firmware = kzalloc(sizeof(struct firmware), GFP_KERNEL);
	if (cdev->tp_firmware == NULL) {
		pr_err("tpd:alloc struct firmware failed");
		return -ENOMEM;
	}
	cdev->tp_firmware->data = vmalloc(sizeof(*cdev->tp_firmware) + buf_size);
	if (!cdev->tp_firmware->data) {
		pr_err("tpd: alloc tp_firmware->data failed");
		kfree(cdev->tp_firmware);
		return -ENOMEM;
	}
	cdev->tp_firmware->size = buf_size;
	memset((char *)cdev->tp_firmware->data, 0x00, sizeof(*cdev->tp_firmware) + buf_size);
	return 0;
}

int  tpd_copy_to_tp_firmware_data(char *buf)
{
	struct ztp_device *cdev = tpd_cdev;
	int len = 0;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		pr_err("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		pr_err("Invalid firmware size");
		return -EINVAL;
	}

	if (cdev->fw_data_pos >= cdev->tp_firmware->size)
		return 0;
	len = strlen(buf);
	if (cdev->fw_data_pos + len > cdev->tp_firmware->size)
		len = cdev->tp_firmware->size - cdev->fw_data_pos;
	memcpy((u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], buf, len);
	cdev->fw_data_pos += len;
	return len;
}

void  tpd_reset_fw_data_pos_and_size(void)
{
	struct ztp_device *cdev = tpd_cdev;

	cdev->tp_firmware->size = cdev->fw_data_pos;
	cdev->fw_data_pos = 0;
}
static ssize_t tp_module_info_read(struct file *file,
	char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t buffer_tpd[200];
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tpinfo) {
	cdev->get_tpinfo(cdev);
	}

	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "TP module: %s(0x%x)\n",
			cdev->ic_tpinfo.vendor_name, cdev->ic_tpinfo.module_id);
	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "IC type : %s\n",
			cdev->ic_tpinfo.tp_name);
	if (cdev->ic_tpinfo.i2c_addr)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "I2C address: 0x%x\n",
			cdev->ic_tpinfo.i2c_addr);
	if (cdev->ic_tpinfo.spi_num)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Spi num: %d\n",
			cdev->ic_tpinfo.spi_num);
	len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Firmware version : 0x%x\n",
			cdev->ic_tpinfo.firmware_ver);
	if (cdev->ic_tpinfo.config_ver)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Config version:0x%x\n",
			cdev->ic_tpinfo.config_ver);
	if (cdev->ic_tpinfo.display_ver)
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Display version:0x%x\n",
			cdev->ic_tpinfo.display_ver);
	if (cdev->ic_tpinfo.chip_batch[0])
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "Chip hard version:%s\n",
			cdev->ic_tpinfo.chip_batch);
	if (cdev->ic_tpinfo.fw_update_status[0])
		len += snprintf(buffer_tpd + len, sizeof(buffer_tpd) - len, "fw update status:%s\n",
			cdev->ic_tpinfo.fw_update_status);
	return simple_read_from_buffer(buffer, count, offset, buffer_tpd, len);
}

static ssize_t tp_wake_gesture_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_gesture) {
		cdev->get_gesture(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->b_gesture_enable);

	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_gesture_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_wake_gesture_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val %d.\n", __func__, input);
	if (cdev->wake_gesture) {
		cdev->wake_gesture(cdev, input);
	}
	return len;
}
static ssize_t tp_smart_cover_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_smart_cover) {
		cdev->get_smart_cover(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->b_smart_cover_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_smart_cover_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_smart_cover_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val %d.\n", __func__, input);
	if (cdev->set_smart_cover) {
		cdev->set_smart_cover(cdev, input);
	}
	return len;
}
static ssize_t tp_glove_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_glove_mode) {
		cdev->get_glove_mode(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->b_glove_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_glove_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_glove_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val %d.\n", __func__, input);
	if (cdev->set_glove_mode) {
		cdev->set_glove_mode(cdev, input);
	}
	return len;
}
static ssize_t tpfwupgrade_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int fw_size = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &fw_size);
	if (ret)
		return -EINVAL;
	pr_notice("tpd: %s val %d.\n", __func__, fw_size);
	mutex_lock(&cdev->cmd_mutex);
	if (fw_size > 10) {
		if (cdev->tp_firmware != NULL) {
			if (cdev->tp_firmware->data != NULL)
				vfree(cdev->tp_firmware->data);
			kfree(cdev->tp_firmware);
		}
		cdev->fw_data_pos = 0;
		cdev->tp_firmware = kzalloc(sizeof(struct firmware), GFP_KERNEL);
		if (cdev->tp_firmware == NULL) {
			pr_err("tpd:alloc struct firmware failed");
			mutex_unlock(&cdev->cmd_mutex);
			return -ENOMEM;
		}
		cdev->tp_firmware->data = vmalloc(sizeof(*cdev->tp_firmware) + fw_size);
		if (!cdev->tp_firmware->data) {
			pr_err("tpd: alloc tp_firmware->data failed");
			kfree(cdev->tp_firmware);
			mutex_unlock(&cdev->cmd_mutex);
			return -ENOMEM;
		}
		cdev->tp_firmware->size = fw_size;
		memset((char *)cdev->tp_firmware->data, 0x00, sizeof(*cdev->tp_firmware) + fw_size);
	} else if (cdev->tp_firmware != NULL) {
		if (cdev->tp_fw_upgrade) {
			cdev->tp_fw_upgrade(cdev, NULL, 0);
		}
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
		cdev->fw_data_pos = 0;
	}
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t suspend_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->tp_suspend_show) {
		cdev->tp_suspend_show(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->tp_suspend);
	len = snprintf(data_buf, sizeof(data_buf), "tp suspend is: %u\n", cdev->tp_suspend);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t suspend_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val %d.\n", __func__, input);

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->sys_set_tp_suspend_flag == input) {
		pr_notice("tpd: %s tp state don't need change.\n", __func__);
		mutex_unlock(&cdev->cmd_mutex);
		return len;
	}
	cdev->sys_set_tp_suspend_flag = input;
	if (cdev->set_tp_suspend) {
		cdev->set_tp_suspend(cdev, PROC_SUSPEND_NODE, input);
	}
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t tp_single_tap_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->get_singletap)
		cdev->get_singletap(cdev);

	pr_notice("tpd: %s val: %d.\n", __func__, cdev->b_single_tap_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_single_tap_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_single_tap_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 5 : 0;
	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_singletap)
		cdev->set_singletap(cdev, input);

	return len;
}

static ssize_t tp_single_aod_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->get_singleaod)
		cdev->get_singleaod(cdev);

	pr_notice("tpd: %s val: %d.\n", __func__, cdev->b_single_aod_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->b_single_aod_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_single_aod_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 5 : 0;
	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_singleaod)
		cdev->set_singleaod(cdev, input);

	return len;
}

static ssize_t tp_edge_report_limit_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;
	int i = 0;
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;
	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		TPD_DMESG("alloc data_buf failed");
		return -ENOMEM;
	}

	len += snprintf(data_buf + len, PAGE_SIZE - len, "#######################################");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "#######################################\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "algo_open, echo algo_open:1 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_pixel, echo jitter_pixel:10 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_timer, echo jitter_timer:100 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel, echo click_pixel:10 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_open, echo long_press_open:1 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_timer, echo long_press_timer:500 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "pixel limit level,user setting. echo 5 > edge_report_limit\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_pixel, echo long_press_pixel:10,10,20,20 > edge_report_limit\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "#######################################");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "#######################################\n\n");

	len += snprintf(data_buf + len, PAGE_SIZE - len, "algo_open:%5u\n", cdev->zte_tp_algo);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_pixel:%5u\n", cdev->tp_jitter_check);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "jitter_timer:%5u\n", cdev->tp_jitter_timer);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel:%5u\n", cdev->edge_click_sup_p);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_open:%5u\n", cdev->edge_long_press_check);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "long_press_timer:%5u\n", cdev->edge_long_press_timer);
	len += snprintf(data_buf + len, PAGE_SIZE - len, "pixel limit level:%5u\n", cdev->edge_limit_pixel_level);

	len += snprintf(data_buf + len, PAGE_SIZE - len, "click_pixel width:");
	for (i = 0; i < MAX_LIMIT_NUM; i++) {
		if (len >= (PAGE_SIZE - 5))
			break;
		len += snprintf(data_buf + len, PAGE_SIZE - len, "%5u", cdev->edge_report_limit[i]);
	}
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n long_press_pixel:");
	for (i = 0; i < MAX_LIMIT_NUM; i++) {
		if (len >= (PAGE_SIZE - 5))
			break;
		len += snprintf(data_buf + len, PAGE_SIZE - len, "%5u", cdev->long_pess_suppression[i]);
	}
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n");
	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}

static ssize_t tp_edge_report_limit_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0, i = 0;
	u8 *token = NULL;
	char *cur = NULL;
	char buff[100] = { 0 };
	u16 count = 0;
	unsigned int  s_to_u8 = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;
	int algo_item = 0;

	len = (len < sizeof(buff)) ? len : sizeof(buff);
	if (buffer != NULL) {
		if (copy_from_user(buff, buffer, len)) {
			pr_err("Failed to copy data from user space\n");
			len = -EINVAL;
			goto out;
		}
	}
	algo_item = get_tp_algo_item_id(buff);
	if (algo_item < 0) {
		ret = kstrtouint_from_user(buffer, len, 10, &input);
		if (ret || input > 10)
			return -EINVAL;
		cdev->edge_limit_pixel_level = input;
		/* user set level  0-5: x_max x 1% increase
		     user set level  6-10:  x_max x 0.5% increase
		*/
#ifdef CONFIG_TOUCHSCREEN_FOLD_PROJECT
		if (cdev->edge_limit_pixel_level <= 5)
			cdev->user_edge_limit[0] = cdev->max_x * cdev->edge_limit_pixel_level * 7 / 2200;
		else
			cdev->user_edge_limit[0] = cdev->max_x * 35  / 2200
				+ (cdev->max_x  * 4 / 2200) * (cdev->edge_limit_pixel_level - 5);
		cdev->user_edge_limit[1] = 0;
		TPD_DMESG("tpd: edge_limit_pixel_level = %d, limit[0,1] = [%d,%d]\n",
			cdev->edge_limit_pixel_level, cdev->user_edge_limit[0], cdev->user_edge_limit[1]);
#else
		if (cdev->edge_limit_pixel_level <= 5)
			cdev->user_edge_limit[0] = cdev->max_x * cdev->edge_limit_pixel_level * 7 / 1000;
		else
			cdev->user_edge_limit[0] = cdev->max_x * 35  / 1000
				+ (cdev->max_x  * 4 / 1000) * (cdev->edge_limit_pixel_level - 5);
		cdev->user_edge_limit[1] = 0;
		TPD_DMESG("tpd: edge_limit_pixel_level = %d, limit[0,1] = [%d,%d]\n",
			cdev->edge_limit_pixel_level, cdev->user_edge_limit[0], cdev->user_edge_limit[1]);
#endif
	} else {
		cur = strchr(buff, ':');
		cur++;
		TPD_DMESG("cur = %s\n", cur);
		switch (algo_item) {
		case zte_algo_enable:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				s_to_u8 = s_to_u8 > 0 ? 1 : 0;
				cdev->zte_tp_algo = s_to_u8;
				TPD_DMESG("zte_tp_algo = %d\n", cdev->zte_tp_algo);
			}
			break;
		case tp_jitter_check_pixel:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->tp_jitter_check = s_to_u8;
				TPD_DMESG("tp_jitter_check = %d\n", cdev->tp_jitter_check);
			}
			break;
		case tp_jitter_timer:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->tp_jitter_timer = s_to_u8;
				TPD_DMESG("tp_jitter_timer = %d\n", cdev->tp_jitter_timer);
			}
			break;
		case tp_edge_click_suppression_pixel:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->edge_click_sup_p = s_to_u8;
				TPD_DMESG("tp_edge_click_suppression_pixel = %d\n", cdev->edge_click_sup_p);
				for (i = 0; i < 4; i++)
					cdev->edge_report_limit[i] = cdev->edge_click_sup_p;
			}
			break;
		case tp_long_press_enable:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				s_to_u8 = s_to_u8 > 0 ? 1 : 0;
				cdev->edge_long_press_check = s_to_u8;
				TPD_DMESG("edge_long_press_check = %d\n", cdev->edge_long_press_check);
			}
			break;
		case tp_long_press_timer:
			ret = kstrtouint(cur, 10, &s_to_u8);
			if (ret == 0) {
				cdev->edge_long_press_timer = s_to_u8;
				TPD_DMESG("zte_tp_algo = %d\n", cdev->edge_long_press_timer);
			}
			break;
		case tp_long_press_pixel:
			while ((token = strsep(&cur, ",")) != NULL) {
			ret = kstrtouint(token, 10, &s_to_u8);
			if (ret == 0) {
				cdev->long_pess_suppression[count] = s_to_u8;
				TPD_DMESG("long_pess_suppression[%d] = %d\n", count, cdev->long_pess_suppression[count]);
				count++;
			}
			if (count >= MAX_LIMIT_NUM)
				break;
			}
			break;
		default:
			TPD_DMESG("ignore algo item");
			break;
		}
	}

out:
	return len;
}

static ssize_t get_one_key(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_one_key) {
		cdev->get_one_key(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->one_key_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->one_key_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_one_key(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_one_key) {
		cdev->set_one_key(cdev, input);
	}

	return len;
}

static ssize_t get_play_game(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_play_game) {
		cdev->get_play_game(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->play_game_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->play_game_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_play_game(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_play_game) {
		cdev->set_play_game(cdev, input);
	}

	return len;
}

static ssize_t get_tp_report_rate(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tp_report_rate) {
		cdev->get_tp_report_rate(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->tp_report_rate);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->tp_report_rate);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_tp_report_rate(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_tp_report_rate) {
		cdev->set_tp_report_rate(cdev, input);
	}

	return len;
}

static ssize_t get_follow_hand_level(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_follow_hand_level) {
		cdev->get_follow_hand_level(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->follow_hand_level);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->follow_hand_level);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_follow_hand_level(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_follow_hand_level) {
		cdev->set_follow_hand_level(cdev, input);
	}

	return len;
}

static ssize_t get_finger_lock_flag(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_finger_lock_flag) {
		cdev->get_finger_lock_flag(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->finger_lock_flag);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->finger_lock_flag);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_finger_lock_flag(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_finger_lock_flag) {
		cdev->set_finger_lock_flag(cdev, input);
	}

	return len;
}

static ssize_t get_tp_noise_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	int retval = -1;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->get_noise)
		retval = cdev->get_noise(cdev);
	if (cdev->tp_firmware != NULL) {
		len = snprintf(data_buf, sizeof(data_buf), "%d\n", cdev->tp_firmware->size);
		pr_notice("tpd: get tp noise size:%d.\n", cdev->tp_firmware->size);
	}

	mutex_unlock(&cdev->cmd_mutex);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t get_tp_noise_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	struct ztp_device *cdev = tpd_cdev;

	mutex_lock(&cdev->cmd_mutex);
	if (cdev->tp_firmware != NULL) {
		if (cdev->tp_firmware->data != NULL) {
			vfree(cdev->tp_firmware->data);
			cdev->tp_firmware->data = NULL;
		}
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
	}
	cdev->fw_data_pos = 0;
	mutex_unlock(&cdev->cmd_mutex);
	return len;
}

static ssize_t headset_state_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->headset_state_show) {
		cdev->headset_state_show(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->headset_state);
	len = snprintf(data_buf, sizeof(data_buf), "headset state: %u\n", cdev->headset_state);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t headset_state_store(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;
	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
	input = input > 0 ? 1 : 0;
	pr_notice("headset_state: %s val %d.\n", __func__, input);
	if (cdev->set_headset_state) {
		cdev->set_headset_state(cdev, input);
	}
	return len;
}

static ssize_t get_rotation_limit_level(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_rotation_limit_level) {
		cdev->get_rotation_limit_level(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->rotation_limit_level);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->rotation_limit_level);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_rotation_limit_level(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	/*input = input > 0 ? 1 : 0;*/

	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_rotation_limit_level) {
		cdev->set_rotation_limit_level(cdev, input);
	}

	return len;
}

static ssize_t display_rotation_show(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[30] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}

	pr_notice("tpd: %s val:%d.\n", __func__, cdev->display_rotation);
	len = snprintf(data_buf, sizeof(data_buf), "display rotation: %d\n", cdev->display_rotation);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_display_rotation(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;
	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
	cdev->display_rotation = input;
	pr_notice("display rotation: %s val %d.\n", __func__, cdev->display_rotation);
	if (cdev->set_display_rotation) {
		cdev->set_display_rotation(cdev, input);
	}
	return len;
}

static ssize_t tp_sensibility_level_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_sensibility) {
		cdev->get_sensibility(cdev);
	}
	pr_notice("%s:ensibility level:val %d.\n", __func__, cdev->sensibility_level);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->sensibility_level);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_sensibility_level_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;

	cdev->sensibility_level = input;
	pr_notice("%s:ensibility level:val %d.\n", __func__, cdev->sensibility_level);
	if (cdev->set_sensibility) {
		cdev->set_sensibility(cdev, input);
	}

	return len;
}

static ssize_t tp_pen_only_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_pen_only_mode) {
		cdev->get_pen_only_mode(cdev);
	}
	pr_notice("%s:pen only model: %d.\n", __func__, cdev->pen_only_mode);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->pen_only_mode);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_pen_only_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
       input = input > 0 ? 1 : 0;
	cdev->pen_only_mode = input;
	pr_notice("%s:pen only mode:%d.\n", __func__, cdev->pen_only_mode);
	if (cdev->set_pen_only_mode) {
		cdev->set_pen_only_mode(cdev, input);
	}

	return len;
}

static ssize_t tp_self_test_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	int len = 0;
	char *data_buf = NULL;
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;
	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		TPD_DMESG("alloc data_buf failed");
		return -ENOMEM;
	}

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_tp_self_test_result) {
		len = cdev->get_tp_self_test_result(cdev, data_buf);
	}
	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	tp_free_tp_firmware_data();
	return len;
}

static ssize_t tp_self_test_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	struct ztp_device *cdev = tpd_cdev;

	if(tp_alloc_tp_firmware_data(TP_TEST_FILE_SIZE)) {
		TPD_DMESG(" alloc tp firmware data fail");
		return -ENOMEM;
	}
	if (cdev->tp_self_test) {
		cdev->tp_self_test(cdev);
	}
	tpd_reset_fw_data_pos_and_size();
	return len;
}

static ssize_t tp_palm_mode_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->tp_palm_mode_read)
		cdev->tp_palm_mode_read(cdev);

	pr_notice("tpd: %s val: %d.\n", __func__, cdev->palm_mode_en);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->palm_mode_en);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_palm_mode_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->tp_palm_mode_write)
		cdev->tp_palm_mode_write(cdev, input);

	return len;
}

static ssize_t tp_fold_state_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0)
		return 0;

	if (cdev->tp_fold_state_read)
		cdev->tp_fold_state_read(cdev);

	pr_notice("tpd: %s val: %d.\n", __func__, cdev->fold_state);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->fold_state);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_fold_state_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->tp_fold_state_write)
		cdev->tp_fold_state_write(cdev, input);

	return len;
}

static ssize_t get_fake_sleep(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[10] = {0};
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	if (cdev->get_fake_sleep) {
		cdev->get_fake_sleep(cdev);
	}
	pr_notice("tpd: %s val:%d.\n", __func__, cdev->fake_sleep_enable);
	len = snprintf(data_buf, sizeof(data_buf), "%u\n", cdev->fake_sleep_enable);
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t set_fake_sleep(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	struct ztp_device *cdev = tpd_cdev;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret)
		return -EINVAL;

	input = input > 0 ? 1 : 0;
	pr_notice("tpd: %s val = %d\n", __func__, input);

	if (cdev->set_fake_sleep) {
		cdev->set_fake_sleep(cdev, input);
	}

	return len;
}

#ifdef TOUCH_DOWN_UP_ZLOG
static ssize_t ghost_debug_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;
	struct ztp_device *cdev = tpd_cdev;

	if (*offset != 0) {
		return 0;
	}
	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		TPD_DMESG("alloc data_buf failed");
		return -ENOMEM;
	}
	TPD_DMESG("ghost_check_single_time is %d", cdev->ghost_check_single_time);
	TPD_DMESG("ghost_check_multi_time is %d", cdev->ghost_check_multi_time);
	TPD_DMESG("ghost_check_single_count is %d", cdev->ghost_check_single_count);
	TPD_DMESG("ghost_check_multi_count is %d", cdev->ghost_check_multi_count);
	TPD_DMESG("ghost_check_start_time is %d", cdev->ghost_check_start_time);
	TPD_DMESG("ghost_check_ignore_id is %d", cdev->ghost_check_ignore_id);

	len += snprintf(data_buf + len, PAGE_SIZE - len, "#######################################\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "single_time,multi_time,single_count,multi_count,start_time,ignore_id \n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "echo 25,20,5,8,35,9 > ghost_debug \n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "#######################################\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE,
		"ghost_check_single_time is %d\n", cdev->ghost_check_single_time);
	len += snprintf(data_buf + len, PAGE_SIZE - len,
		"ghost_check_multi_time is %d\n", cdev->ghost_check_multi_time);
	len += snprintf(data_buf + len, PAGE_SIZE - len,
		"ghost_check_single_count is %d\n", cdev->ghost_check_single_count);
	len += snprintf(data_buf + len, PAGE_SIZE - len,
		"ghost_check_multi_count is %d\n", cdev->ghost_check_multi_count);
	len += snprintf(data_buf + len, PAGE_SIZE - len,
		"ghost_check_start_time is %d\n", cdev->ghost_check_start_time);
	len += snprintf(data_buf + len, PAGE_SIZE - len,
		"ghost_check_ignore_id is %d\n", cdev->ghost_check_ignore_id);
	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}

static ssize_t ghost_debug_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
 	int ret = 0 ;
	u8 *token = NULL;
	char *cur = NULL;
	char buff[100] = { 0 };
	unsigned int  s_to_u8 = 0;
	u8 data[10] = { 0 };
	u16 count = 0;
	struct ztp_device *cdev = tpd_cdev;

	len = (len < sizeof(buff)) ? len : sizeof(buff);
	if (buffer != NULL) {
		if (copy_from_user(buff, buffer, len)) {
			TPD_DMESG("Failed to copy data from user space\n");
			len = -EINVAL;
			goto out;
		}
	}
	cur = &buff[0];
	while ((token = strsep(&cur, ",")) != NULL) {
		ret = kstrtouint(token, 10, &s_to_u8);
		if (ret == 0) {
			data[count] = s_to_u8;
			count++;
		}
	}
	cdev->ghost_check_single_time = data[0];
	cdev->ghost_check_multi_time = data[1];
	cdev->ghost_check_single_count = data[2];
	cdev->ghost_check_multi_count = data[3];
	cdev->ghost_check_start_time = data[4];
	cdev->ghost_check_ignore_id = data[5];
out:
	return len;
}
#endif

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
static ssize_t tp_zlog_debug_read(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	uint8_t data_buf[50] = {0};

	if (*offset != 0) {
		return 0;
	}
	len = snprintf(data_buf, sizeof(data_buf), "this no use");
	return simple_read_from_buffer(buffer, count, offset, data_buf, len);
}

static ssize_t tp_zlog_debug_write(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;
	char data_buf[10] = {0};

	len = len >= sizeof(data_buf) ? sizeof(data_buf) - 1 : len;
	ret = copy_from_user(data_buf, buffer, len);
	if (ret)
		return -EINVAL;

	ret = kstrtouint(data_buf, 0, &input);
	if (ret)
		return -EINVAL;
  	switch (input) {
	case TP_I2C_R_ERROR_NO:
		tpd_zlog_record_notify(TP_I2C_R_ERROR_NO);
		break;
	case TP_I2C_W_ERROR_NO:
		tpd_zlog_record_notify(TP_I2C_W_ERROR_NO);
		break;
	case TP_SPI_R_ERROR_NO:
		tpd_zlog_record_notify(TP_SPI_R_ERROR_NO);
		break;
	case TP_SPI_W_ERROR_NO:
		tpd_zlog_record_notify(TP_SPI_W_ERROR_NO);
		break;
	case TP_CRC_ERROR_NO:
		tpd_zlog_record_notify(TP_CRC_ERROR_NO);
		break;
	case TP_FW_UPGRADE_ERROR_NO:
		tpd_zlog_record_notify(TP_FW_UPGRADE_ERROR_NO);
		break;
	case TP_REQUEST_FIRMWARE_ERROR_NO:
		tpd_zlog_record_notify(TP_REQUEST_FIRMWARE_ERROR_NO);
		break;
	case TP_ESD_CHECK_ERROR_NO:
		tpd_zlog_record_notify(TP_ESD_CHECK_ERROR_NO);
		break;
	case TP_PROBE_ERROR_NO:
		tpd_zlog_record_notify(TP_PROBE_ERROR_NO);
		break;
	case TP_SUSPEND_GESTURE_OPEN_NO:
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
		break;
	default:
		break;
	}

	return len;
}

#endif

static const struct proc_ops proc_ops_tp_module_Info = {
	.proc_read = tp_module_info_read,
};
static const struct proc_ops proc_ops_wake_gesture = {
	.proc_read = tp_wake_gesture_read,
	.proc_write = tp_wake_gesture_write,
};
static const struct proc_ops proc_ops_smart_cover = {
	.proc_read = tp_smart_cover_read,
	.proc_write = tp_smart_cover_write,
};

static const struct proc_ops proc_ops_glove = {
	.proc_read = tp_glove_read,
	.proc_write = tp_glove_write,
};

static const struct proc_ops proc_ops_tpfwupgrade = {
	.proc_write = tpfwupgrade_store,
};

static const struct proc_ops proc_ops_suspend = {
	.proc_read = suspend_show,
	.proc_write = suspend_store,
};

static const struct proc_ops proc_ops_headset_state = {
	.proc_read = headset_state_show,
	.proc_write = headset_state_store,
};

static const struct proc_ops proc_ops_rotation_limit_level = {
	.proc_read = get_rotation_limit_level,
	.proc_write = set_rotation_limit_level,
};

static const struct proc_ops proc_ops_mrotation = {
	.proc_read = display_rotation_show,
	.proc_write = set_display_rotation,
};

static const struct proc_ops proc_ops_single_tap = {
	.proc_read = tp_single_tap_read,
	.proc_write = tp_single_tap_write,
};

static const struct proc_ops proc_ops_single_aod = {
	.proc_read = tp_single_aod_read,
	.proc_write = tp_single_aod_write,
};

static const struct proc_ops proc_ops_get_noise = {
	.proc_read = get_tp_noise_show,
	.proc_write = get_tp_noise_store,
};

static const struct proc_ops proc_ops_edge_report_limit = {
	.proc_read = tp_edge_report_limit_read,
	.proc_write = tp_edge_report_limit_write,
};

static const struct proc_ops proc_ops_onekey = {
	.proc_read = get_one_key,
	.proc_write = set_one_key,
};

static const struct proc_ops proc_ops_playgame = {
	.proc_read = get_play_game,
	.proc_write = set_play_game,
};

static const struct proc_ops proc_ops_tp_report_rate = {
	.proc_read = get_tp_report_rate,
	.proc_write = set_tp_report_rate,
};

static const struct proc_ops proc_ops_follow_hand_level = {
	.proc_read = get_follow_hand_level,
	.proc_write = set_follow_hand_level,
};

static const struct proc_ops proc_ops_sensibility_level = {
	.proc_read = tp_sensibility_level_read,
	.proc_write = tp_sensibility_level_write,
};

static const struct proc_ops proc_ops_pen_only = {
	.proc_read = tp_pen_only_read,
	.proc_write = tp_pen_only_write,
};

static const struct proc_ops proc_ops_finger_lock_flag = {
	.proc_read = get_finger_lock_flag,
	.proc_write = set_finger_lock_flag,
};

static const struct proc_ops proc_ops_tp_self_test = {
	.proc_read = tp_self_test_read,
	.proc_write = tp_self_test_write,
};

static const struct proc_ops proc_ops_palm_mode = {
	.proc_read = tp_palm_mode_read,
	.proc_write = tp_palm_mode_write,
};

static const struct proc_ops proc_ops_fold_state = {
	.proc_read = tp_fold_state_read,
	.proc_write = tp_fold_state_write,
};

static const struct proc_ops proc_ops_fake_sleep = {
	.proc_read = get_fake_sleep,
	.proc_write = set_fake_sleep,
};

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
static const struct proc_ops proc_ops_zlog_debug = {
	.proc_read= tp_zlog_debug_read,
	.proc_write = tp_zlog_debug_write,
};
#endif

#ifdef TOUCH_DOWN_UP_ZLOG
static const struct proc_ops proc_ops_ghost_debug = {
	//.owner = THIS_MODULE,
	.proc_read = ghost_debug_read,
	.proc_write = ghost_debug_write,
};
#endif

static void create_tpd_proc_entry(void)
{
	struct proc_dir_entry *tpd_proc_entry = NULL;

	tpd_proc_dir = proc_mkdir(PROC_TOUCH_DIR, NULL);
	if (tpd_proc_dir == NULL) {
		pr_err("%s: mkdir touchscreen failed!\n",  __func__);
		return;
	}
	tpd_proc_entry = proc_create(PROC_TOUCH_INFO, 0664, tpd_proc_dir, &proc_ops_tp_module_Info);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create ts_information failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_WAKE_GESTURE, 0664,  tpd_proc_dir, &proc_ops_wake_gesture);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create wake_gesture failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SMART_COVER, 0664, tpd_proc_dir, &proc_ops_smart_cover);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create smart_cover failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_GLOVE, 0664, tpd_proc_dir, &proc_ops_glove);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create glove mode failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FW_UPGRADE, 0664,  tpd_proc_dir, &proc_ops_tpfwupgrade);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create FW_upgrade failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SUSPEND, 0664,  tpd_proc_dir, &proc_ops_suspend);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create suspend failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_HEADSET_STATE, 0664,  tpd_proc_dir, &proc_ops_headset_state);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create headset_state failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_ROTATION_LIMIT_LEVEL, 0664,  tpd_proc_dir, &proc_ops_rotation_limit_level);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create rotation_limit_level failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_MROTATION, 0664,  tpd_proc_dir, &proc_ops_mrotation);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create mRotation failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_SINGLETAP, 0664, tpd_proc_dir, &proc_ops_single_tap);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create single_tap failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_SINGLEAOD, 0664, tpd_proc_dir, &proc_ops_single_aod);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create single_aod failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_GET_NOISE, 0664, tpd_proc_dir, &proc_ops_get_noise);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create get_noise failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_EDGE_REPORT_LIMIT, 0664, tpd_proc_dir, &proc_ops_edge_report_limit);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create edge_report_limit failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_ONEKEY, 0664,  tpd_proc_dir, &proc_ops_onekey);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create one_key failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_PLAY_GAME, 0664,  tpd_proc_dir, &proc_ops_playgame);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create play_game failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_REPORT_RATE, 0664,  tpd_proc_dir, &proc_ops_tp_report_rate);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create tp report rate failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FOLLOW_HAND_LEVEL, 0664,  tpd_proc_dir, &proc_ops_follow_hand_level);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create follow_hand level failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_SENSIBILITY, 0664, tpd_proc_dir, &proc_ops_sensibility_level);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create sensilibity failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_PEN_ONLY, 0664, tpd_proc_dir, &proc_ops_pen_only);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create pen only failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FINGER_LOCK_FLAG, 0664,  tpd_proc_dir, &proc_ops_finger_lock_flag);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create finger_lock_flag failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_SELF_TEST, 0664, tpd_proc_dir, &proc_ops_tp_self_test);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create tp self test failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_PALM_MODE, 0664, tpd_proc_dir, &proc_ops_palm_mode);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create palm mode failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_TP_FOLD_STATE, 0664, tpd_proc_dir, &proc_ops_fold_state);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create fold state failed!\n");
	tpd_proc_entry = proc_create(PROC_TOUCH_FAKE_SLEEP, 0664,  tpd_proc_dir, &proc_ops_fake_sleep);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create touch_fake_sleep failed!\n");
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	tpd_proc_entry = proc_create(PROC_ZLOG_DEBUG, 0664, tpd_proc_dir, &proc_ops_zlog_debug);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create zlog_debug failed!\n");
#endif
#ifdef TOUCH_DOWN_UP_ZLOG
	tpd_proc_entry = proc_create(PROC_TOUCH_GHOST_DEBUG, 0664, tpd_proc_dir, &proc_ops_ghost_debug);
	if (tpd_proc_entry == NULL)
		pr_err("proc_create ghost_debug failed!\n");
#endif
}
void tpd_proc_deinit(void)
{
	if (tpd_proc_dir == NULL) {
		pr_err("%s: proc/touchscreen is NULL!\n",  __func__);
		return;
	}
	remove_proc_entry(PROC_TOUCH_INFO, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_WAKE_GESTURE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SMART_COVER, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_GLOVE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FW_UPGRADE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SUSPEND, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_HEADSET_STATE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_ROTATION_LIMIT_LEVEL, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_MROTATION, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SINGLETAP, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SINGLEAOD, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_GET_NOISE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_EDGE_REPORT_LIMIT, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_ONEKEY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_PLAY_GAME, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_REPORT_RATE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FOLLOW_HAND_LEVEL, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_SENSIBILITY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_PEN_ONLY, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FINGER_LOCK_FLAG, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_SELF_TEST, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_PALM_MODE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_TP_FOLD_STATE, tpd_proc_dir);
	remove_proc_entry(PROC_TOUCH_FAKE_SLEEP, tpd_proc_dir);
#ifdef TOUCH_DOWN_UP_ZLOG
	remove_proc_entry(PROC_TOUCH_GHOST_DEBUG, tpd_proc_dir);
#endif
	remove_proc_entry(PROC_TOUCH_DIR, NULL);
}

static ssize_t tpd_sysfs_fwimage_store(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t pos, size_t count)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		pr_err("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		pr_err("Invalid firmware size");
		return -EINVAL;
	}
	if (cdev->fw_data_pos >= cdev->tp_firmware->size) {
		cdev->fw_data_pos = 0;
		return -EINVAL;
	}
	if (cdev->fw_data_pos + count > cdev->tp_firmware->size)
		count = cdev->tp_firmware->size - cdev->fw_data_pos;
	pr_info("tpd: cdev->fw_data_pos: %d, count:%d\n", cdev->fw_data_pos, count);
	mutex_lock(&cdev->cmd_mutex);
	memcpy((u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], buf, count);
	cdev->fw_data_pos += count;
	mutex_unlock(&cdev->cmd_mutex);
	return count;
}

static ssize_t tpd_sysfs_fwimage_show(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t pos, size_t count)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->tp_firmware || !cdev->tp_firmware->data) {
		pr_err("Need set fw image size first");
		return -ENOMEM;
	}

	if (cdev->tp_firmware->size == 0) {
		pr_err("Invalid firmware size");
		return -EINVAL;
	}
	mutex_lock(&cdev->cmd_mutex);
	if (cdev->fw_data_pos >= cdev->tp_firmware->size) {
		cdev->fw_data_pos = 0;
		vfree(cdev->tp_firmware->data);
		cdev->tp_firmware->data = NULL;
		kfree(cdev->tp_firmware);
		cdev->tp_firmware = NULL;
		pr_info("tpd, tp_firmware free.\n");
		mutex_unlock(&cdev->cmd_mutex);
		return 0;
	}
	if (cdev->fw_data_pos + count > cdev->tp_firmware->size)
		count = cdev->tp_firmware->size - cdev->fw_data_pos;
	pr_info("tpd: cdev->fw_data_pos: %d, count:%d\n", cdev->fw_data_pos, count);
	memcpy(buf, (u8 *)&cdev->tp_firmware->data[cdev->fw_data_pos], count);
	cdev->fw_data_pos += count;
	mutex_unlock(&cdev->cmd_mutex);
	return count;
}

static const struct bin_attribute fwimage_attr = {
	.attr = {
		.name = "fwimage",
		.mode = 0666,
	},
	.size = 0,
	.write = tpd_sysfs_fwimage_store,
	.read = tpd_sysfs_fwimage_show,
};

static int tpd_fw_sysfs_init(void)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->zte_touch_pdev){
		TPD_DMESG("zte_touch_pdev is NULL.");
		return -EINVAL;
	}
	cdev->zte_touch_kobj = kobject_create_and_add("fwupdate",
					&cdev->zte_touch_pdev->dev.kobj);
	if (!cdev->zte_touch_kobj) {
		pr_err("failed create sub dir for fwupdate");
		return -EINVAL;
	}
	ret = sysfs_create_bin_file(cdev->zte_touch_kobj, &fwimage_attr);
	if (ret) {
		pr_err("failed create fwimage bin node, %d", ret);
		kobject_put(cdev->zte_touch_kobj);
	}

	return ret;
}

static void tpd_fw_sysfs_remove(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->zte_touch_kobj)
		return;
	sysfs_remove_bin_file(cdev->zte_touch_kobj, &fwimage_attr);
	kobject_put(cdev->zte_touch_kobj);
}

static void tpd_report_uevent(u8 gesture_key)
{
	char *envp[2] = {NULL};
	struct ztp_device *cdev = tpd_cdev;

	switch (gesture_key) {
	case single_tap:
		TPD_DMESG("%s single tap gesture", __func__);
		envp[0] = "single_tap=true";
		break;
	case double_tap:
		TPD_DMESG("%s double tap gesture", __func__);
		envp[0] = "double_tap=true";
		break;
	case pen_low_batt:
		TPD_DMESG("%s pen low batt", __func__);
		envp[0] = "pen_capacity_low=true";
		break;
	default:
		TPD_DMESG("%s no such gesture key(%d)", __func__, gesture_key);
		return;
	}

	kobject_uevent_env(&(cdev->zte_touch_pdev->dev.kobj), KOBJ_CHANGE, envp);
}

int zte_touch_pdev_register(void)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s", __func__);
	cdev->zte_touch_pdev = platform_device_alloc("zte_touch", -1);
	if (!cdev->zte_touch_pdev) {
		TPD_DMESG("%s failed to allocate platform device", __func__);
		ret = -ENOMEM;
		goto alloc_failed;
	}

	ret = platform_device_add(cdev->zte_touch_pdev);
	if (ret < 0) {
		TPD_DMESG("%s failed to add platform device ret=%d", __func__, ret);
		goto register_failed;
	}

	cdev->tpd_report_uevent = tpd_report_uevent;
	return 0;

register_failed:
	cdev->zte_touch_pdev->dev.release(&(cdev->zte_touch_pdev->dev));
alloc_failed:
	cdev->tpd_report_uevent = NULL;

	return ret;
}

void zte_touch_pdev_unregister(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (!cdev->zte_touch_pdev) {
		cdev->zte_touch_pdev->dev.release(&(cdev->zte_touch_pdev->dev));
		platform_device_unregister(cdev->zte_touch_pdev);
	}
}

static int ztp_parse_dt(struct device_node *node, struct ztp_device *cdev)
{
	int ret = 0;
	int i = 0;
	u32 value = 0;

	if (!cdev) {
		TPD_DMESG("invalid tp_dev");
		return -EINVAL;
	}

	cdev->zte_tp_algo = of_property_read_bool(node, "zte,tp_algo");
	if (cdev->zte_tp_algo)
		TPD_DMESG("zte_tp_algo enabled");
	cdev->edge_long_press_check = of_property_read_bool(node, "zte,tp_long_press");
	if (cdev->edge_long_press_check) {
		TPD_DMESG("edge_long_press_check enabled");
		ret = of_property_read_u32(node, "zte,tp_long_press_timer", &value);
		if (!ret) {
			cdev->edge_long_press_timer = value;
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_left_v", &value);
		if (!ret) {
			cdev->long_pess_suppression[0] = value;
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_right_v", &value);
		if (!ret) {
			cdev->long_pess_suppression[1] = value;
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_left_h", &value);
		if (!ret) {
			cdev->long_pess_suppression[2] = value;
		}
		ret = of_property_read_u32(node, "zte,tp_long_press_right_h", &value);
		if (!ret) {
			cdev->long_pess_suppression[3] = value;
		}
	}

#ifdef TOUCH_DOWN_UP_ZLOG
	cdev->ghost_check_config = of_property_read_bool(node, "zte,ghost_check_config");
	if (cdev->ghost_check_config) {
		TPD_DMESG("ghost_check_config enabled");
		ret = of_property_read_u32(node, "zte,ghost_check_single_time", &value);
		if (!ret) {
			cdev->ghost_check_single_time = value;
		} else {
			cdev->ghost_check_single_time = 25;
		}
		ret = of_property_read_u32(node, "zte,ghost_check_multi_time", &value);
		if (!ret) {
			cdev->ghost_check_multi_time = value;
		} else {
			cdev->ghost_check_multi_time = 20;
		}
		ret = of_property_read_u32(node, "zte,ghost_check_single_count", &value);
		if (!ret) {
			cdev->ghost_check_single_count = value;
		} else {
			cdev->ghost_check_single_count = 5;
		}
		ret = of_property_read_u32(node, "zte,ghost_check_multi_count", &value);
		if (!ret) {
			cdev->ghost_check_multi_count = value;
		} else {
			cdev->ghost_check_multi_count = 8;
		}
		ret = of_property_read_u32(node, "zte,ghost_check_start_time", &value);
		if (!ret) {
			cdev->ghost_check_start_time = value;
		} else {
			cdev->ghost_check_start_time = 35;
		}
		ret = of_property_read_u32(node, "zte,ghost_check_ignore_id", &value);
		if (!ret) {
			cdev->ghost_check_ignore_id = value;
		} else {
			cdev->ghost_check_ignore_id = -1;
		}
	} else {
		cdev->ghost_check_single_time = 25;
		cdev->ghost_check_multi_time = 20;
		cdev->ghost_check_single_count = 5;
		cdev->ghost_check_multi_count = 8;
		cdev->ghost_check_start_time = 35;
		cdev->ghost_check_ignore_id = -1;
	}
	TPD_DMESG("ghost_check_single_time is %d", cdev->ghost_check_single_time);
	TPD_DMESG("ghost_check_multi_time is %d", cdev->ghost_check_multi_time);
	TPD_DMESG("ghost_check_single_count is %d", cdev->ghost_check_single_count);
	TPD_DMESG("ghost_check_multi_count is %d", cdev->ghost_check_multi_count);
	TPD_DMESG("ghost_check_start_time is %d", cdev->ghost_check_start_time);
	TPD_DMESG("ghost_check_ignore_id is %d", cdev->ghost_check_ignore_id);
#endif

	ret = of_property_read_u32(node, "zte,tp_jitter_check", &value);
	if (!ret) {
		cdev->tp_jitter_check = value;
		if (cdev->tp_jitter_check) {
			ret = of_property_read_u32(node, "zte,tp_jitter_timer", &value);
			if (!ret) {
				cdev->tp_jitter_timer = value;
			}
		}
	}
	ret = of_property_read_u32(node, "zte,tp_edge_click_suppression_pixel", &value);
	if (!ret) {
		cdev->edge_click_sup_p= value;
		for (i = 0; i < 4; i++)
			cdev->edge_report_limit[i] = cdev->edge_click_sup_p;
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	cdev->ufp_enable = of_property_read_bool(node, "zte,ufp_enable");
	if (cdev->ufp_enable) {
		TPD_DMESG("ufp_enable enabled");
		ret = of_property_read_u32(node, "zte,ufp_circle_center_x", &value);
		if (!ret) {
			cdev->ufp_circle_center_x = value;
		}
		ret = of_property_read_u32(node, "zte,ufp_circle_center_y", &value);
		if (!ret) {
			cdev->ufp_circle_center_y = value;
		}
		ret = of_property_read_u32(node, "zte,ufp_circle_radius", &value);
		if (!ret) {
			cdev->ufp_circle_radius = value;
		}
	}
#endif
	return 0;
}

static void ztp_probe_work(struct work_struct *work)
{
#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
	ilitek_plat_dev_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
	himax_common_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
	cts_i2c_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
	ovt_tcm_module_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
	cts_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
	cts_driver_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_FTS_3681
	fts_ts_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
	goodix_ts_core_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9916R
	goodix_ts_core_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9966
	goodix_ts_core_init();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX
	semi_i2c_device_init();
#endif

}

void tpd_probe_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_DELAYED_WORK(&cdev->tpd_probe_work, ztp_probe_work);

}

void tpd_probe_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_delayed_work_sync(&cdev->tpd_probe_work);

}

#ifdef TOUCH_DOWN_UP_ZLOG
static void tp_ghost_check_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if (tp_ghost_check()){
		TPD_DMESG("%s may be ghost point", __func__);
	}
	ghost_check_reset();
	cdev->start_ghost_check_timer = false;

}
#endif

int tpd_workqueue_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cdev->tpd_wq = create_singlethread_workqueue("tpd_wq");

	if (!cdev->tpd_wq) {
		goto err_create_tpd_report_wq_failed;
	}
	if (tpd_report_work_init())
		goto err_tpd_report_work_init_failed;
	tpd_probe_work_init();
	tpd_resume_work_init();
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	zlog_register_work_init();
#endif
#ifdef TOUCH_DOWN_UP_ZLOG
	INIT_DELAYED_WORK(&cdev->ghost_check_work, tp_ghost_check_work);
#endif

	return 0;
err_tpd_report_work_init_failed:
	if (!cdev->tpd_wq) {
		destroy_workqueue(cdev->tpd_wq);
	}
err_create_tpd_report_wq_failed:
	TPD_DMESG("%s: create tpd workqueue failed\n", __func__);
	return -ENOMEM;
}

void tpd_workqueue_deinit(void)
{
#ifdef TOUCH_DOWN_UP_ZLOG
	struct ztp_device *cdev = tpd_cdev;
#endif

	TPD_DMESG("%s enter", __func__);
	tpd_report_work_deinit();
	tpd_resume_work_deinit();
	tpd_probe_work_deinit();
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	zlog_register_work_deinit();
#endif
#ifdef TOUCH_DOWN_UP_ZLOG
	cancel_delayed_work_sync(&cdev->ghost_check_work);
#endif
}

static void  zte_touch_deinit(void)
{
	static bool ztp_release = false;
	struct ztp_device *cdev = tpd_cdev;

	if (cdev == NULL || ztp_release) {
		TPD_DMESG("zte touch deinit, return\n");
		return;
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	ufp_mac_exit();
#endif
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	zlog_register_work_deinit();
#endif
	tpd_proc_deinit();
	tpd_workqueue_deinit();
	if (!cdev->tpd_wq) {
		destroy_workqueue(cdev->tpd_wq);
	}
	tpd_fw_sysfs_remove();
	zte_touch_pdev_unregister();
	ztp_release = true;
}

static int zte_touch_probe(struct platform_device *pdev)
{
	struct ztp_device *ztp_dev = NULL;

	TPD_DMESG("enter %s, %d\n", __func__, __LINE__);

	ztp_dev = devm_kzalloc(&pdev->dev, sizeof(struct ztp_device), GFP_KERNEL);
	if (!ztp_dev) {
		TPD_DMESG("Failed to allocate memory for ztp dev");
		return -ENOMEM;
	}
	tpd_cdev = ztp_dev;
	ztp_dev->pdev =  pdev;
	platform_set_drvdata(pdev, ztp_dev);
	zte_touch_pdev_register();
	ztp_parse_dt(pdev->dev.of_node, ztp_dev);

	mutex_init(&ztp_dev->cmd_mutex);
	mutex_init(&ztp_dev->report_mutex);
	mutex_init(&ztp_dev->tp_resume_mutex);
#if 0
	lcd_notify_register();
#endif
	create_tpd_proc_entry();
	tpd_fw_sysfs_init();
	tpd_clean_all_event();
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	ufp_mac_init();
#endif
	if(tpd_workqueue_init())
		return -ENOMEM;
	queue_delayed_work(ztp_dev->tpd_wq, &ztp_dev->tpd_probe_work, msecs_to_jiffies(1000));

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	tpd_zlog_init();
	queue_delayed_work(ztp_dev->tpd_report_wq, &ztp_dev->zlog_register_work, msecs_to_jiffies(5000));
#endif

	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	return 0;
}

static int  zte_touch_remove(struct platform_device *pdev)
{
	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	zte_touch_deinit();
	return 0;
}

static void zte_touch_shutdown(struct platform_device *pdev)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	if (cdev->tpd_shutdown)
		cdev->tpd_shutdown(cdev);
	tpd_workqueue_deinit();
}

static const struct of_device_id zte_touch_of_match[] = {
	{ .compatible = "zte_tp", },
	{ },
};

static struct platform_driver zte_touch_device_driver = {
	.probe		= zte_touch_probe,
	.remove		= zte_touch_remove,
	.shutdown	= zte_touch_shutdown,
	.driver		= {
		.name	= "zte_tp",
		.owner	= THIS_MODULE,
		.of_match_table = zte_touch_of_match,
	}
};

int __init zte_touch_init(void)
{
	TPD_DMESG("%s into\n", __func__);

	return platform_driver_register(&zte_touch_device_driver);
}

static void __exit zte_touch_exit(void)
{
#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
	ilitek_plat_dev_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
	himax_common_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
	cts_i2c_driver_exit();
#endif
#if 0
	lcd_notify_unregister();
#endif
#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
	ovt_tcm_module_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
	cts_driver_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
	cts_driver_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_FTS_3681
	fts_ts_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
	goodix_ts_core_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9916R
	goodix_ts_core_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9966
	goodix_ts_core_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX
	semi_i2c_device_exit();
#endif
	zte_touch_deinit();
	platform_driver_unregister(&zte_touch_device_driver);
}

late_initcall(zte_touch_init);
module_exit(zte_touch_exit);
MODULE_SOFTDEP("pre: msm_drm");

MODULE_AUTHOR("zte");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("zte tp");

