// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"BATTERY_CHG: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rpmsg.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/thermal.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/sched.h>
#include <linux/ktime.h>

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
#include <vendor/common/zte_power_supply.h>
#endif

#if IS_ENABLED(CONFIG_VENDOR_ZTE_MISC)
#include <vendor/common/zte_misc.h>
#endif

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
#include <vendor/comdef/zlog_common_base.h>
#endif

#define MSG_OWNER_BC			32778
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

/* opcode for battery charger */
#define BC_RESISTANCE_ID_REQ    0x00
#define BC_SET_NOTIFY_REQ		0x04
#define BC_DISABLE_NOTIFY_REQ		0x05
#define BC_NOTIFY_IND			0x07
#define BC_BATTERY_STATUS_GET		0x30
#define BC_BATTERY_STATUS_SET		0x31
#define BC_USB_STATUS_GET		0x32
#define BC_USB_STATUS_SET		0x33
#define BC_WLS_STATUS_GET		0x34
#define BC_WLS_STATUS_SET		0x35
#define BC_SHIP_MODE_REQ_SET		0x36
#define BC_WLS_FW_CHECK_UPDATE		0x40
#define BC_WLS_FW_PUSH_BUF_REQ		0x41
#define BC_WLS_FW_UPDATE_STATUS_RESP	0x42
#define BC_WLS_FW_PUSH_BUF_RESP		0x43
#define BC_WLS_FW_GET_VERSION		0x44
#define BC_SHUTDOWN_NOTIFY		0x47
#define BC_CHG_CTRL_LIMIT_EN		0x48
#define BC_HBOOST_VMAX_CLAMP_NOTIFY	0x79
#define BC_GENERIC_NOTIFY		0x80

#define BC_ADSP_DEBUG			0x100
#define BC_DEBUG_MSG			0X101

/* Generic definitions */
#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			1000
#define WLS_FW_PREPARE_TIME_MS		1000
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_UPDATE_TIME_MS		1000
#define WLS_FW_BUF_SIZE			128
#define DEFAULT_RESTRICT_FCC_UA		1000000

#define MAX_OEM_MSG_SIZE		256

#define UPDATE_SOC_WORK_TIME_MS		10000

#if 0  //todo wait for merge by boot
extern int socinfo_get_charger_flag(void);
#endif
#ifdef CONFIG_ZTE_FEATURE_CHG_BATT_2S
extern int charger_165W(void);
extern int charger_65W(void);
u32 array_165w[20] = {
	13000000, 12000000, 10000000, 8000000, 5000000, 4000000, 3000000, 2000000, 1500000, 1000000,
	10000000, 8000000, 6000000, 5000000, 4000000, 3000000, 2000000, 1500000, 1000000, 800000,
};
u32 array_65w[20] = {
	6500000, 6000000, 5800000, 5300000, 5000000, 4000000, 3000000, 2000000, 1500000, 1000000,
	10000000, 8000000, 6000000, 5000000, 4000000, 3000000, 2000000, 1500000, 1000000, 800000,
};
#endif
enum psy_type {
	PSY_TYPE_BATTERY,
	PSY_TYPE_USB,
	PSY_TYPE_WLS,
	PSY_TYPE_MAX,
};

enum ship_mode_type {
	SHIP_MODE_PMIC,
	SHIP_MODE_PACK_SIDE,
};

/* property ids */
enum battery_property_id {
	BATT_STATUS,
	BATT_HEALTH,
	BATT_PRESENT,
	BATT_CHG_TYPE,
	BATT_CAPACITY,
	BATT_SOH,
	BATT_VOLT_OCV,
	BATT_VOLT_NOW,
	BATT_VOLT_MAX,
	BATT_CURR_NOW,
	BATT_CHG_CTRL_LIM,
	BATT_CHG_CTRL_LIM_MAX,
	BATT_TEMP,
	BATT_TECHNOLOGY,
	BATT_CHG_COUNTER,
	BATT_CYCLE_COUNT,
	BATT_CHG_FULL_DESIGN,
	BATT_CHG_FULL,
	BATT_MODEL_NAME,
	BATT_TTF_AVG,
	BATT_TTE_AVG,
	BATT_RESISTANCE,
	BATT_POWER_NOW,
	BATT_POWER_AVG,
	BATT_CHG_CTRL_EN,
	BATT_CHG_CTRL_START_THR,
	BATT_CHG_CTRL_END_THR,
	BATT_CURR_AVG,
#if 1
	BATT_FCC_USER,
	BATT_TYPEC_CC_ORIENTATION,
	BATT_RECHARGE_SOC,
	BATT_SMB139X_STATE,
	BATT_SSOC_FULL,
	BATT_RSOC_FULL,
	BATT_CHARGE_MODE,
	BATT_SCREEN_ON,
	BATT_PMIC_TEMP,
	BATT_SMB139X_TEMP1,
	BATT_SMB139X_TEMP2,
#endif
	BATT_PROP_MAX,
};

enum usb_property_id {
	USB_ONLINE,
	USB_VOLT_NOW,
	USB_VOLT_MAX,
	USB_CURR_NOW,
	USB_CURR_MAX,
	USB_INPUT_CURR_LIMIT,
	USB_TYPE,
	USB_ADAP_TYPE,
	USB_MOISTURE_DET_EN,
	USB_MOISTURE_DET_STS,
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
	USB_OEM_CHARGER_TYPE,
	USB_SUSPEND,
	USB_PRESENT,
	USB_PROP_MAX,
};

enum wireless_property_id {
	WLS_ONLINE,
	WLS_VOLT_NOW,
	WLS_VOLT_MAX,
	WLS_CURR_NOW,
	WLS_CURR_MAX,
	WLS_TYPE,
	WLS_BOOST_EN,
	WLS_HBOOST_VMAX,
	WLS_INPUT_CURR_LIMIT,
	WLS_ADAP_TYPE,
	WLS_CONN_TEMP,
	WLS_PROP_MAX,
};

enum {
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP = 0x80,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5,
};
enum {
	OEM_BATTERY_TYPE_NORMAL = 0,
	OEM_BATTERY_TYPE_LOW,
};
struct battery_charger_set_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			power_state;
	u32			low_capacity;
	u32			high_capacity;
};

struct battery_charger_get_resistance_id_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
};

struct battery_charger_adsp_debug_msg {
	struct pmic_glink_hdr	hdr;
	u32			adsp_debug;
};

struct battman_oem_debug_msg {
  struct pmic_glink_hdr	hdr;
  char msg_buffer[MAX_OEM_MSG_SIZE];
  u32 msg_size; //size = 0 if failed, otherwise should be data_size.
};

struct battery_charger_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			notification;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct battery_model_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	char			model[MAX_STR_LEN];
};

struct wireless_fw_check_req {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
	u32			fw_size;
	u32			fw_crc;
};

struct wireless_fw_check_resp {
	struct pmic_glink_hdr	hdr;
	u32			ret_code;
};

struct wireless_fw_push_buf_req {
	struct pmic_glink_hdr	hdr;
	u8			buf[WLS_FW_BUF_SIZE];
	u32			fw_chunk_id;
};

struct wireless_fw_push_buf_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_status;
};

struct wireless_fw_update_status {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_done;
};

struct wireless_fw_get_version_req {
	struct pmic_glink_hdr	hdr;
};

struct wireless_fw_get_version_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct battery_charger_chg_ctrl_msg {
	struct pmic_glink_hdr	hdr;
	u32			enable;
	u32			target_soc;
	u32			delta_soc;
};

struct psy_state {
	struct power_supply	*psy;
	char			*model;
	const int		*map;
	u32			*prop;
	u32			prop_count;
	u32			opcode_get;
	u32			opcode_set;
};

struct battery_chg_dev {
	struct device			*dev;
	struct class			battery_class;
	struct pmic_glink_client	*client;
	struct mutex			rw_lock;
	struct rw_semaphore		state_sem;
	struct completion		ack;
	struct completion		fw_buf_ack;
	struct completion		fw_update_ack;
	struct psy_state		psy_list[PSY_TYPE_MAX];
#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	struct zte_power_supply	*zte_battery_psy;
	struct zte_power_supply	*zte_usb_psy;
#endif
	struct dentry			*debugfs_dir;
	void				*notifier_cookie;
	u32				*thermal_levels;
	const char			*wls_fw_name;
	int				curr_thermal_level;
	int				num_thermal_levels;
	int				shutdown_volt_mv;
	atomic_t			state;
	struct work_struct		subsys_up_work;
	struct work_struct		usb_type_work;
/* add for fast_capacity start */
	bool				fast_capacity_enable;
	struct delayed_work		report_fast_capacity_work;
	struct work_struct		update_prop_work;
	int				fast_capacity;
	time64_t			report_begin;
	bool				discharging_smooth;
/* add for fast_capacity end */
	struct delayed_work	screen_on_select_fcc_work;
	int					last_soc;
	struct delayed_work	update_soc_work;
	struct work_struct		battery_check_work;
	int				fake_soc;
	bool				block_tx;
	bool				ship_mode_en;
	bool				charging_enabled;
	bool				battery_charging_enabled;
	bool				usb_suspend;
	bool				usb_present;
	u16				oem_battery_type;
	bool				screen_is_on;
	u32				resistance_id;
	u32				recharge_soc;
	int				charge_mode;
	u32				adsp_debug;
	bool				debug_battery_detected;
	bool				wls_fw_update_reqd;
	u32				wls_fw_version;
	u16				wls_fw_crc;
	u32				wls_fw_update_time_ms;
	struct notifier_block		reboot_notifier;
	u32				thermal_fcc_ua;
	u32				restrict_fcc_ua;
	u32				last_fcc_ua;
	u32				usb_icl_ua;
	u32				thermal_fcc_step;
	bool				restrict_chg_en;
	u8				chg_ctrl_start_thr;
	u8				chg_ctrl_end_thr;
	bool				chg_ctrl_en;
	/* To track the driver initialization status */
	bool				initialized;
	u32				healthd_batt_prop_masks;
	u32				healthd_usb_prop_masks;
	u32				ssoc_full;
	u32				rsoc_full;
	bool				notify_en;
	bool				error_prop;
};

static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
	[BATT_CHG_CTRL_START_THR] = POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
	[BATT_CHG_CTRL_END_THR]   = POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	[BATT_CURR_AVG]		= POWER_SUPPLY_PROP_CURRENT_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
	[USB_TEMP]		= POWER_SUPPLY_PROP_TEMP,
};

static const int wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
#if 0  // Removed by ZTE
	[WLS_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[WLS_CONN_TEMP]		= POWER_SUPPLY_PROP_TEMP,
#endif
};

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PD_PPS", "BrickID"
};

/* Custom usb_type definitions */
static const char * const qc_power_supply_usb_type_text[] = {
	"HVDCP", "HVDCP_3", "HVDCP_3P5"
};

/* wireless_type definitions */
static const char * const qc_power_supply_wls_type_text[] = {
	"Unknown", "BPP", "EPP", "HPP"
};

static RAW_NOTIFIER_HEAD(hboost_notifier);

int register_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&hboost_notifier, nb);
}
EXPORT_SYMBOL(register_hboost_event_notifier);

int unregister_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&hboost_notifier, nb);
}
EXPORT_SYMBOL(unregister_hboost_event_notifier);

