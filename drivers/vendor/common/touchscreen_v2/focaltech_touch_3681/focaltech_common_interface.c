/************************************************************************
*
* File Name: fts_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/

#include "focaltech_core.h"
#include "focaltech_test/focaltech_test.h"
#include <linux/kernel.h>
#include <linux/power_supply.h>
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define TEST_RESULT_LENGTH (8 * 1200)
#define TEST_TEMP_LENGTH 8
#define TEST_PASS	0
#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TP_TEST_INIT		1
#define TP_TEST_START	2
#define TP_TEST_END		3

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

char g_fts_ini_filename[MAX_INI_FILE_NAME_LEN] = {0};
int fts_tptest_result = 0;

extern struct fts_ts_data *fts_data;
extern struct fts_test *fts_ftest;
extern int fts_ts_suspend(struct device *dev);
extern int fts_ts_resume(struct device *dev);
extern int fts_test_init_basicinfo(struct fts_test *tdata);
extern int fts_test_entry(char *ini_file_name);
extern int fts_ex_mode_switch(enum _ex_mode mode, u8 value);

#if 0
struct tpvendor_t fts_vendor_info[] = {
	{FTS_MODULE_ID, FTS_MODULE_NAME },
	{FTS_MODULE2_ID, FTS_MODULE2_NAME },
	{FTS_MODULE3_ID, FTS_MODULE3_NAME },
	{VENDOR_END, "Unknown"},
};

int get_fts_module_info_from_lcd(void)
{
	int i = 0;
	const char *panel_name = NULL;

	panel_name = get_lcd_panel_name();
	for (i = 0; i < ARRAY_SIZE(fts_vendor_info); i++) {
		if (strnstr(panel_name, fts_vendor_info[i].vendor_name, strlen(panel_name))) {
			return  fts_vendor_info[i].vendor_id;
		}
	}
	return -EINVAL;
}
#endif

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 fwver_in_chip = 0;
	u8 vendorid_in_chip = 0;
	u8 chipid_in_chip = 0;
	u8 lcdver_in_chip = 0;
	u8 retry = 0;

	if (fts_data->suspended) {
		FTS_ERROR("fts tp in suspned");
		return -EIO;
	}

	while (retry++ < 5) {
		fts_read_reg(FTS_REG_CHIP_ID, &chipid_in_chip);
		fts_read_reg(FTS_REG_VENDOR_ID, &vendorid_in_chip);
		fts_read_reg(FTS_REG_FW_VER, &fwver_in_chip);
		fts_read_reg(FTS_REG_LIC_VER, &lcdver_in_chip);
		if ((chipid_in_chip != 0) && (vendorid_in_chip != 0) && (fwver_in_chip != 0)
			&& (lcdver_in_chip != 0)) {
			FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x,lcd_version=%x .\n",
				  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
			break;
		}
		FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x, lcd_version=%x .\n",
			  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
		msleep(20);
	}

	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Focal");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_FOCAL;

	cdev->ic_tpinfo.chip_part_id = chipid_in_chip;
	cdev->ic_tpinfo.module_id = vendorid_in_chip;
	cdev->ic_tpinfo.chip_ver = 0;
	cdev->ic_tpinfo.firmware_ver = fwver_in_chip;
	cdev->ic_tpinfo.display_ver = lcdver_in_chip;
	cdev->ic_tpinfo.i2c_type = 0;
	cdev->ic_tpinfo.i2c_addr = 0x38;

	return 0;
}

static int tpd_get_singleaodgesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_single_aod_enable = ts_data->ztec.is_single_aod;
	FTS_INFO("%s: enter!, ts_data->ztec.is_single_aod=%d", __func__, ts_data->ztec.is_single_aod);
	FTS_INFO("%s: enter!, cdev->b_single_aod_enable=%d", __func__, cdev->b_single_aod_enable);
	return 0;
}

static int tpd_set_singleaodgesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;
	FTS_INFO("%s: enter!, enable=%d", __func__, enable);
	ts_data->ztec.is_single_aod = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_single_aod = enable;
		ts_data->ztec.is_single_tap = (ts_data->ztec.is_single_aod || ts_data->ztec.is_single_fp) ? 5 : 0;
	}
	FTS_INFO("ts_data->ztec.is_single_fp=%d", ts_data->ztec.is_single_fp);
	FTS_INFO("ts_data->ztec.is_single_aod=%d", ts_data->ztec.is_single_aod);
	FTS_INFO("ts_data->ztec.is_single_tap=%d", ts_data->ztec.is_single_tap);
	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_get_singlefpgesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_single_tap_enable = ts_data->ztec.is_single_fp;
	FTS_INFO("%s: enter!, ts_data->ztec.is_single_fp=%d", __func__, ts_data->ztec.is_single_fp);
	FTS_INFO("%s: enter!, cdev->b_single_tap_enable=%d", __func__, cdev->b_single_tap_enable);
	return 0;
}

static int tpd_set_singlefpgesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;
	FTS_INFO("%s: enter!, enable=%d", __func__, enable);
	ts_data->ztec.is_single_fp = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_single_fp = enable;
		ts_data->ztec.is_single_tap = (ts_data->ztec.is_single_aod || ts_data->ztec.is_single_fp) ? 5 : 0;
	}
	FTS_INFO("ts_data->ztec.is_single_fp=%d", ts_data->ztec.is_single_fp);
	FTS_INFO("ts_data->ztec.is_single_aod=%d", ts_data->ztec.is_single_aod);
	FTS_INFO("ts_data->ztec.is_single_tap=%d", ts_data->ztec.is_single_tap);
	return 0;
}
#endif

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_gesture_enable = ts_data->ztec.is_wakeup_gesture;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	ts_data->ztec.is_set_wakeup_in_suspend = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_wakeup_gesture = enable;
	}

	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_set_one_key(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	ts_data->ztec.is_set_onekey_in_suspend = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_one_key = enable;
	}

	return 0;
}

static int tpd_get_one_key(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->one_key_enable = ts_data->ztec.is_one_key;

	return 0;
}
#endif

/*static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_gesture_enable = ts_data->gesture_mode;
	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	if (fts_data->suspended) {
		cdev->tp_suspend_write_gesture = true;
	}
	ts_data->gesture_mode = enable;
	return enable;
}*/

