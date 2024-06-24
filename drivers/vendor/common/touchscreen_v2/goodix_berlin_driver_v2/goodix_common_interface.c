#include "goodix_ts_core.h"
#include <linux/i2c.h>

#ifdef GOODIX_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
#endif

#ifdef GOODIX_USB_DETECT_GLOBAL
extern bool GOODIX_USB_detect_flag;
#endif

#ifdef FOR_ZTE_CELL
extern bool smart_cover_flag;
#endif

#ifdef GTP_PINCTRL_EN
#define GTP_PINCTRL_INIT_STATE "gtp_gpio_init"
#endif

int is_game_mode;

static atomic_t ato_ver = ATOMIC_INIT(0);

extern struct goodix_bus_interface goodix_i2c_bus;
extern int gtp_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len);
extern void goodix_resume_work(struct work_struct *work);
extern int goodix_ts_suspend(struct goodix_ts_core *core_data);
extern int goodix_ts_power_off(struct goodix_ts_core *cd);
#define MAX_NAME_LEN_20  20
#define MAX_FILE_NAME_LEN       64
char gtp_vendor_name[MAX_NAME_LEN_20] = { 0 };
char gtp_cfg_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gtp_firmware_name[MAX_FILE_NAME_LEN] = {0};
struct tpvendor_t goodix_vendor_l[] = {
	{GTP_VENDOR_ID_0, GTP_VENDOR_0_NAME},
	{GTP_VENDOR_ID_1, GTP_VENDOR_1_NAME},
	{GTP_VENDOR_ID_2, GTP_VENDOR_2_NAME},
	{GTP_VENDOR_ID_3, GTP_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};
#ifdef GTP_PINCTRL_EN
int goodix_platform_pinctrl_init(struct goodix_ts_core *core_data)
{
	int ret = 0;

	ts_info("%s enter.\n", __func__);
	/* Get pinctrl if target uses pinctrl */
	core_data->ts_pinctrl = devm_pinctrl_get(goodix_i2c_bus.dev);
	if (IS_ERR_OR_NULL(core_data->ts_pinctrl)) {
		ret = PTR_ERR(core_data->ts_pinctrl);
		ts_err("Target does not use pinctrl %d\n", ret);
		goto err_pinctrl_get;
	}

	core_data->pinctrl_state_init
	    = pinctrl_lookup_state(core_data->ts_pinctrl, GTP_PINCTRL_INIT_STATE);
	if (IS_ERR_OR_NULL(core_data->pinctrl_state_init)) {
		ret = PTR_ERR(core_data->pinctrl_state_init);
		ts_err("Can not lookup %s pinstate %d\n", GTP_PINCTRL_INIT_STATE, ret);
		goto err_pinctrl_lookup;
	}
	ret = pinctrl_select_state(core_data->ts_pinctrl, core_data->pinctrl_state_init);
	if (ret < 0) {
		ts_err("failed to select pin to init state");
		goto err_select_init_state;
	}

	return 0;

err_select_init_state:
err_pinctrl_lookup:
	devm_pinctrl_put(core_data->ts_pinctrl);
err_pinctrl_get:
	core_data->ts_pinctrl = NULL;
	return ret;
}
#endif

int gtp_get_vendor_and_firmware(void)
{
	int i = 0;
	int ret = 0;
	const char *panel_name = NULL;

	panel_name = get_lcd_panel_name();
	for (i = 0; i < ARRAY_SIZE(goodix_vendor_l); i++) {
		if (strnstr(panel_name, goodix_vendor_l[i].vendor_name, strlen(panel_name))) {
			strlcpy(gtp_vendor_name, goodix_vendor_l[i].vendor_name, sizeof(gtp_vendor_name));
			ret = 0;
			goto out;
		}
	}
	ret = -1;
	strlcpy(gtp_vendor_name, GTP_VENDOR_0_NAME, sizeof(gtp_vendor_name));

out:
	snprintf(gtp_firmware_name, sizeof(gtp_firmware_name),
			"goodix_firmware_%s.bin", gtp_vendor_name);
	ts_info("GTP  firmware name :%s\n", gtp_firmware_name);
	snprintf(gtp_cfg_firmware_name, sizeof(gtp_cfg_firmware_name),
			"goodix_cfg_group_%s.bin", gtp_vendor_name);
	ts_info("GTP cfg firmware name :%s\n", gtp_cfg_firmware_name);
	return ret;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	int firmware;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct i2c_client *i2c = container_of(core_data->bus->dev, struct i2c_client, dev);
	struct goodix_fw_version chip_ver;
	int r = 0;
	char *cfg_buf;

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, tp in suspend!", __func__);
		return -EIO;
	}