static int battery_chg_fw_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	down_read(&bcdev->state_sem);
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		up_read(&bcdev->state_sem);
		return -ENOTCONN;
	}

	reinit_completion(&bcdev->fw_buf_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->fw_buf_ack,
					msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			up_read(&bcdev->state_sem);
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	up_read(&bcdev->state_sem);
	return rc;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	down_read(&bcdev->state_sem);
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		up_read(&bcdev->state_sem);
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx) {
		up_read(&bcdev->state_sem);
		return 0;
	}

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	bcdev->error_prop = false;
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			up_read(&bcdev->state_sem);
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}
		rc = 0;

		/*
		 * In case the opcode used is not supported, the remote
		 * processor might ack it immediately with a return code indicating
		 * an error. This additional check is to check if such an error has
		 * happened and return immediately with error in that case. This
		 * avoids wasting time waiting in the above timeout condition for this
		 * type of error.
		 */
		if (bcdev->error_prop) {
			bcdev->error_prop = false;
			rc = -ENODATA;
		}
	}
	mutex_unlock(&bcdev->rw_lock);
	up_read(&bcdev->state_sem);

	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	if (pst->psy)
		pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
			req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	if (pst->psy)
		pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
			req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int get_property_id(struct psy_state *pst,
			enum power_supply_property prop)
{
	u32 i;

	for (i = 0; i < pst->prop_count; i++)
		if (pst->map[i] == prop)
			return i;

	if (pst->psy)
		pr_err("No property id for property %d in psy %s\n", prop,
			pst->psy->desc->name);

	return -ENOENT;
}

#if 0
static void battery_chg_notify_disable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	if (bcdev->notify_en) {
		/* Send request to disable notification */
		req_msg.hdr.owner = MSG_OWNER_BC;
		req_msg.hdr.type = MSG_TYPE_NOTIFY;
		req_msg.hdr.opcode = BC_DISABLE_NOTIFY_REQ;

		rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
		if (rc < 0)
			pr_err("Failed to disable notification rc=%d\n", rc);
		else
			bcdev->notify_en = false;
	}
}
#endif

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	if (!bcdev->notify_en) {
		/* Send request to enable notification */
		req_msg.hdr.owner = MSG_OWNER_BC;
		req_msg.hdr.type = MSG_TYPE_NOTIFY;
		req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

		rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
		if (rc < 0)
			pr_err("Failed to enable notification rc=%d\n", rc);
		else
			bcdev->notify_en = true;
	}
}

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	down_write(&bcdev->state_sem);
	if (!bcdev->initialized) {
		pr_warn("Driver not initialized, pmic_glink state %d\n", state);
		up_write(&bcdev->state_sem);
		return;
	}
	atomic_set(&bcdev->state, state);
	up_write(&bcdev->state_sem);

	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
	else if (state == PMIC_GLINK_STATE_DOWN)
		bcdev->notify_en = false;
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct power_supply *psy;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst;
	int rc = 0;

	if (prop_id >= BATTERY_CHARGER_PROP_MAX)
		return -EINVAL;

	if (strcmp(name, "battery") && strcmp(name, "usb") &&
	    strcmp(name, "wireless"))
		return -EINVAL;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return -ENODEV;

	bcdev = power_supply_get_drvdata(psy);
	if (!bcdev)
		return -ENODEV;

	power_supply_put(psy);

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static bool validate_message(struct battery_chg_dev *bcdev,
			struct battery_charger_resp_msg *resp_msg, size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		pr_err_ratelimited("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		bcdev->error_prop = true;
		return false;
	}

	return true;
}


#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
static struct zlog_client *zlog_chg_client = NULL;
static struct zlog_mod_info zlog_chg_dev = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "PMU",
	.device_name = "qcom_pmu",
	.ic_name = "pmu8450",
	.module_name = "QCOM",
	.fops = NULL,
};

#define ZTE_BUF_SIZE 128

static void zte_log_chg_err(char *log_buf)
{
	if (!zlog_chg_client) {
		pr_err("%s zlog register client zlog_chg_dev fail2\n", __func__);
		return;
	}

	if(strnstr(log_buf,"BATTMNGR_DRV_BATT_OV_EVENT",ZTE_BUF_SIZE)) 
	{
		zlog_client_record(zlog_chg_client, "BATTMNGR_DRV_BATT_OV_EVENT");
		zlog_client_notify(zlog_chg_client, ZLOG_CHG_VBAT_OVP);
	}
	
	if(strnstr(log_buf,"BATTMNGR_DRV_USBIN_FAULT_EVENT",ZTE_BUF_SIZE))
	{
		zlog_client_record(zlog_chg_client, "Servicing BATTMNGR_DRV_USBIN_FAULT_EVENT");
		zlog_client_notify(zlog_chg_client, ZLOG_CHG_USBIN_FAULT);
	}
	
	if(strnstr(log_buf,"Hardreset",ZTE_BUF_SIZE))
	{
		zlog_client_record(zlog_chg_client, "Servicing Hardreset");
		zlog_client_notify(zlog_chg_client, ZLOG_CHG_HARD_RESET);
	}
	
	if(strnstr(log_buf,"Softreset",ZTE_BUF_SIZE))
	{
		zlog_client_record(zlog_chg_client, "Servicing Softreset");
		zlog_client_notify(zlog_chg_client, ZLOG_CHG_SOFT_RESET);
	}
	
}
#endif

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct battery_charger_get_resistance_id_msg *resistance_id_msg = data;
	struct battery_charger_adsp_debug_msg *adsp_debug_msg = data;
#if 0
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
#endif
	struct battman_oem_debug_msg *debug_msg = data;
	struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_RESISTANCE_ID_REQ:
		pr_info("Receive BC_RESISTANCE_ID_REQ");
		if (len == sizeof(*resistance_id_msg)) {
			bcdev->resistance_id = resistance_id_msg->battery_id;
			ack_set = true;
			pr_info("resistance_id: %d", bcdev->resistance_id);
		}
		break;

	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(bcdev, data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
	case BC_DISABLE_NOTIFY_REQ:
	case BC_SHUTDOWN_NOTIFY:
	case BC_SHIP_MODE_REQ_SET:
	case BC_CHG_CTRL_LIMIT_EN:
		/* Always ACK response for notify or ship_mode request */
		ack_set = true;
		break;
	case BC_ADSP_DEBUG:
		if (len == sizeof(*adsp_debug_msg)) {
			ack_set = true;
			pr_info("BC_ADSP_DEBUG");
		}
		break;
	case BC_DEBUG_MSG:
		if (len == sizeof(*debug_msg)) {
			pr_info("BC_DEBUG_MSG: %s\n", debug_msg->msg_buffer);
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM			
			zte_log_chg_err(debug_msg->msg_buffer);
#endif			
			
		} else {
			pr_info("BC_DEBUG_MSG wrong!\n");
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set || bcdev->error_prop)
		complete(&bcdev->ack);
}

static struct power_supply_desc usb_psy_desc;

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		pr_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		return;
	}
	rc = read_property_id(bcdev, pst, USB_OEM_CHARGER_TYPE);

	/* Reset usb_icl_ua whenever USB adapter type changes */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD)
		bcdev->usb_icl_ua = 0;

	pr_debug("usb_adap_type: %u\n", pst->prop[USB_ADAP_TYPE]);

	switch (pst->prop[USB_ADAP_TYPE]) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	}
	if (bcdev->oem_battery_type == OEM_BATTERY_TYPE_LOW) {
		/*for the low battery, vote fcc to 7380ma*/
		rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_FCC_USER, 7380);
	}
}

/* add for fast_capacity start */
static int getRandCapicity(void)
{
	u8 rand;

	get_random_bytes(&rand, 1);
	rand = rand & 0x0f;

	return rand;
}

static void battery_chg_update_healthd_prop(struct battery_chg_dev *bcdev)
{
	int index = 0;
	struct psy_state *pst;
	int rc = 0;

	if (bcdev == NULL) {
		return;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	//pr_info("healthd_batt_prop_masks: %d", bcdev->healthd_batt_prop_masks);
	for (index = 0; index < 32; index++) {
		if (((bcdev->healthd_batt_prop_masks >> index) & 0x01) != 0) {
			rc = read_property_id(bcdev, pst, index);
			if (rc != 0) {
				return;
			}
			pr_debug("read battery prop: %d, prop_id: %d", pst->prop[index], index);
		}
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	//pr_info("healthd_usb_prop_masks: %d", bcdev->healthd_usb_prop_masks);
	for (index = 0; index < 32; index++) {
		if (((bcdev->healthd_usb_prop_masks >> index) & 0x01) != 0) {
			rc = read_property_id(bcdev, pst, index);
			if (rc != 0) {
				return;
			}
			pr_debug("read usb prop: %d, prop_id: %d", pst->prop[index], index);
			if (index == USB_ONLINE) {
				pr_debug("USB_ONLINE: %d\n", pst->prop[index]);
			}
		}
	}
}

#define FULL_SOC							10000
static void battery_chg_update_prop_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev,
						update_prop_work);
	struct psy_state *usb_pst = &bcdev->psy_list[PSY_TYPE_USB];
	struct psy_state *battery_pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	static int last_oem_charger_type = -1;
	static int last_battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
	int oem_charger_type = -1;
	int battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
	int online = 0;
	int ui_capacity = -1;
	struct timespec64 ts;

	read_property_id(bcdev, usb_pst, USB_OEM_CHARGER_TYPE);
	read_property_id(bcdev, battery_pst, BATT_CAPACITY);
	read_property_id(bcdev, battery_pst, BATT_STATUS);
	read_property_id(bcdev, battery_pst, BATT_CURR_NOW);

	battery_chg_update_healthd_prop(bcdev);

	oem_charger_type = usb_pst->prop[USB_OEM_CHARGER_TYPE];
	online = usb_pst->prop[USB_ONLINE];
	battery_status = battery_pst->prop[BATT_STATUS];

	pr_info("fast_capacity online=%d,oem_charger_type=%d,last_oem_charger_type=%d",
			online, oem_charger_type, last_oem_charger_type);
	if (online && oem_charger_type == 7 && last_oem_charger_type != 7) {
#if 0  //todo wait for merge by boot
		if (socinfo_get_charger_flag()) {
			pr_info("in poweroff charge, no need to report fast_capacity\n");
			goto update_last_status;
		}
#endif
		if (bcdev->fake_soc > 0) {
			ui_capacity = bcdev->fake_soc;
		} else {
			ui_capacity = DIV_ROUND_CLOSEST(battery_pst->prop[BATT_CAPACITY], 100);
		}
		if (ui_capacity >= 0 &&	ui_capacity != bcdev->fast_capacity / 100) {
			bcdev->fast_capacity = ui_capacity * 100 + getRandCapicity();
			if (bcdev->fast_capacity > FULL_SOC) {
				pr_info("bcdev->fast_capacity is %d, ui_capacity is %d\n",
						bcdev->fast_capacity, ui_capacity);
				bcdev->fast_capacity = FULL_SOC;
			}
			pr_info("1 set bcdev->fast_capacity to %d\n", bcdev->fast_capacity);
		}
		pr_info("start work to reprot fast_capacity 1\n");
		ts = ktime_to_timespec64(ktime_get_boottime());
		bcdev->report_begin  = ts.tv_sec;
		pm_wakeup_dev_event(bcdev->dev, 1100, true);
		cancel_delayed_work(&bcdev->report_fast_capacity_work);
		schedule_delayed_work(&bcdev->report_fast_capacity_work, msecs_to_jiffies(1000));
		bcdev->discharging_smooth = false;
		goto update_last_status;
	}




update_last_status:
	last_oem_charger_type = oem_charger_type;
	last_battery_status = battery_status;
}


