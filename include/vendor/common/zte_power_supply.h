/*
 *  Functions private to power supply class
 *
 */

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>

#include <linux/power_supply.h>

struct zte_power_supply;

/*
 * All voltages, currents, charges, energies, time and temperatures in uV,
 * µA, µAh, µWh, seconds and tenths of degree Celsius unless otherwise
 * stated. It's driver's job to convert its raw values to units in which
 * this class operates.
 */

/*
 * For systems where the charger determines the maximum battery capacity
 * the min and max fields should be used to present these values to user
 * space. Unused/unknown fields will not appear in sysfs.
 */
enum zte_power_supply_property {
	/* Local extensions */
	POWER_SUPPLY_PROP_USB_HC,
	POWER_SUPPLY_PROP_USB_OTG,
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_RESISTANCE_ID,
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_CAPACITY_RAW,
	POWER_SUPPLY_PROP_CURRENT_COUNTER_ZTE,
	POWER_SUPPLY_PROP_BATTERY_ID,
	POWER_SUPPLY_PROP_TUNING_VINDPM,
	POWER_SUPPLY_PROP_FEED_WATCHDOG,
	POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_LPM_USB_DISCON,
	POWER_SUPPLY_PROP_USB_SUSPEND,
	POWER_SUPPLY_PROP_USB_PRESENT,
};

/* Description of power supply */
struct zte_power_supply_desc {
	const char *name;
	enum power_supply_type type;
	enum power_supply_usb_type *usb_types;
	size_t num_usb_types;
	enum zte_power_supply_property *properties;
	size_t num_properties;

	/*
	 * Functions for drivers implementing power supply class.
	 * These shouldn't be called directly by other drivers for accessing
	 * this power supply. Instead use power_supply_*() functions (for
	 * example power_supply_get_property()).
	 */
	int (*get_property)(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    union power_supply_propval *val);
	int (*set_property)(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    const union power_supply_propval *val);
	/*
	 * property_is_writeable() will be called during registration
	 * of power supply. If this happens during device probe then it must
	 * not access internal data of device (because probe did not end).
	 */
	int (*property_is_writeable)(struct zte_power_supply *psy,
				     enum zte_power_supply_property psp);
	void (*external_power_changed)(struct zte_power_supply *psy);
	void (*set_charged)(struct zte_power_supply *psy);

	/*
	 * Set if thermal zone should not be created for this power supply.
	 * For example for virtual supplies forwarding calls to actual
	 * sensors or other supplies.
	 */
	bool no_thermal;
	/* For APM emulation, think legacy userspace. */
	int use_for_apm;
};

struct zte_power_supply {
	const struct zte_power_supply_desc *desc;

	char **supplied_to;
	size_t num_supplicants;

	char **supplied_from;
	size_t num_supplies;
	struct device_node *of_node;

	/* Driver private data */
	void *drv_data;

	/* private */
	struct device dev;
	struct work_struct changed_work;
	struct delayed_work deferred_register_work;
	spinlock_t changed_lock;
	bool changed;
	bool initialized;
	bool removing;
	atomic_t use_cnt;
#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd;
	struct thermal_cooling_device *tcd;
#endif

#ifdef CONFIG_LEDS_TRIGGERS
	struct led_trigger *charging_full_trig;
	char *charging_full_trig_name;
	struct led_trigger *charging_trig;
	char *charging_trig_name;
	struct led_trigger *full_trig;
	char *full_trig_name;
	struct led_trigger *online_trig;
	char *online_trig_name;
	struct led_trigger *charging_blink_full_solid_trig;
	char *charging_blink_full_solid_trig_name;
#endif
};

extern struct atomic_notifier_head zte_power_supply_notifier;

extern struct zte_power_supply *zte_power_supply_get_by_name(const char *name);
extern void zte_power_supply_put(struct zte_power_supply *psy);
#ifdef CONFIG_OF
extern struct zte_power_supply *zte_power_supply_get_by_phandle(struct device_node *np,
							const char *property);