	if (atomic_cmpxchg(&ato_ver, 0, 1)) {
		ts_err("busy, wait!");
		return -EIO;
	}

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	ts_info("%s: enter!", __func__);

	if (hw_ops->read_version) {
		r = hw_ops->read_version(core_data, &chip_ver);
		if (!r) {
			snprintf(cdev->ic_tpinfo.tp_name, MAX_VENDOR_NAME_LEN, "goodix:GT%s",
				chip_ver.patch_pid);

			firmware = (unsigned int)chip_ver.patch_vid[3] +
					((unsigned int)chip_ver.patch_vid[2] << 8) +
					((unsigned int)chip_ver.patch_vid[1] << 16) +
					((unsigned int)chip_ver.patch_vid[0] << 24);
			cdev->ic_tpinfo.firmware_ver = firmware;

			cdev->ic_tpinfo.chip_model_id = TS_CHIP_GOODIX;

			cdev->ic_tpinfo.module_id = chip_ver.sensor_id;

			cdev->ic_tpinfo.i2c_addr = i2c->addr;

			strlcpy(cdev->ic_tpinfo.vendor_name, gtp_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
		}
	} else {
		ts_err("%s: read_version failed!", __func__);
		goto exit;
	}

	if (hw_ops->read_config) {
		r = hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
		if (r <= 0)
			goto exit;

		cdev->ic_tpinfo.config_ver = cfg_buf[34];
	}

	ts_info("%s: end!", __func__);

exit:
	kfree(cfg_buf);
	atomic_cmpxchg(&ato_ver, 1, 0);

	return r;
}

static int tpd_get_singleaodgesture(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->b_single_aod_enable = core_data->ztec.is_single_aod;
	ts_info("%s: enter!, core_data->ztec.is_single_aod=%d", __func__, core_data->ztec.is_single_aod);
	ts_info("%s: enter!, cdev->b_single_aod_enable=%d", __func__, cdev->b_single_aod_enable);
	return 0;
}

static int tpd_set_singleaodgesture(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	ts_info("%s: enter!, enable=%d", __func__, enable);
	core_data->ztec.is_single_aod = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_aod = enable;
		core_data->ztec.is_single_tap = (core_data->ztec.is_single_aod || core_data->ztec.is_single_fp) ? 5 : 0;
	}
	ts_info("core_data->ztec.is_single_fp=%d", core_data->ztec.is_single_fp);
	ts_info("core_data->ztec.is_single_aod=%d", core_data->ztec.is_single_aod);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_get_singlefpgesture(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->b_single_tap_enable = core_data->ztec.is_single_fp;
	ts_info("%s: enter!, core_data->ztec.is_single_fp=%d", __func__, core_data->ztec.is_single_fp);
	ts_info("%s: enter!, cdev->b_single_tap_enable=%d", __func__, cdev->b_single_tap_enable);
	return 0;
}

static int tpd_set_singlefpgesture(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	ts_info("%s: enter!, enable=%d", __func__, enable);
	core_data->ztec.is_single_fp = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_fp = enable;
		core_data->ztec.is_single_tap = (core_data->ztec.is_single_aod || core_data->ztec.is_single_fp) ? 5 : 0;
	}
	ts_info("core_data->ztec.is_single_fp=%d", core_data->ztec.is_single_fp);
	ts_info("core_data->ztec.is_single_aod=%d", core_data->ztec.is_single_aod);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	return 0;
}
#endif

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->b_gesture_enable = core_data->ztec.is_wakeup_gesture;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	core_data->ztec.is_set_wakeup_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_wakeup_gesture = enable;
	}

	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_set_one_key(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	core_data->ztec.is_set_onekey_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_one_key = enable;
	}

	return 0;
}

