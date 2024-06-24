/*
 * Goodix Gesture Module
 *
 * Copyright (C) 2019 - 2020 Goodix, Inc.
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
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include "goodix_ts_core.h"

#define QUERYBIT(longlong, bit) (!!(longlong[bit/8] & (1 << bit%8)))

#define GSX_GESTURE_TYPE_LEN	32

/*
 * struct gesture_module - gesture module data
 * @registered: module register state
 * @sysfs_node_created: sysfs node state
 * @gesture_type: valid gesture type, each bit represent one gesture type
 * @gesture_data: store latest gesture code get from irq event
 * @gesture_ts_cmd: gesture command data
 */
struct gesture_module {
	atomic_t registered;
	rwlock_t rwlock;
	u8 gesture_type[GSX_GESTURE_TYPE_LEN];
	u8 gesture_data;
	struct goodix_ext_module module;
};

static struct gesture_module *gsx_gesture;	/*allocated in gesture init module */
static bool module_initialized;

int goodix_gesture_enable(int enable)
{
	int ret = 0;

	if (!module_initialized)
		return 0;

	if (enable) {
		if (atomic_read(&gsx_gesture->registered))
			ts_info("gesture module has been already registered");
		else
			ret = goodix_register_ext_module_no_wait(&gsx_gesture->module);
	} else {
		if (!atomic_read(&gsx_gesture->registered))
			ts_info("gesture module has been already unregistered");
		else
			ret = goodix_unregister_ext_module(&gsx_gesture->module);
	}

	return ret;
}

/**
 * gsx_gesture_type_show - show valid gesture type
 *
 * @module: pointer to goodix_ext_module struct
 * @buf: pointer to output buffer
 * Returns >=0 - succeed,< 0 - failed
 */
static ssize_t gsx_gesture_type_show(struct goodix_ext_module *module,
				char *buf)
{
	int count = 0, i, ret = 0;
	unsigned char *type;

	type = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!type)
		return -ENOMEM;
	read_lock(&gsx_gesture->rwlock);
	for (i = 0; i < 256; i++) {
		if (QUERYBIT(gsx_gesture->gesture_type, i)) {
			count += scnprintf(type + count,
					   PAGE_SIZE, "%02x,", i);
		}
	}
	if (count > 0)
		ret = scnprintf(buf, PAGE_SIZE, "%s\n", type);
	read_unlock(&gsx_gesture->rwlock);

	kfree(type);
	return ret;
}

/**
 * gsx_gesture_type_store - set vailed gesture
 *
 * @module: pointer to goodix_ext_module struct
 * @buf: pointer to valid gesture type
 * @count: length of buf
 * Returns >0 - valid gestures, < 0 - failed
 */
static ssize_t gsx_gesture_type_store(struct goodix_ext_module *module,
		const char *buf, size_t count)
{
	int i;

	if (count <= 0 || count > 256 || buf == NULL) {
		ts_err("Parameter error");
		return -EINVAL;
	}

	write_lock(&gsx_gesture->rwlock);
	memset(gsx_gesture->gesture_type, 0, GSX_GESTURE_TYPE_LEN);
	for (i = 0; i < count; i++)
		gsx_gesture->gesture_type[buf[i] / 8] |= (0x1 << buf[i] % 8);
	write_unlock(&gsx_gesture->rwlock);

	return count;
}

static ssize_t gsx_gesture_enable_show(struct goodix_ext_module *module,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 atomic_read(&gsx_gesture->registered));
}

static ssize_t gsx_gesture_enable_store(struct goodix_ext_module *module,
		const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = strtobool(buf, &val);
	if (ret < 0)
		return ret;

	if (val) {
		ret = goodix_gesture_enable(1);
		return ret ? ret : count;
	}
	ret = goodix_gesture_enable(0);
	return ret ? ret : count;
}

static ssize_t gsx_gesture_data_show(struct goodix_ext_module *module,
				char *buf)
{
	ssize_t count;

	read_lock(&gsx_gesture->rwlock);
	count = scnprintf(buf, PAGE_SIZE, "gesture type code:0x%x\n",
			  gsx_gesture->gesture_data);
	read_unlock(&gsx_gesture->rwlock);

	return count;
}

const struct goodix_ext_attribute gesture_attrs[] = {
	__EXTMOD_ATTR(type, 0666, gsx_gesture_type_show,
		      gsx_gesture_type_store),
	__EXTMOD_ATTR(enable, 0666, gsx_gesture_enable_show,
		      gsx_gesture_enable_store),
	__EXTMOD_ATTR(data, 0444, gsx_gesture_data_show, NULL)
};

static int gsx_gesture_init(struct goodix_ts_core *cd,
		struct goodix_ext_module *module)
{
	if (!cd || !cd->hw_ops->gesture) {
		ts_err("gesture unsupported");
		return -EINVAL;
	}