#define EVENT_STRING_LENGTH 64
static void send_capacity_event(struct battery_chg_dev *bcdev, int fast_capacity)
{
	char event_string[EVENT_STRING_LENGTH];
	char *envp[2] = { event_string, NULL };

	pr_info("fast_capacity=%d\n", fast_capacity);

	snprintf(event_string, EVENT_STRING_LENGTH, "capacity=%d", fast_capacity);
	kobject_uevent_env(&bcdev->dev->kobj, KOBJ_CHANGE, envp);

}
#ifdef ZTE_CHARGER_2S_BATTERY
/* 2S single 2100mAh battery */
/* x(A) * 1000/3600 * y(Sec) = 2100/10000 */
/* About 12A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_FAST   60
/* About 9.5A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_LOW    80
/* About 5A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_MED    150
/* About 1.5A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_HIGH   500
#else
/* 4200mAh battery */
/* x(A) * 1000/3600 * y(Sec) = 4200/10000 */
/* About 10A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_FAST	120
/* About 6A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_LOW	200
/* About 2.5A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_MED	500
/* About 1A FCC */
#define FAST_CAPACITY_INCREASE_DELAY_HIGH	1500
#endif
/* time dealy to change fake soc to catch up real soc */
#define FAKE_CAPACITY_CATCH_DELAY			60000

#define WAIT_TIMES_NORMAL_CHARGE			8
#define WAIT_TIMES_POWEROFF_CHARGE			50
#define MIN_WAIT_SEC_TO_EXIT				2
#define MAX_WAIT_SEC_TO_EXIT				15

static void battery_chg_report_fast_capacity_work(struct work_struct *work)
{
	static bool is_work_running = false;
	static int batt_stat_error_times = 0;
//	static int current_error_times = 0;
	int ui_capacity = -1, schedule_delay = -1, batt_status = -1, current_now = -1;
	int oem_charger_type = -1, full_design = 5000;
	struct timespec64 ts;
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev,
						report_fast_capacity_work.work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	struct psy_state *usb_pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (is_work_running) {
		/* work already running, ignore */
		pr_err("work already running, ignore\n");
		return;
	}
	is_work_running = true;
	ui_capacity = DIV_ROUND_CLOSEST(pst->prop[BATT_CAPACITY], 100);
	pr_info("battery_chg_report_fast_capacity_work running, ui_capacity=%d,fake_soc=%d", ui_capacity, bcdev->fake_soc);

	batt_status = pst->prop[BATT_STATUS];
	if (batt_status != POWER_SUPPLY_STATUS_CHARGING || bcdev->battery_charging_enabled == 0) {
		batt_stat_error_times++;
		pr_info("batt_status is not charging: batt_status=%d, battery_charging_enabled=%d, fake_soc=%d, check again\n", batt_status, bcdev->battery_charging_enabled, bcdev->fake_soc);
		read_property_id(bcdev, pst, BATT_STATUS);
		batt_status = pst->prop[BATT_STATUS];
		if (batt_status != POWER_SUPPLY_STATUS_CHARGING || bcdev->battery_charging_enabled == 0) {
			/* if fake_soc not set and error times exceeds, exit work */
			pr_info("batt_status is not charging: %d, error_times: %d\n",
					batt_status, batt_stat_error_times);
			if (bcdev->fake_soc < 0 && batt_stat_error_times > 1) {
				is_work_running = false;
				return;
			}
		}
	} else {
		batt_stat_error_times = 0;
	}

	full_design = pst->prop[BATT_CHG_FULL_DESIGN] / 1000;


	current_now = pst->prop[BATT_CURR_NOW];	
	if (batt_status == POWER_SUPPLY_STATUS_CHARGING) {
		
#if 0		
		if (current_now < 0) {
			read_property_id(bcdev, pst, BATT_CURR_NOW);
			current_now = pst->prop[BATT_CURR_NOW];
		}
		if (current_now/1000 < -10) {
			current_error_times++;
			/* current_now is negetive, skip fast_capacity increase */
			schedule_delay = FAST_CAPACITY_INCREASE_DELAY_MED;
			pr_info("current_now is %d, skip increase\n", current_now);
			goto exit;
		} else {
			current_error_times = 0;
		}
		if (current_error_times > 5) {
			/* current_now negative for too long, exit */
			pr_info("current_error_times is %d, exit\n", current_error_times);
			bcdev->fake_soc = -EINVAL;
			power_supply_changed(pst->psy);
			is_work_running = false;
			return;
		}
#endif

		/* In progress deal with fake soc and normal charge plugin may also run here
			we should continue deal with fake_soc and not send fast_capacity */
		oem_charger_type = usb_pst->prop[USB_OEM_CHARGER_TYPE];
		if (oem_charger_type != 7) {
			if (bcdev->fast_capacity > 0) {
				pr_info("non-fast charge, clear bcdev->fast_capacity\n");
				bcdev->fast_capacity = -EINVAL;
			}
			if (bcdev->fake_soc < 0) {
				is_work_running = false;
				pr_info("non-fast charge fake soc not set, exit\n");
				return;
			} else if (bcdev->fake_soc < ui_capacity) {
				bcdev->fake_soc = bcdev->fake_soc + 1;
				if (ui_capacity == bcdev->fake_soc) {
					bcdev->fake_soc = -EINVAL;
					power_supply_changed(pst->psy);
					is_work_running = false;
					pr_info("non-fast charge fake_soc catch up ui_capacity, exit\n");
					return;
				}
				if (ui_capacity - bcdev->fake_soc > 2)
					schedule_delay = FAKE_CAPACITY_CATCH_DELAY / 2;
				else
					schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
			} else {
				schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
			}
			pr_info("non-fast charge fake_soc:%d, ui_capacity:%d\n", bcdev->fake_soc, ui_capacity);
		} else {
			if (bcdev->fast_capacity < FULL_SOC) {
				bcdev->fast_capacity = bcdev->fast_capacity + 1;
			}
			if (bcdev->fast_capacity / 100 != ui_capacity) {
				if (bcdev->fast_capacity / 100 > ui_capacity) {
					bcdev->fast_capacity = bcdev->fast_capacity - 1;
					schedule_delay = FAST_CAPACITY_INCREASE_DELAY_HIGH;
					pr_info("run too fast, keep in xx.99, bcdev->fast_capacity=%d,schedule_delay=%d\n", bcdev->fast_capacity, schedule_delay);
				} else {
					bcdev->fast_capacity = ui_capacity * 100;
					schedule_delay = FAST_CAPACITY_INCREASE_DELAY_LOW;
					pr_info("run too slow, jump to real soc, keep bcdev->fast_capacity=%d,schedule_delay=%d\n", bcdev->fast_capacity, schedule_delay);
				}
				power_supply_changed(pst->psy);
			} else {
				/* set fake_soc to -EINVAL */
				bcdev->fake_soc = -EINVAL;
				/* This may lead to soc jump, fast_capacity just catchup ui_capacity
				but ui_capacity update again */
				/* power_supply_changed(pst->psy);*/
				if (bcdev->fast_capacity == FULL_SOC) {
					is_work_running = false;
					send_capacity_event(bcdev, bcdev->fast_capacity);
					power_supply_changed(pst->psy);
					pr_info("fast capacity reach full and fake_soc not set, exit\n");
					return;
				}
				read_property_id(bcdev, pst, BATT_CURR_NOW);
				current_now = pst->prop[BATT_CURR_NOW] / 1000;
				/* fast_capacity equal real capacity, use current_now calculate */
				schedule_delay = max(FAST_CAPACITY_INCREASE_DELAY_FAST,
					min(full_design * 360 / current_now, FAST_CAPACITY_INCREASE_DELAY_MED));
			}
			pr_info("charging increase fast_capacity, delay: %d\n", schedule_delay);
			if (bcdev->fast_capacity % 100 == 0 && pst->psy) {
				pr_info("capacity increase 1, send to HLOS\n");
				power_supply_changed(pst->psy);
			}
			send_capacity_event(bcdev, bcdev->fast_capacity);
		}
	} else if (batt_status == POWER_SUPPLY_STATUS_FULL) {
				/* In progress deal with fake soc and normal charge plugin may also run here
			we should continue deal with fake_soc and not send fast_capacity */
		oem_charger_type = usb_pst->prop[USB_OEM_CHARGER_TYPE];
		if (oem_charger_type != 7) {
			if (bcdev->fast_capacity > 0) {
				pr_info("non-fast charge, clear bcdev->fast_capacity\n");
				bcdev->fast_capacity = -EINVAL;
			}
			if (bcdev->fake_soc < 0) {
				is_work_running = false;
				pr_info("non-fast charge fake soc not set, exit\n");
				return;
			} else if (bcdev->fake_soc < ui_capacity) {
				bcdev->fake_soc = bcdev->fake_soc + 1;
				if (ui_capacity == bcdev->fake_soc) {
					bcdev->fake_soc = -EINVAL;
					power_supply_changed(pst->psy);
					is_work_running = false;
					pr_info("non-fast charge fake_soc catch up ui_capacity, exit\n");
					return;
				}
				if (ui_capacity - bcdev->fake_soc > 2)
					schedule_delay = FAKE_CAPACITY_CATCH_DELAY / 2;
				else
					schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
			} else {
				schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
			}
			pr_info("non-fast charge fake_soc:%d, ui_capacity:%d\n", bcdev->fake_soc, ui_capacity);
		} else {
			pr_info("battery full, deal with fast_capacity:%d, ui:%d\n", bcdev->fast_capacity, ui_capacity);
			if (bcdev->fast_capacity / 100 < ui_capacity) {
				bcdev->fast_capacity = bcdev->fast_capacity + 1;
			}
			if (bcdev->fast_capacity / 100 != ui_capacity) {
				if (bcdev->fast_capacity / 100 != bcdev->fake_soc) {
					bcdev->fake_soc = bcdev->fast_capacity / 100;
					/* set fake_capacity to fast_capacity */
					pr_info("fake_soc update to %d, send to HLOS\n", bcdev->fake_soc);
					power_supply_changed(pst->psy);
				}
				if (bcdev->fast_capacity / 100 > ui_capacity) {
					/* fast_capacity above real capacity, just wait */
					schedule_delay = FAST_CAPACITY_INCREASE_DELAY_HIGH;
				} else if (bcdev->fast_capacity / 100 < ui_capacity) {
					/* fast_capacity below real capacity, speed up increase speed */
					schedule_delay = FAST_CAPACITY_INCREASE_DELAY_LOW;
				}
			} else {
				/* set fake_capacity to -EINVAL */
				bcdev->fake_soc = -EINVAL;
				if (bcdev->fast_capacity == FULL_SOC) {
					is_work_running = false;
					send_capacity_event(bcdev, bcdev->fast_capacity);
					pr_info("fast capacity reach 100 and fake capacity not set, exit\n");
					return;
				}
				pr_info("fast capacity equal with ui_capacity but not 100, wait\n");
				schedule_delay = FAST_CAPACITY_INCREASE_DELAY_HIGH;
			}
		}
	} else if (batt_status == POWER_SUPPLY_STATUS_DISCHARGING) {
		pr_info("discharging, deal with fake_soc: %d, ui:%d\n", bcdev->fake_soc, ui_capacity);
		/* fake_soc still above ui_capacity,
		 we decrease fake_soc by step */
		if (bcdev->fake_soc > ui_capacity) {
			if (bcdev->discharging_smooth) {
				pr_info("fake_soc decrease 1, send to HLOS\n");
				bcdev->fake_soc = bcdev->fake_soc - 1;
				power_supply_changed(pst->psy);
				if (bcdev->fake_soc > ui_capacity) {
					schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
				} else {
					bcdev->fake_soc = -EINVAL;
					power_supply_changed(pst->psy);
					is_work_running = false;
					pr_info("fake capacity catch up ui_capacity, exit\n");
					return;
				}
			} else {
				schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
				bcdev->discharging_smooth = true;
			}
		} else if (bcdev->fake_soc < ui_capacity) {
			/* wait for more time to make fake_soc equal with ui_capacity*/
			schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
		} else {
			bcdev->fake_soc = -EINVAL;
			power_supply_changed(pst->psy);
			is_work_running = false;
			pr_info("fake capacity equal with ui_capacity, exit\n");
			return;
		}
	} else if (batt_status == POWER_SUPPLY_STATUS_NOT_CHARGING) {
		if (bcdev->fake_soc < 0) {
			is_work_running = false;
			return;
		} else {
			schedule_delay = FAKE_CAPACITY_CATCH_DELAY;
			if (bcdev->fake_soc == ui_capacity) {
				bcdev->fake_soc = -EINVAL;
				power_supply_changed(pst->psy);
				is_work_running = false;
				pr_info("fake capacity equal with ui_capacity, exit\n");
				return;
			}
		}
	} else {
		pr_info("unknown status, exit\n");
		bcdev->fake_soc = -EINVAL;
		power_supply_changed(pst->psy);
		is_work_running = false;
		return;
	}

//exit:
	if (bcdev->fake_soc < 0) {
		ts = ktime_to_timespec64(ktime_get_boottime());
		if (ts.tv_sec - bcdev->report_begin > MAX_WAIT_SEC_TO_EXIT) {
			pr_info("max wait time reach, exit\n");
			is_work_running = false;
			power_supply_changed(pst->psy);
			return;
		}
	}

	/* acquire wake lock for schedule_delay + 100ms to prevent from sleep */
	pm_wakeup_event(bcdev->dev, schedule_delay + 100);
	schedule_delayed_work(&bcdev->report_fast_capacity_work, msecs_to_jiffies(schedule_delay));
	is_work_running = false;
}
/* add for fast_capacity end */