static int tpd_get_one_key(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->one_key_enable = core_data->ztec.is_one_key;

	return 0;
}
#endif

static int tpd_set_finger_lock_flag(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	core_data->ztec.finger_lock_flag = enable;

	return 0;
}

static int tpd_get_finger_lock_flag(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->finger_lock_flag = core_data->ztec.finger_lock_flag;

	return 0;
}

#ifdef FOR_ZTE_CELL
static int tpd_get_smart_cover(struct ztp_device *cdev)
{
	int retval = 0;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->b_smart_cover_enable = core_data->ztec.is_smart_cover;

	return retval;
}

static int tpd_set_smart_cover(struct ztp_device *cdev, int enable)
{
	int ret = 0;

	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	struct goodix_ts_device *ts_dev = core_data->ts_dev;

	smart_cover_flag = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_smart_cover = enable;
		if (enable) {
			ret = hw_ops->set_enter_smart_cover(core_data);
			ts_info("enter_smart_cover success\n");
		} else {
			ret = hw_ops->set_leave_smart_cover(core_data);
			ts_info("leave_smart_cover success!\n");
		}
	}

	return 0;
}
#endif

#if 0
static int tpd_get_noise(struct ztp_device *cdev, struct list_head *head)
{
	int retval;
	int i = 0;
	char *buf_arry[RT_DATA_NUM];
	struct tp_runtime_data *tp_rt;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	list_for_each_entry(tp_rt, head, list) {
		buf_arry[i++] = tp_rt->rt_data;
		tp_rt->is_empty = false;
	}

	mutex_lock(&(core_data->ztec.rawdata_read_lock));
	retval = get_goodix_ts_rawdata(core_data, buf_arry, RT_DATA_NUM, RT_DATA_LEN);
	if (retval < 0) {
		pr_err("%s: get_raw_noise failed!\n", __func__);
	}
	mutex_unlock(&(core_data->ztec.rawdata_read_lock));

	return retval;
}
#endif

static int tpd_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (core_data->init_stage < CORE_INIT_STAGE2 || atomic_read(&core_data->suspended)) {
		ts_err("%s:error, change set in suspend!", __func__);
		return 0;
	}
	cdev->display_rotation = mrotation;
	ts_info("%s:display_rotation=%d", __func__, cdev->display_rotation);
	if (core_data->bus->ic_type == IC_TYPE_BERLIN_A) {
		switch (cdev->display_rotation) {
			case mRotatin_0:
				ret = hw_ops->set_vertical_panel(core_data);
				break;
			case mRotatin_90:
				ret = hw_ops->set_horizontal_panel(core_data, 1);
				break;
			case mRotatin_180:
				ret = hw_ops->set_vertical_panel(core_data);
				break;
			case mRotatin_270:
				ret = hw_ops->set_horizontal_panel(core_data, 2);
				break;
			default:
				break;
		}
	} else if (core_data->bus->ic_type == IC_TYPE_BERLIN_D) {
		ret = hw_ops->set_display_rotation(core_data, cdev->display_rotation);
	} else {
		ts_err("%s: not support", __func__);
	}
	if (ret) {
		ts_err("Write display rotation failed!");
	} else {
		ts_info("Write display rotation success!");
	}
	return cdev->display_rotation;
}


static int tpd_set_play_game(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (core_data->init_stage < CORE_INIT_STAGE2 || atomic_read(&core_data->suspended)) {
		/* we can not play game in black screen */
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_play_game = enable;
		is_game_mode = enable;
		ts_info("play_game mode is %d", enable);
		if (enable) {
			ret = hw_ops->set_zte_play_game(core_data, enable);
			ts_info("enter_play_game success\n");
		} else {
			ret = hw_ops->set_zte_play_game(core_data, enable);
			ts_info("leave_play_game success!\n");
		}
	}

	return 0;
}