	ts_info("gesture switch: ON");
	ts_debug("enable all gesture type");
	/* set all bit to 1 to enable all gesture wakeup */
	memset(gsx_gesture->gesture_type, 0xff, GSX_GESTURE_TYPE_LEN);
	atomic_set(&gsx_gesture->registered, 1);

	return 0;
}

static int gsx_gesture_exit(struct goodix_ts_core *cd,
		struct goodix_ext_module *module)
{
	if (!cd || !cd->hw_ops->gesture) {
		ts_err("gesture unsupported");
		return -EINVAL;
	}

	ts_info("gesture switch: OFF");
	ts_debug("disable all gesture type");
	memset(gsx_gesture->gesture_type, 0x00, GSX_GESTURE_TYPE_LEN);
	atomic_set(&gsx_gesture->registered, 0);

	return 0;
}

/* todo */
static int zte_gsx_enter_gesture_mode(struct goodix_ts_core *cd, int status)
{
	/*int is_init_cmd = 1;*/
	int ret = -EINVAL;
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	ts_info("%s: enter, status is %d", __func__, status);
	ret = hw_ops->gesture(cd, status);
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	if (cd->ztec.is_one_key)
		ret = hw_ops->set_onekey_switch(cd, 0);
	else
		ret = hw_ops->set_onekey_switch(cd, 1);
#endif
	return ret;
}

