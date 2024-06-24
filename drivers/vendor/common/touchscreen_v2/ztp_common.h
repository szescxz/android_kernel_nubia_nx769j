/*
 * FILE: ztp_common.h
 *
 */
#ifndef __ZTP_COMMON_H_
#define __ZTP_COMMON_H_

#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/printk.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
#include <vendor/comdef/zlog_common_base.h>
#endif
#include <linux/firmware.h>
#include "ztp_ufp.h"
#include <linux/jiffies.h>

#ifdef TPD_DMESG
#undef TPD_DMESG
#endif
#define TPD_DMESG(a, arg...) pr_notice("tpd: " a, ##arg)
#define TPD_ZLOG(a, arg...) pr_notice("tpd_zlog: " a, ##arg)

#define CONFIG_CREATE_TPD_SYS_INTERFACE
/*#define TOUCH_DOWN_UP_ZLOG*/

#define PROC_TOUCH_DIR					"touchscreen"
#define PROC_TOUCH_INFO					"ts_information"
#define PROC_TOUCH_FW_UPGRADE			"FW_upgrade"
#define PROC_TOUCH_SMART_COVER		"smart_cover"
#define PROC_TOUCH_GLOVE				"glove_mode"
#define PROC_TOUCH_WAKE_GESTURE		"wake_gesture"
#define PROC_TOUCH_SUSPEND		"suspend"
#define PROC_TOUCH_HEADSET_STATE		"headset_state"
#define PROC_TOUCH_ROTATION_LIMIT_LEVEL   "rotation_limit_level"
#define PROC_TOUCH_MROTATION		"mRotation"
#define PROC_TOUCH_TP_SINGLETAP		"single_tap"
#define PROC_TOUCH_TP_SINGLEAOD		"single_aod"
#define PROC_TOUCH_GET_NOISE		"get_noise"
#define PROC_TOUCH_EDGE_REPORT_LIMIT		"edge_report_limit"
#define PROC_TOUCH_ONEKEY			"one_key"
#define PROC_TOUCH_PLAY_GAME		"play_game"
#define PROC_TOUCH_SENSIBILITY		"sensibility"
#define PROC_TOUCH_TP_REPORT_RATE		"tp_report_rate"
#define PROC_TOUCH_FOLLOW_HAND_LEVEL    "follow_hand_level"
#define PROC_TOUCH_PEN_ONLY		"pen_only"
#define PROC_TOUCH_FINGER_LOCK_FLAG     "finger_lock_flag"
#define PROC_TOUCH_TP_SELF_TEST		"tp_self_test"
#define PROC_TOUCH_TP_PALM_MODE		"tp_palm_mode"
#define PROC_TOUCH_TP_FOLD_STATE	"fold_state"
#define PROC_TOUCH_FAKE_SLEEP			"fake_sleep"
#ifdef TOUCH_DOWN_UP_ZLOG
#define PROC_TOUCH_GHOST_DEBUG		"ghost_debug"
#endif
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
#define PROC_ZLOG_DEBUG	"zlog_debug"
#endif

#define  MAX_VENDOR_NAME_LEN 40
#define  MAX_LIMIT_NUM 4
#define VENDOR_END 0xff

#define BLANK		1
#define UNBLANK		0
#define K			(1024)
#define MM			(K * K)
#define RT_DATA_LEN	(4 * K)
#define TP_TEST_FILE_SIZE		(1 * MM)

#ifdef TOUCH_DOWN_UP_ZLOG
bool tp_ghost_check(void);
void ghost_check_reset(void);
#endif

enum ts_chip {
	TS_CHIP_INDETER		= 0x00,
	TS_CHIP_SYNAPTICS	= 0x01,
	TS_CHIP_ATMEL		= 0x02,
	TS_CHIP_CYTTSP		= 0x03,
	TS_CHIP_FOCAL		= 0x04,
	TS_CHIP_GOODIX		= 0x05,
	TS_CHIP_MELFAS		= 0x06,
	TS_CHIP_MSTAR		= 0x07,
	TS_CHIP_HIMAX		= 0x08,
	TS_CHIP_NOVATEK		= 0x09,
	TS_CHIP_ILITEK		= 0x0A,
	TS_CHIP_TLSC		= 0x0B,
	TS_CHIP_CHIPONE	= 0x0C,
	TS_CHIP_HYNITRON	= 0x0D,
	TS_CHIP_GTX8		= 0x0E,
	TS_CHIP_GT9897		= 0x0E,
	TS_CHIP_GCORE		= 0x0F,
	TS_CHIP_SITRONIX	= 0x10,
	TS_CHIP_OMNIVISION	= 0x11,
	TS_CHIP_MAX		= 0xFF,
};