static int tpd_get_play_game(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->play_game_enable = core_data->ztec.is_play_game;

	return 0;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (tp_report_rate_level > 3)
		tp_report_rate_level = 3;
	core_data->ztec.tp_report_rate = tp_report_rate_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		/*0:in tp report mode->in 120Hz;
		  1:in tp report mode->in 240Hz;
		  2:in tp report mode->in 360Hz;*/
		ts_info(" report rate mode is %d", tp_report_rate_level);
		ret = hw_ops->set_tp_report_rate(core_data, tp_report_rate_level);
		if (!ret)
			ts_info("set report rate mode success");
		else
			ts_err("set report rate mode failed!");
	}

	return 0;
}

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->tp_report_rate = core_data->ztec.tp_report_rate;

	return 0;
}


static int gtp_tp_suspend_show(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->tp_suspend = atomic_read(&core_data->suspended);
	return cdev->tp_suspend;
}

static int gtp_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
#ifndef CONFIG_TOUCHSCREEN_UFP_MAC
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	if (enable)
		goodix_ts_suspend(core_data);
	else
		queue_work(core_data->gtp_ts_workqueue, &core_data->gtp_resume_work);
#else
	if (enable)
		change_tp_state(OFF);
	else
		change_tp_state(ON);
#endif
	return 0;
}

static int tpd_get_sensibility(struct ztp_device *cdev)
{
	int retval = 0;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->sensibility_enable = core_data->ztec.is_sensibility;

	return retval;
}

static int goodix_ic_config_send(struct goodix_ts_core *cd, int type)
{
	int retval = 0;
	int retry = 0;
	u32 config_id;
	struct goodix_ic_config *cfg;

	if (type >= GOODIX_MAX_CONFIG_GROUP) {
		ts_err("unsupproted config type %d", type);
		return -EINVAL;
	}

	cfg = cd->ic_configs[type];
	if (!cfg || cfg->len <= 0) {
		ts_info("no valid normal config found");
		return -EINVAL;
	}
	config_id = goodix_get_file_config_id(cfg->data);
	ts_info("try send config, id=0x%x", config_id);
	do {
		retval = cd->hw_ops->send_config(cd, cfg->data, cfg->len);
		if (retval < 0) {
			retry++;
			ts_err("failed send config,retry%d", retry);
			msleep(20);
			retval = cd->hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
			msleep(20);
			if (retval) {
				ts_err("reset fail");
			}
		} else {
			break;
		}
	} while (retry < 3);

	return retval;
}

static int tpd_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	static u8 sensibility_level = 1;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	/*struct goodix_ts_device *ts_dev = core_data->ts_dev;*/

	if (sensibility_level == enable)  {
		ts_info("same sensibility level,return");
		return 0;
	}

	sensibility_level = enable;
	core_data->ztec.is_sensibility = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		if (enable == 3) {
			/* send ultra highsense_cfg to firmware */
			retval = goodix_ic_config_send(core_data, CONFIG_TYPE_ULTRA_HIGHSENSE);
			if (retval < 0) {
				ts_err("failed send ultra highsense config[ignore]");
			}
		} else if (enable == 2) {
			/* send highsense_cfg to firmware */
			retval = goodix_ic_config_send(core_data, CONFIG_TYPE_HIGHSENSE);
			if (retval < 0) {
				ts_err("failed send highsense config[ignore]");
			}
		} else if (enable == 1) {
			/* send normal-cfg to firmware */
			retval = goodix_ic_config_send(core_data, CONFIG_TYPE_NORMAL);
			if (retval < 0) {
				ts_err("failed send normal config[ignore]");
			}
		} else {
			ts_err("err sensibility data %d", enable);
		}

	}

	return retval;
}

static int tpd_get_pen_only_mode(struct ztp_device *cdev)
{
	ts_info(" pen only mode is %d",cdev->pen_only_mode);
	return cdev->pen_only_mode;
}