static int __gsx_gesture_before_suspend(struct goodix_ts_core *cd)
{
	int ret = 0;
	int status = 0;
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	ts_info("%s: enter!", __func__);
	if (hw_ops == NULL) {
		ts_err("Uninitialized doze command or hw_ops");
		goto exit;
	}

	/* one key is rely on fingerprint! do not test one_key here! */
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	status = (cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap;
	ts_info("status is %d", status);
	ts_info("(cd->ztec.is_wakeup_gesture) is %d", (cd->ztec.is_wakeup_gesture));
	ts_info("(cd->ztec.is_wakeup_gesture << 1) is %d", (cd->ztec.is_wakeup_gesture << 1));
	ts_info("(cd->ztec.is_single_tap) is %d", (cd->ztec.is_single_tap));
#else
	status = (cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap;
	ts_info("status is %d", status);
	ts_info("(cd->ztec.is_wakeup_gesture) is %d", (cd->ztec.is_wakeup_gesture));
	ts_info("(cd->ztec.is_single_tap) is %d", (cd->ztec.is_single_tap));
#endif
	if (status) {
		zte_gsx_enter_gesture_mode(cd, status);
		if (ret != 0) {
			goto exit;
		} else {
			ts_info("success enter gesture mode");
		}
		ret = EVT_CANCEL_SUSPEND;
	}

exit:
	return ret;
}

/**
 * gsx_gesture_ist - Gesture Irq handle
 * This functions is excuted when interrupt happended and
 * ic in doze mode.
 *
 * @cd: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_CANCEL_IRQEVT  stop execute
 */
static int gsx_gesture_ist(struct goodix_ts_core *cd,
	struct goodix_ext_module *module)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ts_event gs_event = { 0 };
	int ret;
	static int is_first_gesture = 0;

	if (atomic_read(&cd->suspended) == 0)
		return EVT_CONTINUE;

	ret = hw_ops->event_handler(cd, &gs_event);
	if (ret && is_first_gesture) {
		ts_err("failed get gesture data");
		is_first_gesture = 0;
		goto re_send_ges_cmd;
	} else if (ret && !is_first_gesture) {
		ts_err("gesture first failed");
		is_first_gesture = 1;
		goto gesture_ist_exit;
	} else {
		ts_info("gesture success");
		is_first_gesture = 0;
	}

	if (!(gs_event.event_type & EVENT_GESTURE)) {
		ts_err("invalid event type: 0x%x",
			cd->ts_event.event_type);
		goto re_send_ges_cmd;
	}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	if (gs_event.gesture_type == GSX_FP_TAP_AOD) {
		report_ufp_uevent(UFP_FP_DOWN);
		goto gesture_ist_exit;
	} else if (gs_event.gesture_type == GSX_FP_UP_AOD) {
		report_ufp_uevent(UFP_FP_UP);
		goto gesture_ist_exit;
	}
#endif
	if (QUERYBIT(gsx_gesture->gesture_type,
		     gs_event.gesture_type)) {
		gsx_gesture->gesture_data = gs_event.gesture_type;
		/* do resume routine */
		ts_info("got valid gesture type 0x%x",
			gs_event.gesture_type);
		if (gs_event.gesture_type == GSX_DOUBLE_TAP && cd->ztec.is_wakeup_gesture) {
			/* do resume routine */
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
			ts_info("enter:double_gesture");
			ufp_report_gesture_uevent(DOUBLE_TAP_GESTURE);
#else
			if (tpd_cdev->tpd_report_uevent != NULL) {
				ts_info("Double Click Uevent\n");
				tpd_cdev->tpd_report_uevent(double_tap);
			}
#endif
		}
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		else if (gs_event.gesture_type == GSX_SINGLE_TAP) {
			ts_info("enter:single_gesture");
			ufp_report_gesture_uevent(SINGLE_TAP_GESTURE);
		}
#else
		else if (gs_event.gesture_type == GSX_SINGLE_TAP) {
			ts_info("Single Click Uevent\n");
			tpd_cdev->tpd_report_uevent(single_tap);
		}
#endif
		goto gesture_ist_exit;
	} else {
		ts_info("unsupported gesture:%x", gs_event.gesture_type);
	}

re_send_ges_cmd:
	__gsx_gesture_before_suspend(cd);
gesture_ist_exit:
	if (!cd->tools_ctrl_sync)
		hw_ops->after_event_handler(cd);
	return EVT_CANCEL_IRQEVT;
}

/**
 * gsx_gesture_before_suspend - execute gesture suspend routine
 * This functions is excuted to set ic into doze mode
 *
 * @cd: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_IRQCANCLED  stop execute
 */
static int gsx_gesture_before_suspend(struct goodix_ts_core *cd,
	struct goodix_ext_module *module)
{
	int ret;
	/* const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops; */

	ret = __gsx_gesture_before_suspend(cd);
	ts_info("Set IC in  gesture mode");
	/*ret = hw_ops->gesture(cd, 0);*/
	if (ret) {
		ts_info("enter gesture mode");
	}
	return ret;
}

static int gsx_gesture_before_resume(struct goodix_ts_core *cd,
	struct goodix_ext_module *module)
{
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int status = 0;
	int ret = 0;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	status = (cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap;
#else
	status = cd->ztec.is_wakeup_gesture << 1;

#endif
	ts_info("status is %d", status);
	if (status) {
		hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
		ret = EVT_CANCEL_RESUME;
	}
	return ret;
}

static struct goodix_ext_module_funcs gsx_gesture_funcs = {
	.irq_event = gsx_gesture_ist,
	.init = gsx_gesture_init,
	.exit = gsx_gesture_exit,
	.before_suspend = gsx_gesture_before_suspend,
	.before_resume = gsx_gesture_before_resume,
};

int gesture_module_init(void)
{
	int ret;
	int i;
	struct kobject *def_kobj = goodix_get_default_kobj();
	struct kobj_type *def_kobj_type = goodix_get_default_ktype();

	gsx_gesture = kzalloc(sizeof(struct gesture_module), GFP_KERNEL);
	if (!gsx_gesture)
		return -ENOMEM;

	gsx_gesture->module.funcs = &gsx_gesture_funcs;
	gsx_gesture->module.priority = EXTMOD_PRIO_GESTURE;
	gsx_gesture->module.name = "Goodix_gsx_gesture";
	gsx_gesture->module.priv_data = gsx_gesture;

	atomic_set(&gsx_gesture->registered, 0);
	rwlock_init(&gsx_gesture->rwlock);

	/* gesture sysfs init */
	ret = kobject_init_and_add(&gsx_gesture->module.kobj,
			def_kobj_type, def_kobj, "gesture");
	if (ret) {
		ts_err("failed create gesture sysfs node!");
		goto err_out;
	}

	for (i = 0; i < ARRAY_SIZE(gesture_attrs) && !ret; i++)
		ret = sysfs_create_file(&gsx_gesture->module.kobj,
				&gesture_attrs[i].attr);
	if (ret) {
		ts_err("failed create gst sysfs files");
		while (--i >= 0)
			sysfs_remove_file(&gsx_gesture->module.kobj,
					&gesture_attrs[i].attr);

		kobject_put(&gsx_gesture->module.kobj);
		goto err_out;
	}

	goodix_register_ext_module(&(gsx_gesture->module));
	module_initialized = true;
	ts_info("gesture module init success");

	return 0;

err_out:
	ts_err("gesture module init failed!");
	kfree(gsx_gesture);
	return ret;
}

void gesture_module_exit(void)
{
	int i;

	ts_info("gesture module exit");
	if (!module_initialized)
		return;

	goodix_gesture_enable(0);

	/* deinit sysfs */
	for (i = 0; i < ARRAY_SIZE(gesture_attrs); i++)
		sysfs_remove_file(&gsx_gesture->module.kobj,
					&gesture_attrs[i].attr);

	kobject_put(&gsx_gesture->module.kobj);
	kfree(gsx_gesture);
	module_initialized = false;
}