static void battery_chg_check_status_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev,
					battery_check_work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_STATUS);
	if (rc < 0) {
		pr_err("Failed to read BATT_STATUS, rc=%d\n", rc);
		return;
	}

	if (pst->prop[BATT_STATUS] == POWER_SUPPLY_STATUS_CHARGING) {
		pr_debug("Battery is charging\n");
		return;
	}

	rc = read_property_id(bcdev, pst, BATT_CAPACITY);
	if (rc < 0) {
		pr_err("Failed to read BATT_CAPACITY, rc=%d\n", rc);
		return;
	}

	if (DIV_ROUND_CLOSEST(pst->prop[BATT_CAPACITY], 100) > 0) {
		pr_debug("Battery SOC is > 0\n");
		return;
	}

	/*
	 * If we are here, then battery is not charging and SOC is 0.
	 * Check the battery voltage and if it's lower than shutdown voltage,
	 * then initiate an emergency shutdown.
	 */

	rc = read_property_id(bcdev, pst, BATT_VOLT_NOW);
	if (rc < 0) {
		pr_err("Failed to read BATT_VOLT_NOW, rc=%d\n", rc);
		return;
	}

	if (pst->prop[BATT_VOLT_NOW] / 1000 > bcdev->shutdown_volt_mv) {
		pr_debug("Battery voltage is > %d mV\n",
			bcdev->shutdown_volt_mv);
		return;
	}

	pr_emerg("Initiating a shutdown in 100 ms\n");
	msleep(100);
    /* ignore by zte for shutdown both in ztebatteryservice and charger driver may cause crash? */
	pr_emerg("Attempting kernel_power_off: Battery voltage low, ignore by zte\n");
	/* kernel_power_off(); */
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct psy_state *pst = NULL;
	u32 hboost_vmax_mv, notification;

	if (len != sizeof(*notify_msg)) {
		pr_err("Incorrect response length %zu\n", len);
		return;
	}

	notification = notify_msg->notification;
	pr_debug("notification: %#x\n", notification);
	if ((notification & 0xffff) == BC_HBOOST_VMAX_CLAMP_NOTIFY) {
		hboost_vmax_mv = (notification >> 16) & 0xffff;
		raw_notifier_call_chain(&hboost_notifier, VMAX_CLAMP, &hboost_vmax_mv);
		pr_debug("hBoost is clamped at %u mV\n", hboost_vmax_mv);
		return;
	}

	switch (notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		if (bcdev->shutdown_volt_mv > 0)
			schedule_work(&bcdev->battery_check_work);
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
	default:
		break;
	}

	if (pst && pst->psy) {
		/*
		 * For charger mode, keep the device awake at least for 50 ms
		 * so that device won't enter suspend when a non-SDP charger
		 * is removed. This would allow the userspace process like
		 * "charger" to be able to read power supply uevents to take
		 * appropriate actions (e.g. shutting down when the charger is
		 * unplugged).
		 */
		power_supply_changed(pst->psy);

		pr_info("power_supply_changed,online=%d,type=%d,oemtype=%d,icl=%d,soc=%d,vbat=%d,ibat=%d,temp=%d,WLS_ONLINE=%d,vusb=%d",
			bcdev->psy_list[PSY_TYPE_USB].prop[USB_ONLINE],
			bcdev->psy_list[PSY_TYPE_USB].prop[USB_ADAP_TYPE],
			bcdev->psy_list[PSY_TYPE_USB].prop[USB_OEM_CHARGER_TYPE],
			bcdev->psy_list[PSY_TYPE_USB].prop[USB_INPUT_CURR_LIMIT],
			bcdev->psy_list[PSY_TYPE_BATTERY].prop[BATT_CAPACITY],
			bcdev->psy_list[PSY_TYPE_BATTERY].prop[BATT_VOLT_NOW],
			bcdev->psy_list[PSY_TYPE_BATTERY].prop[BATT_CURR_NOW],
			bcdev->psy_list[PSY_TYPE_BATTERY].prop[BATT_TEMP],
			bcdev->psy_list[PSY_TYPE_WLS].prop[WLS_ONLINE],
			bcdev->psy_list[PSY_TYPE_USB].prop[USB_VOLT_NOW]
			);
		pm_wakeup_dev_event(bcdev->dev, 50, true);

/* add for fast_capacity start */
		if (bcdev->fast_capacity_enable)
			schedule_work(&bcdev->update_prop_work);
/* add for fast_capacity end */
	}
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	down_read(&bcdev->state_sem);

	if (!bcdev->initialized) {
		pr_debug("Driver initialization failed: Dropping glink callback message: state %d\n",
			 bcdev->state);
		up_read(&bcdev->state_sem);
		return 0;
	}

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
	else
		handle_message(bcdev, data, len);

	up_read(&bcdev->state_sem);

	return 0;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

static const char *get_wls_type_name(u32 wls_type)
{
	if (wls_type >= ARRAY_SIZE(qc_power_supply_wls_type_text))
		return "Unknown";

	return qc_power_supply_wls_type_text[wls_type];
}

static const char *get_usb_type_name(u32 usb_type)
{
	u32 i;

	if (usb_type >= QTI_POWER_SUPPLY_USB_TYPE_HVDCP &&
	    usb_type <= QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5) {
		for (i = 0; i < ARRAY_SIZE(qc_power_supply_usb_type_text);
		     i++) {
			if (i == (usb_type - QTI_POWER_SUPPLY_USB_TYPE_HVDCP))
				return qc_power_supply_usb_type_text[i];
		}
		return "Unknown";
	}

	for (i = 0; i < ARRAY_SIZE(power_supply_usb_type_text); i++) {
		if (i == usb_type)
			return power_supply_usb_type_text[i];
	}

	return "Unknown";
}

static int usb_psy_set_icl(struct battery_chg_dev *bcdev, u32 prop_id, int val)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	u32 temp;
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		pr_err("Failed to read prop USB_ADAP_TYPE, rc=%d\n", rc);
		return rc;
	}

	/*
	 * Input current limit (ICL) can be set by different clients. E.g. USB
	 * driver can request for a current of 500/900 mA depending on the
	 * port type. Also, clients like EUD driver can pass 0 or -22 to
	 * suspend or unsuspend the input for its use case.
	 */

	temp = val;
	if (val < 0)
		temp = UINT_MAX;

	if (temp <= 2000) {
		pr_err("not set ICL to (%u uA) to avoid suspend\n", temp);
		return rc;
	}

	rc = write_property_id(bcdev, pst, prop_id, temp);
	if (rc < 0) {
		pr_err("Failed to set ICL (%u uA) rc=%d\n", temp, rc);
	} else {
		pr_debug("Set ICL to %u\n", temp);
		bcdev->usb_icl_ua = temp;
	}

	return rc;
}

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;
	char current_pid_name[TASK_COMM_LEN] = {0};

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	get_task_comm(current_pid_name, current);
	pr_debug("current pid name: %s\n", current_pid_name);
	if (strstr(current_pid_name, "health") != NULL || strstr(current_pid_name, "android.hard") != NULL) {
		pr_debug("healthd_usb_prop_masks: %x for prop_id: %d\n", (bcdev->healthd_usb_prop_masks & (0x1 << prop_id)), prop_id);
		if ((bcdev->healthd_usb_prop_masks & (0x1 << prop_id)) == 0) {
			rc = read_property_id(bcdev, pst, prop_id);
			bcdev->healthd_usb_prop_masks |= (0x1 << prop_id);
		} else {
			pr_debug("pst->prop[%d]: %d\n", prop_id, pst->prop[prop_id]);
		}

		if (prop_id == USB_ONLINE) {
			pr_debug("USB_ONLINE: %d\n", pst->prop[prop_id]);
		}
	} else {
		rc = read_property_id(bcdev, pst, prop_id);
	}

	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];
	if (prop == POWER_SUPPLY_PROP_TEMP)
		pval->intval = DIV_ROUND_CLOSEST((int)pval->intval, 10);

	return 0;
}

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = usb_psy_set_icl(bcdev, prop_id, pval->intval);
		break;
	default:
		break;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

#define CHARGE_CTRL_START_THR_MIN	50
#define CHARGE_CTRL_START_THR_MAX	95
#define CHARGE_CTRL_END_THR_MIN		55
#define CHARGE_CTRL_END_THR_MAX		100
#define CHARGE_CTRL_DELTA_SOC		5

static int battery_psy_set_charge_threshold(struct battery_chg_dev *bcdev,
					u32 target_soc, u32 delta_soc)
{
	struct battery_charger_chg_ctrl_msg msg = { { 0 } };
	int rc;

	if (!bcdev->chg_ctrl_en)
		return 0;

	if (target_soc > CHARGE_CTRL_END_THR_MAX)
		target_soc = CHARGE_CTRL_END_THR_MAX;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_CHG_CTRL_LIMIT_EN;
	msg.enable = 1;
	msg.target_soc = target_soc;
	msg.delta_soc = delta_soc;

	rc = battery_chg_write(bcdev, &msg, sizeof(msg));
	if (rc < 0)
		pr_err("Failed to set charge_control thresholds, rc=%d\n", rc);
	else
		pr_debug("target_soc: %u delta_soc: %u\n", target_soc, delta_soc);

	return rc;
}

