
/***********************
 * file : ztp_state_change.c
 ***********************/

#include "ztp_common.h"
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
#include "ztp_ufp.h"
#endif

static char *lcdstate_to_str[] = {
	"screen_on",
	"screen_off",
	"screen_in_doze",
};

static char *lcdchange_to_str[] = {
	"lcd_exit_lp",
	"lcd_enter_lp",
	"lcd_on",
	"lcd_off",
};
struct notifier_block tpd_nb;
atomic_t current_lcd_state = ATOMIC_INIT(SCREEN_ON);

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
extern struct ufp_ops ufp_tp_ops;
#endif
DEFINE_MUTEX(ufp_mac_mutex);

static inline void lcd_on_thing(void)
{
	struct ztp_device *cdev = tpd_cdev;

	queue_work(cdev->tpd_wq, &(cdev->resume_work));
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	atomic_set(&ufp_tp_ops.ato_is_single_tap, 0);
#endif
}

static inline void lcd_off_thing(void)
{
	struct ztp_device *cdev = tpd_cdev;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	atomic_set(&ufp_tp_ops.ato_is_single_tap, 0);
#endif
	queue_work(cdev->tpd_wq, &(cdev->suspend_work));
}

static inline void lcd_doze_thing(void)
{
	struct ztp_device *cdev = tpd_cdev;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	atomic_set(&ufp_tp_ops.ato_is_single_tap, 0);
#endif
	queue_work(cdev->tpd_wq, &(cdev->suspend_work));
}

static void screen_on(lcdchange lcd_change)
{
	switch (lcd_change) {
	case ENTER_LP:
		atomic_set(&current_lcd_state, DOZE);
		lcd_doze_thing();
		break;
	case OFF:
		atomic_set(&current_lcd_state, SCREEN_OFF);
		lcd_off_thing();
		break;
	default:
		UFP_ERR("ignore err lcd change");
	}
}

static void screen_off(lcdchange lcd_change)
{
	switch (lcd_change) {
	case ON:
		atomic_set(&current_lcd_state, SCREEN_ON);
		lcd_on_thing();
		break;
	case ENTER_LP:
/*		if (!atomic_read(&ufp_tp_ops.ato_is_single_tap))
			lcd_on_thing();
*/
		atomic_set(&current_lcd_state, DOZE);
		lcd_doze_thing();
		break;
/*	case OFF:
		atomic_set(&current_lcd_state, SCREEN_OFF);
		lcd_off_thing();
		UFP_ERR("err lcd off change");
		break;*/
	default:
		UFP_ERR("ignore err lcd change");
	}
}

static void doze(lcdchange lcd_change)
{
	switch (lcd_change) {
	case EXIT_LP:
		/* current_lcd_state = SCREEN_ON;
		lcd_on_thing();*/
		break;
	case OFF:
		atomic_set(&current_lcd_state, SCREEN_OFF);
		lcd_off_thing();
		break;
	case ON:
		atomic_set(&current_lcd_state, SCREEN_ON);
		lcd_on_thing();
		break;
	default:
		UFP_ERR("ignore err lcd change");
	}
}

void change_tp_state(lcdchange lcd_change)
{
	struct ztp_device *cdev = tpd_cdev;

	mutex_lock(&cdev->tp_resume_mutex);

	UFP_INFO("current_lcd_state:%s, lcd change:%s\n",
			lcdstate_to_str[atomic_read(&current_lcd_state)],
							lcdchange_to_str[lcd_change]);
	switch (atomic_read(&current_lcd_state)) {
	case SCREEN_ON:
		screen_on(lcd_change);
		break;
	case SCREEN_OFF:
		screen_off(lcd_change);
		break;
	case DOZE:
		doze(lcd_change);
		break;
	default:
		atomic_set(&current_lcd_state, SCREEN_ON);
		lcd_on_thing();
		UFP_ERR("err lcd light change");
	}

	mutex_unlock(&cdev->tp_resume_mutex);
}

static void tpd_resume_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_resume_func)
		cdev->tp_resume_func(cdev->tp_data);
}

static void tpd_suspend_work(struct work_struct *work)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_suspend_func)
		cdev->tp_suspend_func(cdev->tp_data);
}

int suspend_tp_need_awake(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tpd_suspend_need_awake) {
		return cdev->tpd_suspend_need_awake(cdev);
	}
	return 0;
}

bool tp_esd_check(void)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tpd_esd_check) {
		return cdev->tpd_esd_check(cdev);
	}
	return 0;
}

void set_lcd_reset_processing(bool enable)
{
	struct ztp_device *cdev = tpd_cdev;

	if (enable) {
		cdev->ignore_tp_irq = true;
	} else {
		cdev->ignore_tp_irq = false;
	}
	TPD_DMESG("cdev->ignore_tp_irq is %d.\n", cdev->ignore_tp_irq);
}