static int tpd_set_pen_only_mode(struct ztp_device *cdev, u8 pen_only_mode)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		cdev->pen_only_mode = pen_only_mode;
		ts_info(" pen only mode is %d",cdev->pen_only_mode);
		ret = hw_ops->set_tp_pen_only_mode(core_data, cdev->pen_only_mode);
		if (!ret)
			ts_info("set pen only mode success");
		else
			ts_err("set pen only  mode failed!");
	}

	return 0;
}
#ifdef GOODIX_USB_DETECT_GLOBAL
static bool goodix_get_charger_status(void)
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
	ts_info("charger status:%d", status);
	return status;
}

static void goodix_work_charger_detect_work(struct work_struct *work)
{
	int ret = -EINVAL;
	struct delayed_work *charger_work_delay = container_of(work, struct delayed_work, work);
	struct goodix_ts_core *core_data = container_of(charger_work_delay, struct goodix_ts_core, charger_work);
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	static int status = 0;

	ts_info("into charger detect");
	GOODIX_USB_detect_flag = goodix_get_charger_status();
	if (GOODIX_USB_detect_flag && !atomic_read(&core_data->suspended) && !status) {
		ret = hw_ops->set_enter_charger(core_data);
		status = 1;
	} else if (!GOODIX_USB_detect_flag && !atomic_read(&core_data->suspended) && status) {
		ret = hw_ops->set_leave_charger(core_data);
		status = 0;
	} else if (!GOODIX_USB_detect_flag && atomic_read(&core_data->suspended) && status) {
		status = 0;
	} else if (GOODIX_USB_detect_flag && atomic_read(&core_data->suspended) && !status) {
		status = 1;
	}

}

#ifdef PEN_LOW_BATT_CHECK
static void goodix_pen_low_batt_check_work(struct work_struct *work)
{
	struct delayed_work *pen_delay_work = container_of(work, struct delayed_work, work);
	struct goodix_ts_core *core_data = container_of(pen_delay_work, struct goodix_ts_core, pen_low_batt_check_work);

	core_data->pen_low_batt_enable = true;
}
#endif

static int goodix_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct goodix_ts_core *core_data = container_of(nb, struct goodix_ts_core, charger_notifier);

	/*ts_info("into charger notify");*/
	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(core_data->charger_wq, &core_data->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int goodix_init_charger_notifier(struct goodix_ts_core *core_data)
{
	int ret = 0;

	ts_info("Init Charger notifier");

	core_data->charger_notifier.notifier_call = goodix_charger_notify_call;
	ret = power_supply_reg_notifier(&core_data->charger_notifier);
	return ret;
}

#endif

static int tpd_get_noise(struct ztp_device *cdev)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *cd = (struct goodix_ts_core *)cdev->private;
	int tx = 0;
	int rx = 0;
	int ret = 0;
	int i =0 ;
	int index = 0;
	int frame = 0;
	int len = 0;

	if (cdev->tp_firmware != NULL) {
		if (cdev->tp_firmware->data != NULL)
			vfree(cdev->tp_firmware->data);
		kfree(cdev->tp_firmware);
	}
	cdev->fw_data_pos = 0;
	cdev->tp_firmware = kzalloc(sizeof(struct firmware), GFP_KERNEL);
	if (cdev->tp_firmware == NULL) {
			ts_info("tpd:alloc struct firmware failed");
			return -ENOMEM;
	}
	cdev->tp_firmware->data = vmalloc(sizeof(*cdev->tp_firmware) + (sizeof(*cdev->tp_firmware) + TS_RAWDATA_BUFF_MAX * 10));
	if (!cdev->tp_firmware->data) {
			ts_info("tpd: alloc tp_firmware->data failed");
			kfree(cdev->tp_firmware);
			return -ENOMEM;
	}
	memset((char *)cdev->tp_firmware->data, 0x00, (sizeof(*cdev->tp_firmware) + TS_RAWDATA_BUFF_MAX * 10));
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		vfree(cdev->tp_firmware->data);
		kfree(cdev->tp_firmware);
		ts_err("Failed to alloc rawdata info memory");
		return -ENOMEM;
	}
	for (frame = 0; frame  < 5; frame++) {
		ret = cd->hw_ops->get_capacitance_data(cd, info);
		if (ret < 0) {
			ts_err("failed to get_capacitance_data, exit!");
			goto exit;
		}
		rx = info->buff[0];
		tx = info->buff[1];
		len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", frame, tx, rx);
		len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len,
				"mutual_rawdata:\n");
		index = 2;
		for (i = 0; i < tx * rx; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len,
				"%5d,", info->buff[index + i]);
			if ((i + 1) % tx == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len, "\n");
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len, "mutual_diffdata:\n");
		index += tx * rx;
		for (i = 0; i < tx * rx; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len,
				"%3d,", info->buff[index + i]);
			if ((i + 1) % tx == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), TS_RAWDATA_BUFF_MAX * 10 - len, "\n");
		}
	}