static int battery_psy_set_charge_end_threshold(struct battery_chg_dev *bcdev,
					int val)
{
	u32 delta_soc = CHARGE_CTRL_DELTA_SOC;
	int rc;

	if (val < CHARGE_CTRL_END_THR_MIN ||
	    val > CHARGE_CTRL_END_THR_MAX) {
		pr_err("Charge control end_threshold should be within [%u %u]\n",
			CHARGE_CTRL_END_THR_MIN, CHARGE_CTRL_END_THR_MAX);
		return -EINVAL;
	}

	if (bcdev->chg_ctrl_start_thr && val > bcdev->chg_ctrl_start_thr)
		delta_soc = val - bcdev->chg_ctrl_start_thr;

	rc = battery_psy_set_charge_threshold(bcdev, val, delta_soc);
	if (rc < 0)
		pr_err("Failed to set charge control end threshold %u, rc=%d\n",
			val, rc);
	else
		bcdev->chg_ctrl_end_thr = val;

	return rc;
}

static int battery_psy_set_charge_start_threshold(struct battery_chg_dev *bcdev,
					int val)
{
	u32 target_soc, delta_soc;
	int rc;

	if (val < CHARGE_CTRL_START_THR_MIN ||
	    val > CHARGE_CTRL_START_THR_MAX) {
		pr_err("Charge control start_threshold should be within [%u %u]\n",
			CHARGE_CTRL_START_THR_MIN, CHARGE_CTRL_START_THR_MAX);
		return -EINVAL;
	}

	if (val > bcdev->chg_ctrl_end_thr) {
		target_soc = val +  CHARGE_CTRL_DELTA_SOC;
		delta_soc = CHARGE_CTRL_DELTA_SOC;
	} else {
		target_soc = bcdev->chg_ctrl_end_thr;
		delta_soc = bcdev->chg_ctrl_end_thr - val;
	}

	rc = battery_psy_set_charge_threshold(bcdev, target_soc, delta_soc);
	if (rc < 0)
		pr_err("Failed to set charge control start threshold %u, rc=%d\n",
			val, rc);
	else
		bcdev->chg_ctrl_start_thr = val;

	return rc;
}

static int __battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					u32 fcc_ua)
{
	int rc;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (rc < 0) {
		pr_err("Failed to set FCC %u, rc=%d\n", fcc_ua, rc);
	} else {
		pr_debug("Set FCC to %u uA\n", fcc_ua);
		bcdev->last_fcc_ua = fcc_ua;
	}

	return rc;
}
enum batt_fcc_offset {
	USB_SCR_OFF = 0,
	USB_SCR_ON,
	OFFSET_NUMBERS,
};
#define LEVELS_OF_EACH_SCENE	10
static int battery_get_fcc_by_thermal_level(struct battery_chg_dev *bcdev, int level)
{
	u32 fcc_ua = 0;
	int calc_level = 0;
	bool support_screenon = false;
	int baselevel = 0;
	if (bcdev->num_thermal_levels >= LEVELS_OF_EACH_SCENE * 2) {
		support_screenon = true;
	}

	if (support_screenon && bcdev->screen_is_on) {
		baselevel = baselevel + LEVELS_OF_EACH_SCENE;
	}
	if (level > 0) {
		calc_level = baselevel + level;
	} else {
		calc_level = 0;//level=0 means no thermal limit
	}
	fcc_ua = bcdev->thermal_levels[calc_level];
	pr_info("battery_get_fcc_by_thermal_level levelcount=%d,screenon=%d,level=%d,calc_level=%d,fcc=%d\n",
		bcdev->num_thermal_levels, bcdev->screen_is_on, level, calc_level, fcc_ua);

	return fcc_ua;
}
static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		pr_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	bcdev->curr_thermal_level = val;
	fcc_ua = battery_get_fcc_by_thermal_level(bcdev, val);
	prev_fcc_ua = bcdev->thermal_fcc_ua;
	bcdev->thermal_fcc_ua = fcc_ua;
	pr_info("battery_psy_set_charge_current thermal level=%d,fcc=%d\n", val, fcc_ua);
	rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
	if (!rc)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int prop_id, rc = 0;
	char current_pid_name[TASK_COMM_LEN] = {0};

	pval->intval = -ENODATA;

	/*
	 * The prop id of TIME_TO_FULL_NOW and TIME_TO_FULL_AVG is same.
	 * So, map the prop id of TIME_TO_FULL_AVG for TIME_TO_FULL_NOW.
	 */
	if (prop == POWER_SUPPLY_PROP_TIME_TO_FULL_NOW)
		prop = POWER_SUPPLY_PROP_TIME_TO_FULL_AVG;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	get_task_comm(current_pid_name, current);
	pr_debug("current pid name: %s\n", current_pid_name);
	if (strstr(current_pid_name, "health") != NULL || strstr(current_pid_name, "android.hard") != NULL) {
		pr_debug("healthd_batt_prop_masks: %x for prop_id: %d\n", (bcdev->healthd_batt_prop_masks & (0x1 << prop_id)), prop_id);
		if ((bcdev->healthd_batt_prop_masks & (0x1 << prop_id)) == 0) {
			rc = read_property_id(bcdev, pst, prop_id);
			bcdev->healthd_batt_prop_masks |= (0x1 << prop_id);
		} else {
			pr_debug("pst->prop[%d]: %d\n", prop_id, pst->prop[prop_id]);
		}
	} else {
		rc = read_property_id(bcdev, pst, prop_id);
	}

	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);
/* add for fast_capacity start */
		if (bcdev->fast_capacity_enable) {
			if ((bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
				pval->intval = bcdev->fake_soc;
		} else {
		if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) &&
		   (bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
			pval->intval = bcdev->fake_soc;
		}
/* add for fast_capacity end */

		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	default:
		pval->intval = pst->prop[prop_id];
		break;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		return battery_psy_set_charge_start_threshold(bcdev,
								pval->intval);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		return battery_psy_set_charge_end_threshold(bcdev,
								pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

#if IS_ENABLED(CONFIG_VENDOR_ZTE_MISC)
int battery_chg_set_screen_on(const char *val, const void *arg)
{
	struct battery_chg_dev *bcdev = (struct battery_chg_dev *) arg;

	if (bcdev == NULL)
		return -EINVAL;
	if (kstrtobool(val, &bcdev->screen_is_on))
		return -EINVAL;

	pr_info("battery_chg_set_screen_on,bcdev->screen_is_on=%d\n", bcdev->screen_is_on);
	{
		cancel_delayed_work_sync(&bcdev->screen_on_select_fcc_work);
		if (bcdev->screen_is_on) {
			schedule_delayed_work(&bcdev->screen_on_select_fcc_work, msecs_to_jiffies(1000 * 70));
		} else {
			schedule_delayed_work(&bcdev->screen_on_select_fcc_work, msecs_to_jiffies(1000));
		}
	}
	if (bcdev->screen_is_on) {
		cancel_delayed_work(&bcdev->update_soc_work);
		schedule_delayed_work(&bcdev->update_soc_work, msecs_to_jiffies(50));
		pr_info("start battery_chg_update_soc_work for screen on\n");
	}
	write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY], BATT_SCREEN_ON, bcdev->screen_is_on);
	return 0;
}

int battery_chg_get_screen_on(char *val, const void *arg)
{
	struct battery_chg_dev *bcdev = (struct battery_chg_dev *) arg;

	return scnprintf(val, PAGE_SIZE, "%d\n", bcdev->screen_is_on);
}

struct zte_misc_ops screen_on_node = {
	.node_name = "screen_on",
	.set = battery_chg_set_screen_on,
	.get = battery_chg_get_screen_on,
	.free = NULL,
	.arg = NULL,
};

int battery_chg_get_oem_charger_type(char *val, const void *arg)
{
	int rc;
	struct battery_chg_dev *bcdev = (struct battery_chg_dev *) arg;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_OEM_CHARGER_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(val, PAGE_SIZE, "%d\n", pst->prop[USB_OEM_CHARGER_TYPE]);
}

struct zte_misc_ops oem_charger_type_node = {
	.node_name = "charge_type_oem",
	.set = NULL,
	.get = battery_chg_get_oem_charger_type,
	.free = NULL,
	.arg = NULL,
};
#endif

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
static enum zte_power_supply_property zte_battery_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
};

static int zte_battery_psy_get_prop(struct zte_power_supply *psy,
		enum zte_power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = zte_power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		pval->intval = bcdev->charging_enabled ? 1 : 0;
		break;

	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		pval->intval = bcdev->battery_charging_enabled ? 1 : 0;
		break;

	default:
		break;
	}

	return 0;
}

static int zte_battery_psy_set_prop(struct zte_power_supply *psy,
		enum zte_power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = zte_power_supply_get_drvdata(psy);
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		bcdev->charging_enabled = (pval->intval == 1) ? true : false;
		if (bcdev->charging_enabled == 0) {
			/* disable charging*/
			/* ICL below 50mA is same with 0mA, but can differ with other client */
			rc = usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT, 49000);
		} else {
			rc = usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT, 0xFFFFFFFF);
		}
		pr_debug("prop=%d,en=%d\n", USB_INPUT_CURR_LIMIT, bcdev->charging_enabled);
		break;

	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		bcdev->battery_charging_enabled = (pval->intval == 1) ? true : false;
		if (bcdev->battery_charging_enabled == 0) {
			/* disable charging*/
			rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
					BATT_FCC_USER, 0);
		} else {
			rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
					BATT_FCC_USER, 0xFFFFFFFF);
		}
		pr_debug("prop=%d,en=%d,rc=%d\n", BATT_FCC_USER, bcdev->battery_charging_enabled, rc);
		break;

	default:
		return -EINVAL;
	}

	return rc;
}

static int zte_battery_psy_prop_is_writeable(struct zte_power_supply *psy,
		enum zte_power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct zte_power_supply_desc zte_batt_psy_desc = {
	.name			= "zte_battery",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_battery_props,
	.num_properties		= ARRAY_SIZE(zte_battery_props),
	.get_property		= zte_battery_psy_get_prop,
	.set_property		= zte_battery_psy_set_prop,
	.property_is_writeable	= zte_battery_psy_prop_is_writeable,
};

static enum zte_power_supply_property zte_usb_props[] = {
	POWER_SUPPLY_PROP_USB_SUSPEND,
	POWER_SUPPLY_PROP_USB_PRESENT,
};

static int zte_usb_psy_get_prop(struct zte_power_supply *psy,
		enum zte_power_supply_property prop,
		union power_supply_propval *pval)
{
	struct psy_state *pst = NULL;
	int rc = 0;
	struct battery_chg_dev *bcdev = zte_power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_USB_SUSPEND:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		rc = read_property_id(bcdev, pst, USB_SUSPEND);
		if (rc < 0)
			return rc;

		bcdev->usb_suspend = pst->prop[USB_SUSPEND];
		pval->intval = bcdev->usb_suspend ? 1 : 0;
		break;

	case POWER_SUPPLY_PROP_USB_PRESENT:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		rc = read_property_id(bcdev, pst, USB_PRESENT);
		if (rc < 0)
			return rc;

		bcdev->usb_present = pst->prop[USB_PRESENT];
		pval->intval = bcdev->usb_present ? 1 : 0;
		break;

	default:
		break;
	}

	return 0;
}

