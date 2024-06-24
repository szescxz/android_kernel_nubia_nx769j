#ifndef _ZTP_UFP_H_
#define _ZTP_UFP_H_

#define ZTE_ONE_KEY

/* zte: add for fp uevent report */
#define SINGLE_TAP_GESTURE "single_tap=true"
#define DOUBLE_TAP_GESTURE "double_tap=true"
#define AOD_AREAMEET_DOWN "aod_areameet_down=true"
#define AREAMEET_DOWN "areameet_down=true"
#define AREAMEET_UP "areameet_up=true"

#define UFP_FP_DOWN 1
#define UFP_FP_UP 0

struct ufp_ops {
	struct platform_device *uevent_pdev;
	atomic_t ato_is_single_tap;
	struct delayed_work single_tap_work;
	struct workqueue_struct *single_tap_workqueue;
};

/* Log define */
#define UFP_INFO(fmt, arg...)	pr_info("tpd_ufp_info: "fmt"\n", ##arg)
#define UFP_ERR(fmt, arg...)	pr_err("tpd_ufp_err: "fmt"\n", ##arg)

extern struct ufp_ops ufp_tp_ops;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
void uf_touch_report(int is_down, int x, int y, int finger_id);
#endif
#ifdef ZTE_ONE_KEY
void one_key_report(int is_down, int x, int y, int finger_id);
#endif

int ufp_get_lcdstate(void);
void ufp_report_gesture_uevent(char *str);
void report_ufp_uevent(int enable);
int ufp_notifier_cb(int in_lp);
void ufp_report_lcd_state_delayed_work(u32 ms);

#endif /* __UFP_MAC_H */