int set_gpio_mode(u8 mode)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->set_gpio_mode) {
		return cdev->set_gpio_mode(cdev, mode);
	}
	return -EIO;
}

void tpd_reset_gpio_output(bool value)
{
	struct ztp_device *cdev = tpd_cdev;

	if (cdev->tp_reset_gpio_output) {
		cdev->tp_reset_gpio_output(value);
	}
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
extern void ufp_report_lcd_state(void);

static void ufp_report_lcd_state_work(struct work_struct *work)
{
	ufp_report_lcd_state();
}

void ufp_report_lcd_state_delayed_work(u32 ms)
{
	struct ztp_device *cdev = tpd_cdev;

	mod_delayed_work(cdev->tpd_wq, &cdev->tpd_report_lcd_state_work, msecs_to_jiffies(ms));

}

void cancel_report_lcd_state_delayed_work(void)
{
	struct ztp_device *cdev = tpd_cdev;

	cancel_delayed_work_sync(&cdev->tpd_report_lcd_state_work);

}
#endif

void tpd_resume_work_init(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	INIT_WORK(&cdev->resume_work, tpd_resume_work);
	INIT_WORK(&cdev->suspend_work,tpd_suspend_work);
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	INIT_DELAYED_WORK(&cdev->tpd_report_lcd_state_work, ufp_report_lcd_state_work);
#endif

}

void tpd_resume_work_deinit(void)
{
	struct ztp_device *cdev = tpd_cdev;

	TPD_DMESG("%s enter", __func__);
	cancel_work_sync(&cdev->resume_work);
	cancel_work_sync(&cdev->suspend_work);
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	cancel_delayed_work_sync(&cdev->tpd_report_lcd_state_work);
#endif

}

#if 0
static int tpd_lcd_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct ztp_device *cdev = tpd_cdev;

	if (tpd_cdev == NULL) {
		TPD_DMESG("zte touch deinit, return\n", __func__, __LINE__);
		return -ENOMEM;
	}
	switch (event) {
	case LCD_POWER_ON:
		TPD_DMESG("%s: lcd power on\n", __func__);
		set_lcd_reset_processing(true);
		tpd_reset_gpio_output(1);
		break;
	case LCD_RESET:
		TPD_DMESG("%s: lcd reset\n", __func__);
		if (cdev->tp_resume_before_lcd_cmd)
			 change_tp_state(ON);
		break;
	case LCD_CMD_ON:
		TPD_DMESG("%s: lcd cmd on\n", __func__);
		if (!cdev->tp_resume_before_lcd_cmd)
			change_tp_state(ON);
		set_lcd_reset_processing(false);
		cdev->tp_suspend_write_gesture = false;
		break;
	case LCD_CMD_OFF:
		TPD_DMESG("%s: lcd cmd off\n", __func__);
		if (suspend_tp_need_awake())
			TPD_DMESG("%s: temp LCD_SUSPEND_POWER_ON\n", __func__);
			/*tpd_notifier_call_chain(LCD_SUSPEND_POWER_ON);*/
		else
			TPD_DMESG("%s: temp LCD_SUSPEND_POWER_OFF\n", __func__);
			/*tpd_notifier_call_chain(LCD_SUSPEND_POWER_OFF);*/
		change_tp_state(OFF);

		break;
	case LCD_POWER_OFF:
		TPD_DMESG("%s: lcd power off\n", __func__);
		break;
	case LCD_POWER_OFF_RESET_LOW:
		TPD_DMESG("%s: lcd power off reset low\n", __func__);
		 tpd_reset_gpio_output(0);
		break;
	case LCD_ENTER_AOD:
		TPD_DMESG("%s: lcd enter aod\n", __func__);
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		ufp_notifier_cb(true);
		ufp_report_lcd_state_delayed_work(50);
#endif
		break;
case LCD_EXIT_AOD:
		TPD_DMESG("%s: lcd exit aod\n", __func__);
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
		/*ufp_notifier_cb(false);*/
#endif
		break;
	default:
		TPD_DMESG("%s: lcd state unknown\n", __func__);
		break;
	}
	return 0;
}

void lcd_notify_register(void)
{
	int ret = 0;

	tpd_nb.notifier_call = tpd_lcd_notifier_callback;
	/*ret = lcd_notifer_register_client(&tpd_nb);*/
	if (ret) {
		TPD_DMESG(" Unable to register fb_notifier: %d\n", ret);
	}
	TPD_DMESG(" register lcd notifier success\n");
}
void lcd_notify_unregister(void)
{
	/*lcd_notifer_unregister_client(&tpd_nb);*/
}
#endif