extern int zte_power_supply_get_by_phandle_array(struct device_node *np,
					     const char *property,
					     struct zte_power_supply **psy,
					     ssize_t size);
extern struct zte_power_supply *zte_devm_power_supply_get_by_phandle(
				    struct device *dev, const char *property);
#else /* !CONFIG_OF */
static inline struct zte_power_supply *
zte_power_supply_get_by_phandle(struct device_node *np, const char *property)
{ return NULL; }
static inline int
zte_power_supply_get_by_phandle_array(struct device_node *np,
				  const char *property,
				  struct zte_power_supply **psy,
				  int size)
{ return 0; }
static inline struct zte_power_supply *
zte_devm_power_supply_get_by_phandle(struct device *dev, const char *property)
{ return NULL; }
#endif /* CONFIG_OF */

extern int zte_power_supply_get_battery_info(struct zte_power_supply *psy,
					 struct power_supply_battery_info *info);
extern void zte_power_supply_put_battery_info(struct zte_power_supply *psy,
					  struct power_supply_battery_info *info);
extern int zte_power_supply_ocv2cap_simple(struct power_supply_battery_ocv_table *table,
				       int table_len, int ocv);
extern struct power_supply_battery_ocv_table *
zte_power_supply_find_ocv2cap_table(struct power_supply_battery_info *info,
				int temp, int *table_len);
extern int zte_power_supply_batinfo_ocv2cap(struct power_supply_battery_info *info,
					int ocv, int temp);
extern int
zte_power_supply_temp2resist_simple(struct power_supply_resistance_temp_table *table,
				int table_len, int temp);
extern void zte_power_supply_changed(struct zte_power_supply *psy);
extern int zte_power_supply_am_i_supplied(struct zte_power_supply *psy);
extern int zte_power_supply_set_input_current_limit_from_supplier(
					 struct zte_power_supply *psy);
extern int zte_power_supply_set_battery_charged(struct zte_power_supply *psy);

#ifdef CONFIG_POWER_SUPPLY
extern int zte_power_supply_is_system_supplied(void);
#else
static inline int zte_power_supply_is_system_supplied(void) { return -ENOSYS; }
#endif

extern int zte_power_supply_get_property(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    union power_supply_propval *val);
extern int zte_power_supply_set_property(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    const union power_supply_propval *val);
extern int zte_power_supply_property_is_writeable(struct zte_power_supply *psy,
					enum zte_power_supply_property psp);
extern void zte_power_supply_external_power_changed(struct zte_power_supply *psy);

extern struct zte_power_supply *__must_check
zte_power_supply_register(struct device *parent,
				 const struct zte_power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct zte_power_supply *__must_check
zte_power_supply_register_no_ws(struct device *parent,
				 const struct zte_power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct zte_power_supply *__must_check
zte_devm_power_supply_register(struct device *parent,
				 const struct zte_power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct zte_power_supply *__must_check
zte_devm_power_supply_register_no_ws(struct device *parent,
				 const struct zte_power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern void zte_power_supply_unregister(struct zte_power_supply *psy);
extern int zte_power_supply_powers(struct zte_power_supply *psy, struct device *dev);

extern void *zte_power_supply_get_drvdata(struct zte_power_supply *psy);
/* For APM emulation, think legacy userspace. */
extern struct class *zte_power_supply_class;


#ifdef CONFIG_SYSFS
extern void zte_power_supply_init_attrs(struct device_type *dev_type);
extern int zte_power_supply_uevent(struct device *dev, struct kobj_uevent_env *env);
#else
static inline void zte_power_supply_init_attrs(struct device_type *dev_type) {}
#define power_supply_uevent NULL

#endif /* CONFIG_SYSFS */

#ifdef CONFIG_LEDS_TRIGGERS

extern void zte_power_supply_update_leds(struct zte_power_supply *psy);
#else
static inline void zte_power_supply_update_leds(struct zte_power_supply *psy) {}
#endif /* CONFIG_LEDS_TRIGGERS */
