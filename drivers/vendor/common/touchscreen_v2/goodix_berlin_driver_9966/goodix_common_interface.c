#include "goodix_ts_core.h"
#include <linux/i2c.h>

#ifdef GOODIX_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
extern bool GOODIX_USB_detect_flag;
#endif

static atomic_t ato_ver = ATOMIC_INIT(0);

#define MAX_NAME_LEN_20  20
char gtp_vendor_name[MAX_NAME_LEN_20] = { 0 };

extern int goodix_do_inspect(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info);

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


static int tpd_get_fold_state(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->fold_state = core_data->ztec.is_fold_state;

	return 0;
}

static int tpd_set_fold_state(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	core_data->ztec.is_fold_state = enable;
	cdev->fold_state = enable;
	if (core_data->init_stage < CORE_INIT_STAGE2 || atomic_read(&core_data->suspended)) {
		/* we can not set fold mode in black screen */
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ts_info("%s: fold state is %d", __func__, core_data->ztec.is_fold_state);
		ret = hw_ops->set_zte_fold_mode(core_data, enable);
	}

	return 0;
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
	int mark = 0;

	if(enable){
		mark = 1;
		core_data->ztec.is_single_aod = mark;
	} else {
		core_data->ztec.is_single_aod = mark;
	}

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_aod = mark;
		core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp;
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
	int mark = 0;

	if(enable){
		mark = 4;
		core_data->ztec.is_single_fp = mark;
	} else {
		core_data->ztec.is_single_fp = mark;
	}

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_fp = mark;
		core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp;
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

#if 0
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

static int tpd_set_rotation_limit_level(struct ztp_device *cdev, int level)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (level > 3)
		level = 3;
	core_data->ztec.rotation_limit_level = level;

	if (core_data->bus->ic_type == IC_TYPE_BERLIN_D) {
		ret = hw_ops->set_display_rotation(core_data, cdev->display_rotation);
	} else {
		ts_err("%s: not support", __func__);
	}

	return 0;
}

static int tpd_get_rotation_limit_level(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->rotation_limit_level = core_data->ztec.rotation_limit_level;

	return 0;

}
static int tpd_set_sensibility_level(struct ztp_device *cdev, u8 tp_sensibility_level)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (tp_sensibility_level > 4)
		tp_sensibility_level = 4;
	core_data->ztec.sensibility_level = tp_sensibility_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ts_info("tp_sensibility_level mode is %d", tp_sensibility_level);
		ret = hw_ops->set_sensibility(core_data, tp_sensibility_level);
		if (!ret)
			ts_info("set sensibility_level mode success");
		else
			ts_err("set sensibility_level mode failed!");
	}

	return 0;
}

static int tpd_get_sensibility_level(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->sensibility_level = core_data->ztec.sensibility_level;

	return 0;
}

static int tpd_set_follow_hand_level(struct ztp_device *cdev, int tp_follow_hand_level)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (tp_follow_hand_level > 4)
		tp_follow_hand_level = 4;
	core_data->ztec.follow_hand_level = tp_follow_hand_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ts_info("tp_follow_hand_level mode is %d", tp_follow_hand_level);
		ret = hw_ops->set_follow_hand_level(core_data, tp_follow_hand_level);
		if (!ret)
			ts_info("set follow_hand_level mode success");
		else
			ts_err("set follow_hand_level mode failed!");
	}

	return 0;
}

static int tpd_get_follow_hand_level(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->follow_hand_level = core_data->ztec.follow_hand_level;

	return 0;
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
	core_data->ztec.display_rotation = mrotation;
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

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->tp_report_rate = core_data->ztec.tp_report_rate;

	return 0;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (tp_report_rate_level > 4)
		tp_report_rate_level = 4;
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

static int tpd_set_palm_mode(struct ztp_device *cdev, int enable)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	core_data->ztec.is_palm_mode = enable;
	ts_info("palm_mode is %d", enable);

	return 0;
}

static int tpd_get_palm_mode(struct ztp_device *cdev)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

	cdev->palm_mode_en = core_data->ztec.is_palm_mode;

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
#endif/*GOODIX_USB_DETECT_GLOBAL*/

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

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	ts_info("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", 0, 16,
			37, 0);
	num_read_chars = i_len;
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	struct ts_rawdata_info *info = NULL;
	struct goodix_ts_core *cd = (struct goodix_ts_core *)cdev->private;
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (atomic_read(&cd->suspended)) {
		ts_info("In suspend, no test, return now");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	goodix_do_inspect(cd, info);

	ts_info("test_result:%s", info->result);
	hw_ops->set_zte_fold_mode(cd, cd->ztec.is_fold_state);
	ts_info("%s: fold state is %d", __func__, cd->ztec.is_fold_state);
	kfree(info);

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

	tpd_cdev->get_singleaod = tpd_get_singleaodgesture;
	tpd_cdev->set_singleaod = tpd_set_singleaodgesture;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	tpd_cdev->get_singletap = tpd_get_singlefpgesture;
	tpd_cdev->set_singletap = tpd_set_singlefpgesture;
	tpd_cdev->set_one_key = tpd_set_one_key;
	tpd_cdev->get_one_key = tpd_get_one_key;
#endif

	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;

	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;

#if 0
	tpd_cdev->get_finger_lock_flag = tpd_get_finger_lock_flag;
	tpd_cdev->set_finger_lock_flag = tpd_set_finger_lock_flag;

	tpd_cdev->get_sensibility = tpd_get_sensibility_level;
	tpd_cdev->set_sensibility = tpd_set_sensibility_level;

	tpd_cdev->get_follow_hand_level = tpd_get_follow_hand_level;
	tpd_cdev->set_follow_hand_level = tpd_set_follow_hand_level;

	tpd_cdev->get_rotation_limit_level = tpd_get_rotation_limit_level;
	tpd_cdev->set_rotation_limit_level = tpd_set_rotation_limit_level;
#endif

	tpd_cdev->set_display_rotation = tpd_set_display_rotation;

	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;

	tpd_cdev->tp_palm_mode_read = tpd_get_palm_mode;
	tpd_cdev->tp_palm_mode_write = tpd_set_palm_mode;

	tpd_cdev->get_noise = tpd_get_noise;

	tpd_cdev->tp_fold_state_read = tpd_get_fold_state;
	tpd_cdev->tp_fold_state_write = tpd_set_fold_state;

	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;

	tpd_cdev->max_x = ts_bdata->panel_max_x;
	tpd_cdev->max_y = ts_bdata->panel_max_y;

	ts_info("%s: end", __func__);
}