static bool fts_suspend_need_awake(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	if (!cdev->tp_suspend_write_gesture &&
		(ts_data->fw_loading || ts_data->gesture_mode)) {
		FTS_INFO("tp suspend need awake.\n");
		return true;
	} else {
		FTS_INFO("tp suspend dont need awake.\n");
		return false;
	}
}

static int fts_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev = ts_data->input_dev;

	mutex_lock(&input_dev->mutex);
	fts_upgrade_bin(NULL, 0);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int fts_hw_reset(void)
{
	if (fts_data->ic_info.is_incell) {
		FTS_DEBUG("tp reset");
		gpio_direction_output(fts_data->pdata->reset_gpio, 0);
		usleep_range(5000, 6000);
		gpio_direction_output(fts_data->pdata->reset_gpio, 1);
	}
	return 0;
}

int fts_tp_suspend(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_suspend(ts_data->dev);
	return 0;
}

int fts_tp_resume(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_resume(ts_data->dev);
	return 0;
}

static int fts_tp_suspend_show(struct ztp_device *cdev)
{
	cdev->tp_suspend = fts_data->suspended;
	return cdev->tp_suspend;
}

static int fts_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable)
		change_tp_state(OFF);
	else
		change_tp_state(ON);	
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_INFO("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", fts_tptest_result, tdata->node.tx_num,
			tdata->node.rx_num, 0);
	num_read_chars = i_len;
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}

	input_dev = ts_data->input_dev;
	snprintf(g_fts_ini_filename, sizeof(g_fts_ini_filename), "fts_test_sensor_%02x.ini",
			tpd_cdev->ic_tpinfo.module_id);
	FTS_TEST_DBG("g_fts_ini_filename:%s.", g_fts_ini_filename);

	mutex_lock(&input_dev->mutex);
	fts_irq_disable();