static int zte_usb_psy_set_prop(struct zte_power_supply *psy,
		enum zte_power_supply_property prop,
		const union power_supply_propval *pval)
{
	//struct battery_chg_dev *bcdev = zte_power_supply_get_drvdata(psy);
	switch (prop) {
	default:
		return -EINVAL;
	}

	return 0;
}

static int zte_usb_psy_prop_is_writeable(struct zte_power_supply *psy,
		enum zte_power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_USB_SUSPEND:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct zte_power_supply_desc zte_usb_psy_desc = {
	.name			= "zte_usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= zte_usb_props,
	.num_properties		= ARRAY_SIZE(zte_usb_props),
	.get_property		= zte_usb_psy_get_prop,
	.set_property		= zte_usb_psy_set_prop,
	.property_is_writeable	= zte_usb_psy_prop_is_writeable,
};
#endif

static int battery_chg_init_psy(struct battery_chg_dev *bcdev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = bcdev;
	psy_cfg.of_node = bcdev->dev->of_node;
	bcdev->psy_list[PSY_TYPE_USB].psy =
		devm_power_supply_register(bcdev->dev, &usb_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_USB].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_USB].psy);
		bcdev->psy_list[PSY_TYPE_USB].psy = NULL;
		pr_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_WLS].psy =
		devm_power_supply_register(bcdev->dev, &wls_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy);
		bcdev->psy_list[PSY_TYPE_WLS].psy = NULL;
		pr_err("Failed to register wireless power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].psy =
		devm_power_supply_register(bcdev->dev, &batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy);
		bcdev->psy_list[PSY_TYPE_BATTERY].psy = NULL;
		pr_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	bcdev->zte_battery_psy =
		zte_devm_power_supply_register(bcdev->dev, &zte_batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->zte_battery_psy)) {
		rc = PTR_ERR(bcdev->zte_battery_psy);
		pr_err("Failed to register zte_battery power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->zte_usb_psy =
		zte_devm_power_supply_register(bcdev->dev, &zte_usb_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->zte_usb_psy)) {
		rc = PTR_ERR(bcdev->zte_usb_psy);
		pr_err("Failed to register zte_usb power supply, rc=%d\n", rc);
		return rc;
	}
#endif

	return 0;
}

static int battery_chg_get_resistance_id(struct battery_chg_dev *bcdev)
{
	struct battery_charger_get_resistance_id_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_RESISTANCE_ID_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		pr_err("Failed to enable notification rc=%d\n", rc);

	return rc;
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);
	int rc;

	battery_chg_notify_enable(bcdev);

	/*
	 * Give some time after enabling notification so that USB adapter type
	 * information can be obtained properly which is essential for setting
	 * USB ICL.
	 */
	msleep(200);

	if (bcdev->last_fcc_ua) {
		rc = __battery_psy_set_charge_current(bcdev,
				bcdev->last_fcc_ua);
		if (rc < 0)
			pr_err("Failed to set FCC (%u uA), rc=%d\n",
				bcdev->last_fcc_ua, rc);
	}

	if (bcdev->usb_icl_ua) {
		rc = usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT,
				bcdev->usb_icl_ua);
		if (rc < 0)
			pr_err("Failed to set ICL(%u uA), rc=%d\n",
				bcdev->usb_icl_ua, rc);
	}
}

static int wireless_fw_send_firmware(struct battery_chg_dev *bcdev,
					const struct firmware *fw)
{
	struct wireless_fw_push_buf_req msg = {};
	const u8 *ptr;
	u32 i, num_chunks, partial_chunk_size;
	int rc;

	num_chunks = fw->size / WLS_FW_BUF_SIZE;
	partial_chunk_size = fw->size % WLS_FW_BUF_SIZE;

	if (!num_chunks)
		return -EINVAL;

	pr_debug("Updating FW...\n");

	ptr = fw->data;
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_WLS_FW_PUSH_BUF_REQ;

	for (i = 0; i < num_chunks; i++, ptr += WLS_FW_BUF_SIZE) {
		msg.fw_chunk_id = i + 1;
		memcpy(msg.buf, ptr, WLS_FW_BUF_SIZE);

		pr_debug("sending FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	if (partial_chunk_size) {
		msg.fw_chunk_id = i + 1;
		memset(msg.buf, 0, WLS_FW_BUF_SIZE);
		memcpy(msg.buf, ptr, partial_chunk_size);

		pr_debug("sending partial FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wireless_fw_check_for_update(struct battery_chg_dev *bcdev,
					u32 version, size_t size)
{
	struct wireless_fw_check_req req_msg = {};

	bcdev->wls_fw_update_reqd = false;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_CHECK_UPDATE;
	req_msg.fw_version = version;
	req_msg.fw_size = size;
	req_msg.fw_crc = bcdev->wls_fw_crc;

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#define IDT9415_FW_MAJOR_VER_OFFSET		0x84
#define IDT9415_FW_MINOR_VER_OFFSET		0x86
#define IDT_FW_MAJOR_VER_OFFSET		0x94
#define IDT_FW_MINOR_VER_OFFSET		0x96

static int wireless_fw_update(struct battery_chg_dev *bcdev, bool force)
{
	const struct firmware *fw;
	struct psy_state *pst;
	u32 version;
	u16 maj_ver, min_ver;
	int rc;

	if (!bcdev->wls_fw_name) {
		pr_err("wireless FW name is not specified\n");
		return -EINVAL;
	}

	pm_stay_awake(bcdev->dev);

	/*
	 * Check for USB presence. If nothing is connected, check whether
	 * battery SOC is at least 50% before allowing FW update.
	 */
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_ONLINE);
	if (rc < 0)
		goto out;

	if (!pst->prop[USB_ONLINE]) {
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_CAPACITY);
		if (rc < 0)
			goto out;

		if ((pst->prop[BATT_CAPACITY] / 100) < 50) {
			pr_err("Battery SOC should be at least 50%% or connect charger\n");
			rc = -EINVAL;
			goto out;
		}
	}

	rc = firmware_request_nowarn(&fw, bcdev->wls_fw_name, bcdev->dev);
	if (rc) {
		pr_err("Couldn't get firmware rc=%d\n", rc);
		goto out;
	}

	if (!fw || !fw->data || !fw->size) {
		pr_err("Invalid firmware\n");
		rc = -EINVAL;
		goto release_fw;
	}

	if (fw->size < SZ_16K) {
		pr_err("Invalid firmware size %zu\n", fw->size);
		rc = -EINVAL;
		goto release_fw;
	}

	if (strstr(bcdev->wls_fw_name, "9412")) {
		maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MAJOR_VER_OFFSET));
		min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MINOR_VER_OFFSET));
	} else {
		maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT9415_FW_MAJOR_VER_OFFSET));
		min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT9415_FW_MINOR_VER_OFFSET));
	}
	version = maj_ver << 16 | min_ver;

	if (force)
		version = UINT_MAX;

	pr_debug("FW size: %zu version: %#x\n", fw->size, version);

	rc = wireless_fw_check_for_update(bcdev, version, fw->size);
	if (rc < 0) {
		pr_err("Wireless FW update not needed, rc=%d\n", rc);
		goto release_fw;
	}

	if (!bcdev->wls_fw_update_reqd) {
		pr_warn("Wireless FW update not required\n");
		goto release_fw;
	}

	/* Wait for IDT to be setup by charger firmware */
	msleep(WLS_FW_PREPARE_TIME_MS);

	reinit_completion(&bcdev->fw_update_ack);
	rc = wireless_fw_send_firmware(bcdev, fw);
	if (rc < 0) {
		pr_err("Failed to send FW chunk, rc=%d\n", rc);
		goto release_fw;
	}

	pr_debug("Waiting for fw_update_ack\n");
	rc = wait_for_completion_timeout(&bcdev->fw_update_ack,
				msecs_to_jiffies(bcdev->wls_fw_update_time_ms));
	if (!rc) {
		pr_err("Error, timed out updating firmware\n");
		rc = -ETIMEDOUT;
		goto release_fw;
	} else {
		pr_debug("Waited for %d ms\n",
			bcdev->wls_fw_update_time_ms - jiffies_to_msecs(rc));
		rc = 0;
	}

	pr_info("Wireless FW update done\n");

release_fw:
	bcdev->wls_fw_crc = 0;
	release_firmware(fw);
out:
	pm_relax(bcdev->dev);

	return rc;
}

static ssize_t wireless_fw_update_time_ms_store(struct class *c,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtou32(buf, 0, &bcdev->wls_fw_update_time_ms))
		return -EINVAL;

	return count;
}

static ssize_t wireless_fw_update_time_ms_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->wls_fw_update_time_ms);
}
static CLASS_ATTR_RW(wireless_fw_update_time_ms);

static ssize_t wireless_fw_crc_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	u16 val;

	if (kstrtou16(buf, 0, &val) || !val)
		return -EINVAL;

	bcdev->wls_fw_crc = val;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_crc);

static ssize_t wireless_fw_version_show(struct class *c,
					struct class_attribute *attr,
					char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct wireless_fw_get_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_GET_VERSION;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%#x\n", bcdev->wls_fw_version);
}
static CLASS_ATTR_RO(wireless_fw_version);

static ssize_t wireless_fw_force_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, true);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_force_update);

static ssize_t wireless_fw_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, false);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_update);

static ssize_t wireless_type_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_ADAP_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			get_wls_type_name(pst->prop[WLS_ADAP_TYPE]));
}
static CLASS_ATTR_RO(wireless_type);

static ssize_t charge_control_en_store(struct class *c,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (val == bcdev->chg_ctrl_en)
		return count;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_EN, val);
	if (rc < 0) {
		pr_err("Failed to set charge_control_en, rc=%d\n", rc);
		return rc;
	}

	bcdev->chg_ctrl_en = val;

	return count;
}

static ssize_t charge_control_en_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->chg_ctrl_en);
}
static CLASS_ATTR_RW(charge_control_en);

static ssize_t usb_typec_compliant_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_TYPEC_COMPLIANT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)pst->prop[USB_TYPEC_COMPLIANT]);
}
static CLASS_ATTR_RO(usb_typec_compliant);

static ssize_t usb_real_type_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_REAL_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			get_usb_type_name(pst->prop[USB_REAL_TYPE]));
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t restrict_cur_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (kstrtou32(buf, 0, &fcc_ua) || fcc_ua > bcdev->thermal_fcc_ua)
		return -EINVAL;

	prev_fcc_ua = bcdev->restrict_fcc_ua;
	bcdev->restrict_fcc_ua = fcc_ua;
	if (bcdev->restrict_chg_en) {
		rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
		if (rc < 0) {
			bcdev->restrict_fcc_ua = prev_fcc_ua;
			return rc;
		}
	}

	return count;
}

static ssize_t restrict_cur_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->restrict_fcc_ua);
}
static CLASS_ATTR_RW(restrict_cur);