enum tp_test_type {
	RAWDATA_TEST = 0,
	DELTA_TEST = 1,
};

enum {
	PROC_SUSPEND_NODE = 0,
	SYS_SUSPEND_NODE = 1,
};

enum {
	mRotatin_0 = 0,
	mRotatin_90 = 1,
	mRotatin_180 = 2,
	mRotatin_270 = 3,
};

enum {
	rotation_limit_level_0 = 0,
	rotation_limit_level_1 = 1,
	rotation_limit_level_2 = 2,
	rotation_limit_level_3 = 3,
};

enum {
	tp_freq_120Hz = 0,
	tp_freq_240Hz = 1,
	tp_freq_360Hz = 2,
	tp_freq_480Hz = 3,
	tp_freq_960Hz = 4,
};

enum {
	sensibility_level_0 = 0,
	sensibility_level_1 = 1,
	sensibility_level_2 = 2,
	sensibility_level_3 = 3,
	sensibility_level_4 = 4,
};

enum {
	follow_hand_level_0 = 0,
	follow_hand_level_1 = 1,
	follow_hand_level_2 = 2,
	follow_hand_level_3 = 3,
	follow_hand_level_4 = 4,
};

enum {
	single_tap,
	double_tap,
	pen_low_batt,
};

enum {
	gpio_mode = 0,
	spi_mode =1,
};

typedef enum lcdstate {
	SCREEN_ON = 0,
	SCREEN_OFF,
	DOZE,
} lcdstate;

typedef enum lcdchange {
	EXIT_LP = 0,
	ENTER_LP,
	ON,
	OFF,
} lcdchange;


struct tpvendor_t {
	int vendor_id;
	char *vendor_name;
};

struct tp_point_log {
	unsigned int x;
	unsigned int y;
};

struct tp_ic_vendor_info {
	u8 tp_chip_id;
	char *tp_ic_vendor_name;
};

enum ztp_algo_item {
	zte_algo_enable = 0,
	tp_jitter_check_pixel,
	tp_jitter_timer,
	tp_edge_click_suppression_pixel,
	tp_long_press_enable,
	tp_long_press_timer,
	tp_long_press_pixel,
};

struct ztp_algo_info {
	u8 ztp_algo_item_id;
	char *ztp_algo_item_name;
};

struct tpd_tpinfo_t {
	unsigned int chip_model_id;
	unsigned int chip_part_id;
	unsigned int chip_ver;
	unsigned int module_id;
	unsigned int firmware_ver;
	unsigned int config_ver;
	unsigned int display_ver;
	unsigned int i2c_addr;
	unsigned int i2c_type;
	unsigned int spi_num;
	char tp_name[MAX_VENDOR_NAME_LEN];
	char vendor_name[MAX_VENDOR_NAME_LEN];
	char chip_batch[MAX_VENDOR_NAME_LEN];
	char fw_update_status[MAX_VENDOR_NAME_LEN];
};

struct ts_firmware {
	u8 *data;
	int size;
};

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
typedef enum zlog_error_no {
	TP_I2C_R_ERROR_NO = 1,
	TP_I2C_W_ERROR_NO,
	TP_I2C_R_WARN_NO,
	TP_I2C_W_WARN_NO,
	TP_SPI_R_ERROR_NO,
	TP_SPI_W_ERROR_NO,
	TP_SPI_R_WARN_NO,
	TP_SPI_W_WARN_NO,
	TP_CRC_ERROR_NO,
	TP_FW_UPGRADE_ERROR_NO,
	TP_ESD_CHECK_ERROR_NO,
	TP_ESD_CHECK_WARN_NO,
	TP_PROBE_ERROR_NO,
	TP_SUSPEND_GESTURE_OPEN_NO,
	TP_REQUEST_FIRMWARE_ERROR_NO,
#ifdef TOUCH_DOWN_UP_ZLOG
	TP_GHOST_ERROR_NO,
#endif
	TP_ZLOG_ERROR_MAX,
} zlog_error_no;

struct zlog_error_item {
	unsigned long  count[TP_ZLOG_ERROR_MAX];
	unsigned long  timer[TP_ZLOG_ERROR_MAX];
};

