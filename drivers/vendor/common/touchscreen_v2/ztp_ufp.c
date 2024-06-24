#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/compat.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>

struct wakeup_source tp_wakeup;

#include "ztp_common.h"

#define SINGLE_TAP_DELAY	600

bool aod_down_flag = false;

int finger_retry;


#ifdef ZTE_ONE_KEY
#define MAX_POINTS_SUPPORT 10
#define FP_GESTURE_DOWN	"fp_gesture_down=true"
#define FP_GESTURE_UP	"fp_gesture_up=true"

static char *one_key_finger_id[] = {
	"finger_id=0",
	"finger_id=1",
	"finger_id=2",
	"finger_id=3",
	"finger_id=4",
	"finger_id=5",
	"finger_id=6",
	"finger_id=7",
	"finger_id=8",
	"finger_id=9",
};
#endif

static char *tppower_to_str[] = {
	"TP_POWER_STATUS=2",		/* TP_POWER_ON */
	"TP_POWER_STATUS=1",		/* TP_POWER_OFF */
	"TP_POWER_STATUS=3",		/* TP_POWER_AOD */
};

struct ufp_ops ufp_tp_ops;

extern atomic_t current_lcd_state;

int ufp_get_lcdstate(void)
{
	return atomic_read(&current_lcd_state);
}

static void ufp_single_tap_work(struct work_struct *work)
{
	atomic_set(&ufp_tp_ops.ato_is_single_tap, 0);
}

void ufp_report_gesture_uevent(char *str)
{
	char *envp[2];
	envp[0] = str;
	envp[1] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);

        __pm_wakeup_event(&tp_wakeup, 2000);

	if (!strcmp(str, SINGLE_TAP_GESTURE)) {
		atomic_set(&ufp_tp_ops.ato_is_single_tap, 1);
		mod_delayed_work(ufp_tp_ops.single_tap_workqueue,
				&ufp_tp_ops.single_tap_work, msecs_to_jiffies(SINGLE_TAP_DELAY));
	} else if (!strcmp(str, DOUBLE_TAP_GESTURE))
		mod_delayed_work(ufp_tp_ops.single_tap_workqueue,
						&ufp_tp_ops.single_tap_work, 0);
	UFP_INFO("%s", str);
}

static inline void __report_ufp_uevent(char *str)
{
	char *envp[3];

	if (!ufp_tp_ops.uevent_pdev) {
		UFP_ERR("uevent pdev is null!\n");
		return;
	}

	if (!strcmp(str, AOD_AREAMEET_DOWN))
		ufp_report_gesture_uevent(SINGLE_TAP_GESTURE);

	envp[0] = str;
	envp[1] = tppower_to_str[atomic_read(&current_lcd_state)];
	envp[2] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);
	UFP_INFO("%s,lcd state=%s", str, envp[1]);
}

void report_ufp_uevent(int enable)
{
	static int area_meet_down = 0;

	if (enable && !area_meet_down) {
		area_meet_down = 1;
		if (atomic_read(&current_lcd_state) == SCREEN_ON) {/* fp func enable is guaranted by user*/
			__report_ufp_uevent(AREAMEET_DOWN);
		 } else {
			__report_ufp_uevent(AOD_AREAMEET_DOWN);
			aod_down_flag = true;
			finger_retry = 100;
		}
	} else if (!enable && area_meet_down) {
			area_meet_down = 0;
			__report_ufp_uevent(AREAMEET_UP);
			aod_down_flag = false;
	}
}
static inline int zte_in_zeon(int x, int y)
{
	int ret = 0;
	struct ztp_device *cdev = tpd_cdev;

	if ((cdev->ufp_circle_center_x - cdev->ufp_circle_radius < x) &&
		(cdev->ufp_circle_center_x  + cdev->ufp_circle_radius > x) &&
		(cdev->ufp_circle_center_y  - cdev->ufp_circle_radius < y) &&
		(cdev->ufp_circle_center_y + cdev->ufp_circle_radius > y)) {
		ret = 1;
	}

	return ret;
}