static ssize_t restrict_chg_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	bcdev->restrict_chg_en = val;
	rc = __battery_psy_set_charge_current(bcdev, bcdev->restrict_chg_en ?
			bcdev->restrict_fcc_ua : bcdev->thermal_fcc_ua);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t restrict_chg_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->restrict_chg_en);
}
static CLASS_ATTR_RW(restrict_chg);

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	bcdev->fake_soc = val;
	pr_debug("Set fake soc to %d\n", val);
/* add for fast_capacity start */
	if ((bcdev->fast_capacity_enable || IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG))
			&& pst->psy)
/* add for fast_capacity end */
		power_supply_changed(pst->psy);

	return count;
}

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->fake_soc);
}
static CLASS_ATTR_RW(fake_soc);

static ssize_t wireless_boost_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
				WLS_BOOST_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_boost_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_BOOST_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_BOOST_EN]);
}
static CLASS_ATTR_RW(wireless_boost_en);

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static ssize_t resistance_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[BATT_RESISTANCE]);
}
static CLASS_ATTR_RO(resistance);

static ssize_t soh_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SOH]);
}
static CLASS_ATTR_RO(soh);

static ssize_t ship_mode_en_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->ship_mode_en))
		return -EINVAL;

	return count;
}

static ssize_t ship_mode_en_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ship_mode_en);
}
static CLASS_ATTR_RW(ship_mode_en);

static ssize_t charging_enabled_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->charging_enabled))
		return -EINVAL;
	if (bcdev->charging_enabled == 0) {
		/* disable charging*/
		/* ICL below 50mA is same with 0mA, but can differ with other client */
		usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT, 49000);
	} else {
		usb_psy_set_icl(bcdev, USB_INPUT_CURR_LIMIT, 0xFFFFFFFF);
	}
	pr_info("charging_enabled_store, prop=%d,en=%d\n", USB_INPUT_CURR_LIMIT, bcdev->charging_enabled);
	return count;
}
static ssize_t charging_enabled_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->charging_enabled);
}
static CLASS_ATTR_RW(charging_enabled);

static ssize_t battery_charging_enabled_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc = 0;

	if (kstrtobool(buf, &bcdev->battery_charging_enabled))
		return -EINVAL;
	if (bcdev->battery_charging_enabled == 0) {
		/* disable charging*/
		rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_FCC_USER, 0);
	} else {
		rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_FCC_USER, 0xFFFFFFFF);
	}
	pr_info("battery_charging_enabled_store, prop=%d,en=%d,rc=%d\n", BATT_FCC_USER, bcdev->battery_charging_enabled, rc);

	return count;
}
static ssize_t battery_charging_enabled_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->battery_charging_enabled);
}
static CLASS_ATTR_RW(battery_charging_enabled);

static ssize_t typec_cc_orientation_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_TYPEC_CC_ORIENTATION);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_TYPEC_CC_ORIENTATION]);
}
static CLASS_ATTR_RO(typec_cc_orientation);
static ssize_t smb139x_state_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SMB139X_STATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SMB139X_STATE]);
}
static CLASS_ATTR_RO(smb139x_state);
static ssize_t oem_charger_type_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_OEM_CHARGER_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[USB_OEM_CHARGER_TYPE]);
}
static CLASS_ATTR_RO(oem_charger_type);

static ssize_t oem_battery_type_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtou16(buf, 0, &bcdev->oem_battery_type))
		return -EINVAL;

	pr_info("oem_battery_type_store,bcdev->oem_battery_type=%d\n", bcdev->oem_battery_type);

	return count;
}
static ssize_t oem_battery_type_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->oem_battery_type);
}
static CLASS_ATTR_RW(oem_battery_type);

static void battery_chg_screen_on_select_fcc_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev,
						screen_on_select_fcc_work.work);
	battery_psy_set_charge_current(bcdev, bcdev->curr_thermal_level);
}
static ssize_t screen_is_on_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	if (bcdev == NULL)
		return -EINVAL;
	if (kstrtobool(buf, &bcdev->screen_is_on))
		return -EINVAL;

	pr_info("screen_on_store,bcdev->screen_is_on=%d\n", bcdev->screen_is_on);
	{
		cancel_delayed_work_sync(&bcdev->screen_on_select_fcc_work);
		if (bcdev->screen_is_on) {
			schedule_delayed_work(&bcdev->screen_on_select_fcc_work, msecs_to_jiffies(1000 * 70));
		} else {
			schedule_delayed_work(&bcdev->screen_on_select_fcc_work, msecs_to_jiffies(1000));
		}
	}
	if (bcdev->screen_is_on) {
		cancel_delayed_work(&bcdev->update_soc_work);
		schedule_delayed_work(&bcdev->update_soc_work, msecs_to_jiffies(50));
		pr_info("start battery_chg_update_soc_work for screen on\n");
	}
	write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY], BATT_SCREEN_ON, bcdev->screen_is_on);
	return count;
}
static ssize_t screen_is_on_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->screen_is_on);
}
static CLASS_ATTR_RW(screen_is_on);

static ssize_t resistance_id_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	battery_chg_get_resistance_id(bcdev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->resistance_id);
}
static CLASS_ATTR_RO(resistance_id);

static ssize_t recharge_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc = 0;

	if (kstrtou32(buf, 10, &bcdev->recharge_soc))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_RECHARGE_SOC, bcdev->recharge_soc);

	return count;
}

static ssize_t recharge_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RECHARGE_SOC);
	if (rc < 0)
		return rc;

	bcdev->recharge_soc = pst->prop[BATT_RECHARGE_SOC];

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->recharge_soc);
}
static CLASS_ATTR_RW(recharge_soc);

static ssize_t charge_mode_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc = 0;

	if (kstrtoint(buf, 0, &bcdev->charge_mode))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHARGE_MODE, bcdev->charge_mode);

	return count;
}

static ssize_t charge_mode_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_CHARGE_MODE);
	if (rc < 0)
		return rc;

	bcdev->charge_mode= pst->prop[BATT_CHARGE_MODE];

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->charge_mode);
}
static CLASS_ATTR_RW(charge_mode);

static ssize_t usb_suspend_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_SUSPEND);
	if (rc < 0)
		return rc;

	bcdev->usb_suspend = pst->prop[USB_SUSPEND];

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->usb_suspend);
}
static CLASS_ATTR_RO(usb_suspend);

static ssize_t usb_present_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_PRESENT);
	if (rc < 0)
		return rc;

	bcdev->usb_present = pst->prop[USB_PRESENT];

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->usb_present);
}
static CLASS_ATTR_RO(usb_present);


static int battery_chg_set_adsp_debug(struct battery_chg_dev *bcdev)
{
	struct battery_charger_adsp_debug_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_ADSP_DEBUG;
	req_msg.adsp_debug = bcdev->adsp_debug;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		pr_err("Failed to enable notification rc=%d\n", rc);

	return rc;
}

static ssize_t adsp_debug_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc = 0;

	if (kstrtou32(buf, 10, &bcdev->adsp_debug))
		return -EINVAL;

	rc = battery_chg_set_adsp_debug(bcdev);

	return count;
}

static ssize_t adsp_debug_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->adsp_debug);
}
static CLASS_ATTR_RW(adsp_debug);

static ssize_t ssoc_full_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
    int rc;

	if (kstrtou32(buf, 10, &bcdev->ssoc_full))
		return -EINVAL;
	pr_info("store ssoc_full=%d\n",  bcdev->ssoc_full);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_SSOC_FULL, bcdev->ssoc_full);

	return count;
}

static ssize_t ssoc_full_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
    int rc;

	rc = read_property_id(bcdev, pst, BATT_SSOC_FULL);

	pr_info("show ssoc_full=%d\n",  bcdev->ssoc_full);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ssoc_full);
}
static CLASS_ATTR_RW(ssoc_full);

static ssize_t rsoc_full_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
    int rc;

	if (kstrtou32(buf, 10, &bcdev->rsoc_full))
		return -EINVAL;
	pr_info("store rsoc_full=%d\n",  bcdev->rsoc_full);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_RSOC_FULL, bcdev->rsoc_full);

	return count;
}

static ssize_t rsoc_full_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
    struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
    int rc;

    rc = read_property_id(bcdev, pst, BATT_RSOC_FULL);
	pr_info("show rsoc_full=%d\n",  bcdev->rsoc_full);
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->rsoc_full);
}
static CLASS_ATTR_RW(rsoc_full);

static ssize_t batt_pmic_temp_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_PMIC_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_PMIC_TEMP]);
}
static CLASS_ATTR_RO(batt_pmic_temp);
static ssize_t batt_smb139x_temp1_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SMB139X_TEMP1);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SMB139X_TEMP1]);
}
static CLASS_ATTR_RO(batt_smb139x_temp1);
static ssize_t batt_smb139x_temp2_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SMB139X_TEMP2);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SMB139X_TEMP2]);
}
static CLASS_ATTR_RO(batt_smb139x_temp2);

static struct attribute *battery_class_attrs[] = {
	&class_attr_soh.attr,
	&class_attr_resistance.attr,
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	&class_attr_wireless_boost_en.attr,
	&class_attr_fake_soc.attr,
	&class_attr_wireless_fw_update.attr,
	&class_attr_wireless_fw_force_update.attr,
	&class_attr_wireless_fw_version.attr,
	&class_attr_wireless_fw_crc.attr,
	&class_attr_wireless_fw_update_time_ms.attr,
	&class_attr_wireless_type.attr,
	&class_attr_ship_mode_en.attr,
	&class_attr_restrict_chg.attr,
	&class_attr_restrict_cur.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_usb_typec_compliant.attr,
	&class_attr_charge_control_en.attr,
	&class_attr_charging_enabled.attr,
	&class_attr_battery_charging_enabled.attr,
	&class_attr_typec_cc_orientation.attr,
	&class_attr_smb139x_state.attr,
	&class_attr_oem_charger_type.attr,
	&class_attr_oem_battery_type.attr,
	&class_attr_screen_is_on.attr,
	&class_attr_resistance_id.attr,
	&class_attr_recharge_soc.attr,
	&class_attr_charge_mode.attr,
	&class_attr_usb_suspend.attr,
	&class_attr_usb_present.attr,
	&class_attr_adsp_debug.attr,
	&class_attr_ssoc_full.attr,
	&class_attr_rsoc_full.attr,
	&class_attr_batt_pmic_temp.attr,
	&class_attr_batt_smb139x_temp1.attr,
	&class_attr_batt_smb139x_temp2.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		pr_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	bcdev->debugfs_dir = dir;
	debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int i, rc, len;
	u32 prev, val;

	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);
	of_property_read_u32(node, "qcom,shutdown-voltage",
				&bcdev->shutdown_volt_mv);

/* add for fast_capacity start */
	bcdev->fast_capacity_enable = of_property_read_bool(node, "qcom,fast-capacity-enable");

	pr_info("fast_capacity_enable enabled = %d\n", bcdev->fast_capacity_enable);