extern struct zlog_mod_info zlog_tp_dev;
#define ZLOG_INFO_LEN (2 * 1024)
void tpd_zlog_record_notify(zlog_error_no error_no);
#define tpd_print_zlog(fmt, args...) do {\
	if (tpd_cdev->ztp_zlog_buffer != NULL) { \
		snprintf(tpd_cdev->ztp_zlog_buffer, ZLOG_INFO_LEN , fmt, ##args);\
	} \
} while (0)
#endif

struct ztp_device {
	int b_gesture_enable;
	int b_smart_cover_enable;
	int b_glove_enable;
	int rotation_limit_level;
	int display_rotation;
	bool TP_have_registered;
	bool tp_suspend;
	bool sys_set_tp_suspend_flag;
	bool headset_state;
	bool ignore_tp_irq;
	bool tp_suspend_write_gesture;
	bool tp_resume_before_lcd_cmd;
	bool zte_tp_algo;
	bool ufp_enable;
#ifdef TOUCH_DOWN_UP_ZLOG
	bool start_ghost_check_timer;
#endif
	u16 ufp_circle_center_x;
	u16 ufp_circle_center_y;
	u16 ufp_circle_radius;
	u8 tp_jitter_check;
	bool edge_long_press_check;
	u8 edge_click_sup_p;
	u8 edge_report_limit[MAX_LIMIT_NUM];
	u16 user_edge_limit[MAX_LIMIT_NUM];
	u8 long_pess_suppression[MAX_LIMIT_NUM];
	u8 edge_limit_pixel_level;
	u16 long_press_max_count;
	u16 edge_long_press_timer;
	u16 tp_jitter_timer;
	u8 sensibility_level;
	u8 pen_only_mode;
	u16 max_x;
	u16 max_y;
	u8 tp_chip_id;
	u32 fw_data_pos;
	int b_single_tap_enable;
	int b_single_aod_enable;
	int one_key_enable;
	int play_game_enable;
	int tp_report_rate;
	int follow_hand_level;
	int sensibility_enable;
	int finger_lock_flag;
	int palm_mode_en;
	int fold_state;
	int fake_sleep_enable;
#ifdef TOUCH_DOWN_UP_ZLOG
	int point_down_num;
	bool ghost_check_config;
	u8 ghost_check_single_time;
	u8 ghost_check_multi_time;
	u8 ghost_check_single_count;
	u8 ghost_check_multi_count;
	u8 ghost_check_start_time;
	int ghost_check_ignore_id;
#endif

	struct workqueue_struct *tpd_wq;
	struct workqueue_struct *tpd_report_wq;
	struct delayed_work tpd_report_work0;
	struct delayed_work tpd_report_work1;
	struct delayed_work tpd_report_work2;
	struct delayed_work tpd_report_work3;
	struct delayed_work tpd_report_work4;
	struct delayed_work tpd_report_work5;
	struct delayed_work tpd_report_work6;
	struct delayed_work tpd_report_work7;
	struct delayed_work tpd_report_work8;
	struct delayed_work tpd_report_work9;
	struct delayed_work tpd_probe_work;
#ifdef TOUCH_DOWN_UP_ZLOG
	struct delayed_work ghost_check_work;
#endif
	struct work_struct suspend_work;
	struct work_struct resume_work;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	struct delayed_work tpd_report_lcd_state_work;
#endif
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	struct delayed_work point_report_check_work;
#endif
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	struct zlog_client *zlog_client;
	struct delayed_work zlog_register_work;
	struct zlog_error_item zlog_item;
	char *ztp_zlog_buffer;
	bool zlog_regisered;
	u8 ztp_probe_fail_chip_id;
	unsigned long tp_reset_timer;
#endif

	struct bin_attribute attr_fwimage;
	struct kobject *zte_touch_kobj;
	struct firmware *tp_firmware;
	struct mutex cmd_mutex;
	struct mutex report_mutex;
	struct mutex tp_resume_mutex;
	struct tpd_tpinfo_t ic_tpinfo;
	void *private;
	struct device  *dev;
	struct platform_device *pdev;
	struct platform_device *zte_touch_pdev;
	void *tp_data;