#if FTS_ESDCHECK_EN
	fts_esdcheck_switch(DISABLE);
#endif

	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
	} else {
		fts_test_entry(g_fts_ini_filename);
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
	}
#if FTS_ESDCHECK_EN
	fts_esdcheck_switch(ENABLE);
#endif

	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int fts_headset_state_show(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->headset_state = ts_data->headset_mode;
	return cdev->headset_state;
}

static int fts_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;

	ts_data->headset_mode = enable;
	FTS_INFO("%s: headset_state = %d.\n", __func__, ts_data->headset_mode);
	if (!ts_data->suspended) {
		fts_ex_mode_switch(MODE_HEADSET, ts_data->headset_mode);
	}
	return ts_data->headset_mode;
}

static int fts_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;

	cdev->display_rotation = mrotation;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("%s: display_rotation = %d.\n", __func__, cdev->display_rotation);
	switch (cdev->display_rotation) {
		case mRotatin_0:
			ret = fts_write_reg(FTS_REG_MROTATION, 0);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_90:
			ret = fts_write_reg(FTS_REG_MROTATION, 1);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_180:
			ret = fts_write_reg(FTS_REG_MROTATION, 0);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_270:
			ret = fts_write_reg(FTS_REG_MROTATION, 2);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		default:
			break;
	}
	return cdev->display_rotation;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret = 0;

	if (tp_report_rate_level > 3)
		tp_report_rate_level = 3;
	ts_data->ztec.tp_report_rate = tp_report_rate_level;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no set report rate, return now");
		return -EINVAL;
	} else {
		/*0:in tp report mode->in 120Hz;
		  1:in tp report mode->in 240Hz;
		  2:in tp report mode->in 360Hz;
		  3:in tp report mode->in 480Hz;
		  */
		switch (tp_report_rate_level) {
			case tp_freq_120Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x0C);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_240Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x18);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_360Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x24);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_480Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x30);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			default:
				break;
		}
	}

	return 0;
}

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->tp_report_rate = ts_data->ztec.tp_report_rate;

	return 0;
}

static int tpd_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct fts_ts_data *ts_data = fts_data;

	if (ts_data->sensibility_level  == enable)  {
		FTS_INFO("same sensibility level,return");
		return 0;
	}

	ts_data->sensibility_level = enable;
	cdev->sensibility_level = enable;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("%s: sensibility_level = %d.\n", __func__, cdev->sensibility_level);
	retval = fts_write_reg(FTS_REG_SENSIBILITY_MODE_EN, ts_data->sensibility_level - 1);
	if (retval < 0) {
		FTS_ERROR("%s write sensibility_level fail", __func__);
	}
	return retval;
}

static int tpd_set_play_game(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no set play game, return now");
		return -EINVAL;
	} else {
		ts_data->ztec.is_play_game = enable;
		FTS_INFO("play_game mode is %d", enable);
		if (enable) {
			ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x30);
			/*ret = fts_write_reg(FTS_REG_GAME_MODE, 1);*/
			FTS_INFO("enter_play_game success\n");
		} else {
			ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x18);
			/*ret = fts_write_reg(FTS_REG_GAME_MODE, 0);*/
			FTS_INFO("leave_play_game success!\n");
		}
	}

	return 0;
}

static int tpd_get_play_game(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->play_game_enable = ts_data->ztec.is_play_game;

	return 0;
}


static int tpd_set_palm_mode(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;

	ts_data->ztec.is_palm_mode = enable;
	FTS_INFO("palm_mode is %d", enable);

	return 0;
}

static int tpd_get_palm_mode(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->palm_mode_en = ts_data->ztec.is_palm_mode;

	return 0;
}

static bool fts_get_charger_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}
	FTS_INFO("charger status:%d", status);
	return status;
}