/* add for fast_capacity end */

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0) {
		pr_err("Failed to read prop BATT_CHG_CTRL_LIM_MAX, rc=%d\n",
			rc);
		return rc;
	}

	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0) {

		rc = of_property_read_u32(node, "qcom,thermal-mitigation-step",
						&val);

		if (rc < 0)
			return 0;

		if (val < 500000 || val >= pst->prop[BATT_CHG_CTRL_LIM_MAX]) {
			pr_err("thermal_fcc_step %d is invalid\n", val);
			return -EINVAL;
		}

		bcdev->thermal_fcc_step = val;
		len = pst->prop[BATT_CHG_CTRL_LIM_MAX] / bcdev->thermal_fcc_step;

		/*
		 * FCC values must be above 500mA.
		 * Since len is truncated when calculated, check and adjust len so
		 * that the above requirement is met.
		 */
		if (pst->prop[BATT_CHG_CTRL_LIM_MAX] - (bcdev->thermal_fcc_step * len) < 500000)
			len = len - 1;
	} else {
		bcdev->thermal_fcc_step = 0;
		len = rc;
		prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

		for (i = 0; i < len; i++) {
			rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
				i, &val);
			if (rc < 0)
				return rc;
/*  we use screen on/off config,so the value is not descending.
			if (val > prev) {
				pr_err("Thermal levels should be in descending order\n");
				bcdev->num_thermal_levels = -EINVAL;
				return 0;
			}
*/
			prev = val;
		}

		bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
						sizeof(*bcdev->thermal_levels),
						GFP_KERNEL);
		if (!bcdev->thermal_levels)
			return -ENOMEM;

		/*
		 * Element 0 is for normal charging current. Elements from index 1
		 * onwards is for thermal mitigation charging currents.
		 */

		bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

		rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
		if (rc < 0) {
			pr_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
			return rc;
		}
#ifdef CONFIG_ZTE_FEATURE_CHG_BATT_2S
		if(charger_165W()){
			memcpy(&bcdev->thermal_levels[1], array_165w, sizeof(array_165w));
		}else if(charger_65W()){
			memcpy(&bcdev->thermal_levels[1], array_65w, sizeof(array_65w));
		}
#endif
	}

	bcdev->num_thermal_levels = len;
	bcdev->thermal_fcc_ua = pst->prop[BATT_CHG_CTRL_LIM_MAX];
	pr_info("qcom,thermal-mitigation num=%d,high=%d\n", bcdev->num_thermal_levels, bcdev->thermal_levels[0]);

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_notify_msg msg_notify = { { 0 } };
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	msg_notify.hdr.owner = MSG_OWNER_BC;
	msg_notify.hdr.type = MSG_TYPE_NOTIFY;
	msg_notify.hdr.opcode = BC_SHUTDOWN_NOTIFY;

	rc = battery_chg_write(bcdev, &msg_notify, sizeof(msg_notify));
	if (rc < 0)
		pr_err("Failed to send shutdown notification rc=%d\n", rc);
#if 1
	/* wait for adsp request 5V for PD adapters */
	msleep(100);
#endif

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

static void battery_chg_update_soc_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev,
				update_soc_work.work);
	struct psy_state *pst;
	int rc = 0;
	int soc = 0;
	unsigned long delay_ms = 0;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = read_property_id(bcdev, pst, BATT_CAPACITY);
	if (!rc) {
		soc = pst->prop[BATT_CAPACITY];
	}
	if (bcdev->last_soc == 0) {
		bcdev->last_soc = soc;
	}
	battery_chg_update_healthd_prop(bcdev);
	if ((bcdev->last_soc - soc >= 1) || (soc - bcdev->last_soc >= 1)) {
		pr_info("battery_chg_update_soc_work last_soc=%d,soc=%d\n", bcdev->last_soc, soc);
		bcdev->last_soc = soc;
		power_supply_changed(pst->psy);
	}

	if (bcdev->screen_is_on) {
		delay_ms = msecs_to_jiffies(UPDATE_SOC_WORK_TIME_MS);
	} else {
		delay_ms = msecs_to_jiffies(1000 * 60 * 10);
	}
	pr_info("screen_is_on=%d,delay=%d\n", bcdev->screen_is_on, delay_ms);
	schedule_delayed_work(&bcdev->update_soc_work, delay_ms);
}

#if 0
static void panel_event_notifier_callback(enum panel_event_notifier_tag tag,
			struct panel_event_notification *notification, void *data)
{
	struct battery_chg_dev *bcdev = data;

	if (!notification) {
		pr_debug("Invalid panel notification\n");
		return;
	}

	pr_debug("panel event received, type: %d\n", notification->notif_type);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
		battery_chg_notify_disable(bcdev);
		break;
	case DRM_PANEL_EVENT_UNBLANK:
		battery_chg_notify_enable(bcdev);
		break;
	default:
		pr_debug("Ignore panel event: %d\n", notification->notif_type);
		break;
	}
}

static int battery_chg_register_panel_notifier(struct battery_chg_dev *bcdev)
{
	struct device_node *np = bcdev->dev->of_node;
	struct device_node *pnode;
	struct drm_panel *panel, *active_panel = NULL;
	void *cookie = NULL;
	int i, count, rc;

	count = of_count_phandle_with_args(np, "qcom,display-panels", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(np, "qcom,display-panels", i);
		if (!pnode)
			return -ENODEV;

		panel = of_drm_find_panel(pnode);
		of_node_put(pnode);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			break;
		}
	}

	if (!active_panel) {
		rc = PTR_ERR(panel);
		if (rc != -EPROBE_DEFER)
			dev_err(bcdev->dev, "Failed to find active panel, rc=%d\n", rc);
		return rc;
	}

	cookie = panel_event_notifier_register(
			PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER,
			active_panel,
			panel_event_notifier_callback,
			(void *)bcdev);
	if (IS_ERR(cookie)) {
		rc = PTR_ERR(cookie);
		dev_err(bcdev->dev, "Failed to register panel event notifier, rc=%d\n", rc);
		return rc;
	}

	pr_debug("register panel notifier successful\n");
	bcdev->notifier_cookie = cookie;
	return 0;
}
#endif

static int
battery_chg_get_max_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct battery_chg_dev *bcdev = tcd->devdata;

	*state = bcdev->num_thermal_levels;

	return 0;
}

static int
battery_chg_get_cur_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct battery_chg_dev *bcdev = tcd->devdata;

	*state = bcdev->curr_thermal_level;

	return 0;
}

static int
battery_chg_set_cur_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long state)
{
	struct battery_chg_dev *bcdev = tcd->devdata;

	return battery_psy_set_charge_current(bcdev, (int)state);
}

static const struct thermal_cooling_device_ops battery_tcd_ops = {
	.get_max_state = battery_chg_get_max_charge_cntl_limit,
	.get_cur_state = battery_chg_get_cur_charge_cntl_limit,
	.set_cur_state = battery_chg_set_cur_charge_cntl_limit,
};

static int battery_chg_probe(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	struct thermal_cooling_device *tcd;
	struct psy_state *pst;
	int rc, i;

#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
	zlog_chg_client = zlog_register_client(&zlog_chg_dev);
	if (!zlog_chg_client) {
		dev_err(&pdev->dev, "zlog register client zlog_chg_dev fail\n");
	}
#endif

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_WLS].map = wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
	init_rwsem(&bcdev->state_sem);
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);
	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
	INIT_WORK(&bcdev->battery_check_work, battery_chg_check_status_work);
/* add for fast_capacity start */
	INIT_DELAYED_WORK(&bcdev->report_fast_capacity_work, battery_chg_report_fast_capacity_work);
	INIT_WORK(&bcdev->update_prop_work, battery_chg_update_prop_work);
	bcdev->fast_capacity = -EINVAL;
	bcdev->discharging_smooth = false;
/* add for fast_capacity end */
	INIT_DELAYED_WORK(&bcdev->screen_on_select_fcc_work, battery_chg_screen_on_select_fcc_work);
	INIT_DELAYED_WORK(&bcdev->update_soc_work, battery_chg_update_soc_work);
	schedule_delayed_work(&bcdev->update_soc_work, msecs_to_jiffies(UPDATE_SOC_WORK_TIME_MS));
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;
#if 0
	rc = battery_chg_register_panel_notifier(bcdev);
	if (rc < 0)
		return rc;
#endif
	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		goto reg_error;
	}
	bcdev->charging_enabled = true;
	bcdev->battery_charging_enabled = true;
	bcdev->oem_battery_type = OEM_BATTERY_TYPE_NORMAL;
	bcdev->screen_is_on = true;
	bcdev->resistance_id = 0;
	bcdev->recharge_soc = 0;
	bcdev->usb_suspend = false;
	bcdev->curr_thermal_level = 0;
	down_write(&bcdev->state_sem);
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	/*
	 * This should be initialized here so that battery_chg_callback
	 * can run successfully when battery_chg_parse_dt() starts
	 * reading BATT_CHG_CTRL_LIM_MAX parameter and waits for a response.
	 */
	bcdev->initialized = true;
	up_write(&bcdev->state_sem);

	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	bcdev->healthd_batt_prop_masks = 0;
	bcdev->healthd_usb_prop_masks = 0;
	register_reboot_notifier(&bcdev->reboot_notifier);

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0) {
		dev_err(dev, "Failed to parse dt rc=%d\n", rc);
		goto error;
	}

	bcdev->restrict_fcc_ua = DEFAULT_RESTRICT_FCC_UA;
	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;
	rc = battery_chg_init_psy(bcdev);
	if (rc < 0)
		goto error;

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		dev_err(dev, "Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	tcd = devm_thermal_of_cooling_device_register(dev, dev->of_node,
			(char *)pst->psy->desc->name, bcdev, &battery_tcd_ops);
	if (IS_ERR_OR_NULL(tcd)) {
		rc = PTR_ERR_OR_ZERO(tcd);
		dev_err(dev, "Failed to register thermal cooling device rc=%d\n",
			rc);
		class_unregister(&bcdev->battery_class);
		goto error;
	}

	bcdev->wls_fw_update_time_ms = WLS_FW_UPDATE_TIME_MS;

#if IS_ENABLED(CONFIG_VENDOR_ZTE_MISC)
	zte_misc_register_callback(&screen_on_node, (void *)bcdev);
	zte_misc_register_callback(&oem_charger_type_node, (void *)bcdev);
#endif

	battery_chg_add_debugfs(bcdev);
	bcdev->notify_en = false;
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);
	schedule_work(&bcdev->usb_type_work);

	return 0;
error:
	cancel_work_sync(&bcdev->subsys_up_work);
	down_write(&bcdev->state_sem);
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_DOWN);
	bcdev->initialized = false;
	up_write(&bcdev->state_sem);

	pmic_glink_unregister_client(bcdev->client);
	cancel_work_sync(&bcdev->usb_type_work);
	cancel_work_sync(&bcdev->subsys_up_work);
	cancel_work_sync(&bcdev->battery_check_work);
	complete(&bcdev->ack);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
reg_error:
	if (bcdev->notifier_cookie)
		panel_event_notifier_unregister(bcdev->notifier_cookie);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);

	down_write(&bcdev->state_sem);
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_DOWN);
	bcdev->initialized = false;
	up_write(&bcdev->state_sem);

	if (bcdev->notifier_cookie)
		panel_event_notifier_unregister(bcdev->notifier_cookie);

	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	pmic_glink_unregister_client(bcdev->client);
	cancel_work_sync(&bcdev->subsys_up_work);
	cancel_work_sync(&bcdev->usb_type_work);
	cancel_work_sync(&bcdev->battery_check_work);
	unregister_reboot_notifier(&bcdev->reboot_notifier);

	return 0;
}

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "qcom,battery-charger" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "qti_battery_charger",
		.of_match_table = battery_chg_match_table,
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
};
module_platform_driver(battery_chg_driver);

MODULE_DESCRIPTION("QTI Glink battery charger driver");
MODULE_LICENSE("GPL v2");