exit:
	cdev->tp_firmware->size  = len;
	kfree(info);
	return ret;
}

static int tpd_goodix_shutdown(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	ts_info("disable irq");
	hw_ops->irq_enable(core_data, false);
	goodix_ts_power_off(core_data);
	gpio_direction_output(core_data->board_data.reset_gpio, 0);
	return 0;
}

void goodix_tpd_register_fw_class(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);

	ts_info("%s: entry", __func__);
#ifdef GOODIX_USB_DETECT_GLOBAL
	core_data->charger_wq = create_singlethread_workqueue("GOODIX_charger_detect");
	if (!core_data->charger_wq) {
		ts_err("allocate charger_wq failed");
	} else  {
		GOODIX_USB_detect_flag = goodix_get_charger_status();
		INIT_DELAYED_WORK(&core_data->charger_work, goodix_work_charger_detect_work);
		goodix_init_charger_notifier(core_data);
	}
#endif

	tpd_cdev->private = (void *)core_data;
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

	tpd_cdev->tp_suspend_show = gtp_tp_suspend_show;
	tpd_cdev->set_tp_suspend = gtp_set_tp_suspend;
	tpd_cdev->get_finger_lock_flag = tpd_get_finger_lock_flag;
	tpd_cdev->set_finger_lock_flag = tpd_set_finger_lock_flag;
#ifdef FOR_ZTE_CELL
	tpd_cdev->get_smart_cover = tpd_get_smart_cover;
	tpd_cdev->set_smart_cover = tpd_set_smart_cover;
#endif
/*
	tpd_cdev.get_noise = tpd_get_noise;
*/
	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;
	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;
	tpd_cdev->get_pen_only_mode = tpd_get_pen_only_mode;
	tpd_cdev->set_pen_only_mode = tpd_set_pen_only_mode;
	tpd_cdev->get_finger_lock_flag = tpd_get_finger_lock_flag;
	tpd_cdev->set_finger_lock_flag = tpd_set_finger_lock_flag;
	tpd_cdev->get_sensibility = tpd_get_sensibility;
	tpd_cdev->set_sensibility = tpd_set_sensibility;
	tpd_cdev->tp_fw_upgrade = gtp_tp_fw_upgrade;
	tpd_cdev->set_display_rotation = tpd_set_display_rotation;
	tpd_cdev->get_noise = tpd_get_noise;
	tpd_cdev->tpd_shutdown = tpd_goodix_shutdown;

	core_data->gtp_ts_workqueue = create_singlethread_workqueue("gtp ts workqueue");
	if (!core_data->gtp_ts_workqueue) {
		ts_err("allocate gtp_ts_workqueue failed");
	} else {
		ts_info("allocate gtp_ts_workqueue success");
		INIT_WORK(&core_data->gtp_resume_work, goodix_resume_work);
#ifdef PEN_LOW_BATT_CHECK
		INIT_DELAYED_WORK(&core_data->pen_low_batt_check_work, goodix_pen_low_batt_check_work);
		core_data->pen_low_batt_enable = true;
#endif
	}
	tpd_cdev->max_x = ts_bdata->panel_max_x;
	tpd_cdev->max_y = ts_bdata->panel_max_y;

	ts_info("%s: end", __func__);
}