static void fts_work_charger_detect_work(struct work_struct *work)
{
	struct fts_ts_data *ts_data = fts_data;
	bool charger_mode_old = ts_data->charger_mode;

	ts_data->charger_mode = fts_get_charger_status();
	if (!ts_data->suspended && (ts_data->charger_mode != charger_mode_old)) {
		FTS_INFO("write charger mode:%d", ts_data->charger_mode);
		fts_ex_mode_switch(MODE_CHARGER, ts_data->charger_mode);
	}
}

static int fts_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct fts_ts_data *ts_data = fts_data;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(ts_data->ts_workqueue, &ts_data->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int fts_init_charger_notifier(void)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;

	FTS_INFO("Init Charger notifier");

	ts_data->charger_notifier.notifier_call = fts_charger_notify_call;
	ret = power_supply_reg_notifier(&ts_data->charger_notifier);
	return ret;
}

static int tpd_fts_shutdown(struct ztp_device *cdev)
{
	FTS_INFO("disable irq");
	fts_irq_disable();
#if FTS_ESDCHECK_EN
	fts_esdcheck_switch(DISABLE);
#endif
#if FTS_POINT_REPORT_CHECK_EN
	cancel_delayed_work_sync(&fts_data->prc_work);
#endif
	cancel_delayed_work_sync(&fts_data->charger_work);
	gpio_direction_output(fts_data->pdata->reset_gpio, 0);
	return 0;
}

int tpd_register_fw_class(struct fts_ts_data *data)
{
	tpd_cdev->private = (void *)data;
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;

	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;

	tpd_cdev->get_singleaod = tpd_get_singleaodgesture;
	tpd_cdev->set_singleaod = tpd_set_singleaodgesture;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	tpd_cdev->get_singletap = tpd_get_singlefpgesture;
	tpd_cdev->set_singletap = tpd_set_singlefpgesture;
	tpd_cdev->set_one_key = tpd_set_one_key;
	tpd_cdev->get_one_key = tpd_get_one_key;
#endif

	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;
	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;
	tpd_cdev->tp_fw_upgrade = fts_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = fts_tp_suspend_show;
	tpd_cdev->set_tp_suspend = fts_set_tp_suspend;
	tpd_cdev->tpd_suspend_need_awake = fts_suspend_need_awake;
	tpd_cdev->tp_hw_reset = fts_hw_reset;
	tpd_cdev->set_display_rotation = fts_set_display_rotation;
	tpd_cdev->headset_state_show = fts_headset_state_show;
	tpd_cdev->set_headset_state = fts_set_headset_state;
	tpd_cdev->set_sensibility = tpd_set_sensibility;
	tpd_cdev->tp_data = data;
	tpd_cdev->tp_resume_func = fts_tp_resume;
	tpd_cdev->tp_suspend_func = fts_tp_suspend;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->tpd_shutdown = tpd_fts_shutdown;
	tpd_cdev->tp_palm_mode_read = tpd_get_palm_mode;
	tpd_cdev->tp_palm_mode_write = tpd_set_palm_mode;
	tpd_init_tpinfo(tpd_cdev);
	tpd_cdev->max_x = data->pdata->x_max;
	tpd_cdev->max_y = data->pdata->y_max;
	data->sensibility_level = 1;
	snprintf(g_fts_ini_filename, sizeof(g_fts_ini_filename), "fts_test_sensor_%02x.ini",
		tpd_cdev->ic_tpinfo.module_id);
	if (data->ts_workqueue) {
		INIT_DELAYED_WORK(&data->charger_work, fts_work_charger_detect_work);
		queue_delayed_work(data->ts_workqueue, &data->charger_work, msecs_to_jiffies(1000));
		fts_init_charger_notifier();
	}
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	zlog_tp_dev.device_name = FTS_MODULE_NAME;
	zlog_tp_dev.ic_name = "focal_tp";
	TPD_ZLOG("device_name:%s, device_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}