#ifdef ZTE_ONE_KEY
static inline void report_one_key_uevent(char *str, int i)
{
	char *envp[3];

	envp[0] = str;
	envp[1] = one_key_finger_id[i];
	envp[2] = NULL;
	kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);
	UFP_INFO("%s", str);
}

/* We only track the first finger in zeon */
void one_key_report(int is_down, int x, int y, int finger_id)
{
	int retval;
	static char one_key_finger[MAX_POINTS_SUPPORT] = {0};
	static int one_key_down = 0;

	if (is_down) {
		retval = zte_in_zeon(x, y);
		if (retval && !one_key_finger[finger_id] && !one_key_down) {
			one_key_finger[finger_id] = 1;
			one_key_down = 1;
			report_one_key_uevent(FP_GESTURE_DOWN, finger_id);
		}
	} else if (one_key_finger[finger_id]) {
			one_key_finger[finger_id] = 0;
			one_key_down = 0;
			report_one_key_uevent(FP_GESTURE_UP, finger_id);
	}
}
#endif

/*temp */
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
/* We only track the first finger in zeon */
void uf_touch_report(int is_down, int x, int y, int finger_id)
{
	int retval;
	static int fp_finger[MAX_POINTS_SUPPORT] = { 0 };
	static int area_meet_down = 0;

	if (is_down) {
		retval = zte_in_zeon(x, y);
		if (retval && !fp_finger[finger_id] && !area_meet_down) {
			fp_finger[finger_id] = 1;
			area_meet_down = 1;
			__report_ufp_uevent(AREAMEET_DOWN);
		}
	} else if (fp_finger[finger_id]) {
			fp_finger[finger_id] = 0;
			area_meet_down = 0;
			__report_ufp_uevent(AREAMEET_UP);
	}
}
#endif

static inline void report_lcd_uevent(struct kobject *kobj, char **envp)
{
	int retval;

	envp[0] = "aod=true";
	envp[1] = NULL;
	UFP_INFO("uevent:aod=true\n");
	retval = kobject_uevent_env(kobj, KOBJ_CHANGE, envp);
	if (retval != 0)
		UFP_ERR("lcd state uevent send failed!\n");
}

void ufp_report_lcd_state(void)
{
	char *envp[2];

	if (!ufp_tp_ops.uevent_pdev) {
		UFP_ERR("uevent pdev is null!\n");
		return;
	}

	report_lcd_uevent(&(ufp_tp_ops.uevent_pdev->dev.kobj), envp);
}
EXPORT_SYMBOL(ufp_report_lcd_state);

/*for lcd low power mode*/
int ufp_notifier_cb(int in_lp)
{
	int retval = 0;

	if (in_lp)
		change_tp_state(ENTER_LP);
	else
		change_tp_state(EXIT_LP);

	return retval;
}
EXPORT_SYMBOL(ufp_notifier_cb);

int ufp_mac_init(void)
{
	ufp_tp_ops.single_tap_workqueue =
			create_singlethread_workqueue("single_tap_cancel");
	INIT_DELAYED_WORK(&ufp_tp_ops.single_tap_work, ufp_single_tap_work);

	atomic_set(&ufp_tp_ops.ato_is_single_tap, 0);

        wakeup_source_add(&tp_wakeup);

	if (tpd_cdev->zte_touch_pdev)
		ufp_tp_ops.uevent_pdev = tpd_cdev->zte_touch_pdev;
	return 0;
}

void  ufp_mac_exit(void)
{
	cancel_delayed_work_sync(&ufp_tp_ops.single_tap_work);
	flush_workqueue(ufp_tp_ops.single_tap_workqueue);
	destroy_workqueue(ufp_tp_ops.single_tap_workqueue);

	wakeup_source_remove(&tp_wakeup);

	ufp_tp_ops.uevent_pdev = NULL;
}