	int (*tp_resume_func)(void *data);
	int (*tp_suspend_func)(void *data);
	int (*tp_fw_upgrade)(struct ztp_device *cdev, char *fwname, int fwname_len);
	int (*get_tpinfo)(struct ztp_device *cdev);
	int (*get_gesture)(struct ztp_device *cdev);
	int (*wake_gesture)(struct ztp_device *cdev, int enable);
	int (*get_smart_cover)(struct ztp_device *cdev);
	int (*set_smart_cover)(struct ztp_device *cdev, int enable);
	int (*get_glove_mode)(struct ztp_device *cdev);
	int (*set_glove_mode)(struct ztp_device *cdev, int enable);
	int (*tp_suspend_show)(struct ztp_device *cdev);
	int (*set_tp_suspend)(struct ztp_device *cdev, u8 suspend_node, int enable);
	bool (*tpd_suspend_need_awake)(struct ztp_device *cdev);
	int (*set_headset_state)(struct ztp_device *cdev, int enable);
	int (*headset_state_show)(struct ztp_device *cdev);
	int (*set_rotation_limit_level)(struct ztp_device *cdev, int enable);
	int (*get_rotation_limit_level)(struct ztp_device *cdev);
	int (*set_display_rotation)(struct ztp_device *cdev, int mrotation);
	bool (*tpd_esd_check)(struct ztp_device *cdev);
	void (*tpd_report_uevent)(u8 gesture_key);
	int (*tp_hw_reset)(void);
	void (*tp_reset_gpio_output)(bool value);
	int (*get_singletap)(struct ztp_device *cdev);
	int (*set_singletap)(struct ztp_device *cdev, int enable);
	int (*get_singleaod)(struct ztp_device *cdev);
	int (*set_singleaod)(struct ztp_device *cdev, int enable);
	int (*get_noise)(struct ztp_device *cdev);
	int (*get_one_key)(struct ztp_device *cdev);
	int (*set_one_key)(struct ztp_device *cdev, int enable);
	int (*get_play_game)(struct ztp_device *cdev);
	int (*set_play_game)(struct ztp_device *cdev, int enable);
	int (*set_tp_report_rate)(struct ztp_device *cdev, int enable);
	int (*get_tp_report_rate)(struct ztp_device *cdev);
	int (*set_follow_hand_level)(struct ztp_device *cdev, int enable);
	int (*get_follow_hand_level)(struct ztp_device *cdev);
	int (*set_sensibility_leve)(struct ztp_device *cdev, u8 level);
	int (*set_gpio_mode)(struct ztp_device *cdev, u8 mode);
	int (*get_sensibility)(struct ztp_device *cdev);
	int (*set_sensibility)(struct ztp_device *cdev, u8 enable);
	int (*set_finger_lock_flag)(struct ztp_device *cdev, int enable);
	int (*get_finger_lock_flag)(struct ztp_device *cdev);
	int (*get_pen_only_mode)(struct ztp_device *cdev);
	int (*set_pen_only_mode)(struct ztp_device *cdev, u8 enable);
	int (*tpd_shutdown)(struct ztp_device *cdev);
	int (*get_tp_self_test_result)(struct ztp_device *cdev, char *buf);
	int (*tp_self_test)(struct ztp_device *cdev);
	int (*tp_palm_mode_read)(struct ztp_device *cdev);
	int (*tp_palm_mode_write)(struct ztp_device *cdev, int enable);
	int (*tp_fold_state_read)(struct ztp_device *cdev);
	int (*tp_fold_state_write)(struct ztp_device *cdev, int enable);
	int (*get_fake_sleep)(struct ztp_device *cdev);
	int (*set_fake_sleep)(struct ztp_device *cdev, int enable);
};


#if 0
extern const char *zte_get_lcd_panel_name(void);
#endif
extern struct ztp_device *tpd_cdev;
extern void set_lcd_reset_processing(bool enable);
extern const char *get_lcd_panel_name(void);

int get_tp_chip_id(void);
void tpd_touch_press(struct input_dev *input, u16 x, u16 y, u16 id, u8 touch_major, u8  pressure);
void tpd_touch_release(struct input_dev *input, u16 id);
void tpd_clean_all_event(void);
int set_gpio_mode(u8 mode);
void tp_free_tp_firmware_data(void);
int tp_alloc_tp_firmware_data(int buf_size);
int  tpd_copy_to_tp_firmware_data(char *buf);
void  tpd_reset_fw_data_pos_and_size(void);
void change_tp_state(lcdchange lcd_change);
#endif
