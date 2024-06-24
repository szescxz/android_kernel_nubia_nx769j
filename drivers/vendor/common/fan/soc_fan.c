// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019, 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/completion.h>
#include <asm/atomic.h>

#define LOG_TAG "NB_FAN"
#define FAN_DBG(fmt, args...)                                                \
	do {                                                                   \
		printk(KERN_DEBUG "[%s] [%s:%d] " fmt, LOG_TAG, __FUNCTION__,  \
				__LINE__, ##args);                                      \
	} while (0)

#define LABEL_LED  "led"
#define LABEL_FAN  "fan"

#define CAL_STAT_START    0X01
#define CAL_STAT_PROCING  0X02
#define CAL_STAT_END      0X03

#define FAN_MIN_RPM       4000
#define FAN_SPEED_STABLE		1
#define FAN_DEF_VOL       2800000

enum therm_product_id {
	FAN_LEVEL_0 = 0,   //close fan
	FAN_LEVEL_1,
	FAN_LEVEL_2,
	FAN_LEVEL_3,
	FAN_LEVEL_4,
	FAN_LEVEL_5,
	FAN_LEVEL_MAX = FAN_LEVEL_5,
};
struct fan_dev {
	u8 level;
	u8 fan_level_bak;
	u8 cal_stat;
	atomic_t fg_count;
	volatile u32 real_count;
	bool   enable;
	int    pwm_gpio;
	int    speed_gpio;
	int    en_gpio;
	int    speed_irq;
	const char		*label;
	struct hrtimer  timer;
	struct pwm_device *pwm_dev;
	struct pwm_state  pstate;
	struct completion m;
};
struct led_dev {
	bool        enable;
	u64			on_ms;
	u64			off_ms;
	u64			fade_ms;
	u8          brightness;
	const char		*label;
	struct pwm_device *pwm_dev;
	struct pwm_state  pstate;
};
struct nb_chip {
	u8     led_id;
	u8     num_leds;
	struct device		*dev;
	struct fan_dev	    *fan;
	struct led_dev	    *leds;
	struct led_dev	    *leds_bak;
	struct kobject      *kobj;
	struct mutex		lock;
	struct regulator    *avdd_ldo;
	struct pinctrl_state *pins_active;
};

struct nb_chip *chip = NULL;

static int nb_fan_power_set(struct fan_dev *fan, bool enable);

static int led_stat_set(struct led_dev led)
{
	int rc = 0;
	
	return rc;
}
static enum hrtimer_restart fan_hrtimer_hander(struct hrtimer *timer)
{
	ktime_t ktime;

	if(chip->fan->cal_stat == CAL_STAT_START){
		atomic_set(&chip->fan->fg_count, 0);
		chip->fan->cal_stat = CAL_STAT_PROCING;
		ktime = ktime_set(0, 500000000);	//delay 500ms
		hrtimer_start(&chip->fan->timer, ktime, HRTIMER_MODE_REL);
		enable_irq(chip->fan->speed_irq);
		FAN_DBG("fan_level=%d start calculate, count=%d.", chip->fan->level, atomic_read(&chip->fan->fg_count));
		return HRTIMER_RESTART;
	}
	if(chip->fan->cal_stat == CAL_STAT_PROCING){
		chip->fan->cal_stat = CAL_STAT_END;
		chip->fan->real_count = atomic_read(&chip->fan->fg_count);
		disable_irq(chip->fan->speed_irq);
		FAN_DBG("fan_level=%d stop calculate, count=%d.", chip->fan->level, atomic_read(&chip->fan->fg_count));
	}

    return HRTIMER_NORESTART;
}
static int fan_level_set(struct fan_dev *fan, u8 level)
{
	int rc;
	struct pwm_state pstate;

	if(level == fan->level){
	    FAN_DBG("fan have in level=%d.\n", level);
		return 0;
 	}
	if(level==FAN_LEVEL_0)
	    nb_fan_power_set(chip->fan, false);
	else
	    nb_fan_power_set(chip->fan, true);

	pwm_get_state(fan->pwm_dev, &pstate);
	pstate.enabled = (level==FAN_LEVEL_0)?false:true;
	pstate.period = 40000;
	pstate.duty_cycle = 8000*level;
	//pstate.usage_power = false;

	rc = pwm_apply_state(fan->pwm_dev, &pstate);
	if (rc < 0){
		FAN_DBG("Apply PWM state for fan_level=%d failed, rc=%d\n", level, rc);
		return rc;
	}
	fan->level = level;

	return rc;
}
static int fan_pwm_set(struct fan_dev *fan, u8 pwm)
{
	int rc;
	struct pwm_state pstate;


	pwm_get_state(fan->pwm_dev, &pstate);
	pstate.enabled = true;
	pstate.period = 40000;
	pstate.duty_cycle = 400*pwm;
	//pstate.usage_power = false;

	rc = pwm_apply_state(fan->pwm_dev, &pstate);
	if (rc < 0){
		FAN_DBG("Apply PWM state for fan_pwm=%d failed, rc=%d\n", pwm, rc);
		return rc;
	}

	return rc;
}
static int nb_fan_power_set(struct fan_dev *fan, bool enable)
{
	int ret = 0;

	if(enable) {
	    gpio_direction_output(chip->fan->en_gpio, 1);
	    FAN_DBG("NB_FAN: open fan power!\n");
	}
	else {
	    gpio_direction_output(chip->fan->en_gpio, 0);
	    FAN_DBG("NB_FAN: close fan power!\n");
	}
	/*if (enable==false && pwr_reg) {
		if (regulator_is_enabled(pwr_reg) !=0){
			ret = regulator_disable(pwr_reg);
			FAN_DBG("NB_FAN: close fan power!ret=%d.\n", ret);
		}
	}

	if (enable==true && pwr_reg) {
		if (regulator_is_enabled(pwr_reg) ==0){
			ret = regulator_enable(pwr_reg);
			msleep(10);
			FAN_DBG("NB_FAN: open fan power!ret=%d.\n", ret);
		}
	}*/
	msleep(100);
	FAN_DBG("Fan pwoer gpio status:%d", gpio_get_value(chip->fan->en_gpio));

   return ret;
}
irqreturn_t fan_speed_irq_proc(int irq, void *dev_id)
{
	struct nb_chip *chip = (struct nb_chip *)dev_id;
	
	atomic_inc(&chip->fan->fg_count);

	return IRQ_HANDLED;
}
static int fan_rpm_get(struct fan_dev *fan, u32 *p_rpm)
{
	ktime_t ktime;
	u32 adj_count = 0;

	fan->cal_stat = CAL_STAT_START;
	ktime = ktime_set(0, 1000000);	//delay 1ms
	hrtimer_start(&fan->timer, ktime, HRTIMER_MODE_REL);
	msleep(1000);
	*p_rpm = fan->real_count*40;
	
	if(FAN_SPEED_STABLE){
		if(fan->level==FAN_LEVEL_3 && *p_rpm>FAN_MIN_RPM){
			adj_count = 13000 + fan->real_count;
		}
		if(fan->level==FAN_LEVEL_4 && *p_rpm>FAN_MIN_RPM){
			adj_count = 16000 + fan->real_count;
		}
		if(fan->level==FAN_LEVEL_5 && *p_rpm>FAN_MIN_RPM){
			adj_count = 19000 + fan->real_count;
		}
	}

	FAN_DBG("NB_FAN:level:%d real_count:%d adj_count:%d.\n", fan->level, *p_rpm, adj_count);
	
	*p_rpm = adj_count>*p_rpm?adj_count:*p_rpm;

	return 0;
}
static ssize_t fan_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	FAN_DBG("fan_enable=%d.\n", chip->fan->enable);
	return sprintf(buf, "%d\n", chip->fan->enable);
}

static ssize_t fan_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	bool enable = false;

 	sscanf(buf, "%d", &enable);
	if(enable == chip->fan->enable){
		FAN_DBG("fan have been enabled!");
		return count;
	}
	chip->fan->enable = enable;
    FAN_DBG("fan_enable=%d\n", chip->fan->enable);

	mutex_lock(&chip->lock);
	if(!chip->fan->enable){
		chip->fan->fan_level_bak = chip->fan->level;
        fan_level_set(chip->fan, FAN_LEVEL_0);
    }else{
		if(chip->fan->fan_level_bak != FAN_LEVEL_0){
			fan_level_set(chip->fan, chip->fan->fan_level_bak);
			FAN_DBG("fan enabled,back fan level:%d to run.\n", chip->fan->fan_level_bak);
			chip->fan->fan_level_bak = FAN_LEVEL_0;
		}
	}
	mutex_unlock(&chip->lock);

    return count;
}

static ssize_t fan_level_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->fan->enable)
		return sprintf(buf, "%d\n", chip->fan->level);
	else
		return sprintf(buf, "%d\n", chip->fan->fan_level_bak);
}

static ssize_t fan_level_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
    unsigned int level;

 	sscanf(buf, "%d", &level);

	if(level < FAN_LEVEL_0 || level > FAN_LEVEL_MAX){
		FAN_DBG("level=%d,it is a bad level,not in(%d-%d).\n",level,FAN_LEVEL_0, FAN_LEVEL_MAX);
		return -EINVAL;
 	}

	if(!chip->fan->enable){
		chip->fan->fan_level_bak = level;
		FAN_DBG("Fan is disabled, just run this level:%d after fan enabled.", level);
		return count;
	}
	mutex_lock(&chip->lock);
	ret = fan_level_set(chip->fan, level);
	if(ret != 0){
		mutex_unlock(&chip->lock);
		return ret;
 	}
	mutex_unlock(&chip->lock);
	FAN_DBG("set fan_level=%d rc=%d.", level, ret);

	return count;
}
static ssize_t fan_pwm_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
    unsigned int pwm;

 	sscanf(buf, "%d", &pwm);

	if(pwm < 0 || pwm > 100){
		FAN_DBG("pwm=%d,it is a bad pwm,not in(%d-%d).\n",pwm,0, 100);
		return -EINVAL;
 	}

	mutex_lock(&chip->lock);
	ret = fan_pwm_set(chip->fan, pwm);
	if(ret != 0){
		mutex_unlock(&chip->lock);
		return ret;
 	}
	mutex_unlock(&chip->lock);
	FAN_DBG("set fan_pwm=%d rc=%d.", pwm, ret);

	return count;
}
static int nb_fan_rpm_check(u32 rpm)
{
	static int rst_cnt = 0;
	unsigned int level = 0;

	if(chip->fan->level==FAN_LEVEL_0 || rpm>chip->fan->level*1000){
		rst_cnt = 0;
		return 0;
	}
	if(rst_cnt > 10){
		return 0;
	}

	level = chip->fan->level;
	fan_level_set(chip->fan, FAN_LEVEL_0);
	nb_fan_power_set(chip->fan, false);
	msleep(50);
	nb_fan_power_set(chip->fan, true);
	fan_level_set(chip->fan, level);
	
	FAN_DBG("fan level=%d rpm=%d error! NB.%d reset fan !", chip->fan->level, rpm, rst_cnt);
	
	if(rst_cnt == 10){
		FAN_DBG("fan level=%d rpm=%d error!rest fan is invalid! stop to try reset!", chip->fan->level, rpm);
	}
	rst_cnt++;
	
	return 0;
}
static ssize_t fan_speed_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	int len = 0;
	u32 rpm = 0;
 
	mutex_lock(&chip->lock);
	if(chip->fan->level != FAN_LEVEL_0){
	    ret = fan_rpm_get(chip->fan, &rpm);
	    nb_fan_rpm_check(rpm);
	}
	len = sprintf(buf, "%d\n", rpm);
	mutex_unlock(&chip->lock);
 
	FAN_DBG("fan_level=%d fan_speed=%d\n",chip->fan->level, rpm);
	return len;
 }
static ssize_t led_id_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", chip->led_id);
}

static ssize_t led_id_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc;
	int led_id;

	rc = kstrtoint(buf, 10, &led_id);
	if (rc < 0)
		return rc;

	if(led_id>chip->num_leds || chip->num_leds<=0){
		return -ENODEV;
	}
	mutex_lock(&chip->lock);
	chip->led_id = led_id;
	mutex_unlock(&chip->lock);

	return (rc < 0) ? rc : count;
}
static ssize_t led_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->num_leds<=0){
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", chip->leds[chip->led_id].enable);
}
static ssize_t led_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc;
	bool enable;

	if(chip->num_leds<=0){
		return -ENODEV;
	}

	rc = kstrtobool(buf, &enable);
	if (rc < 0)
		return rc;

	mutex_lock(&chip->lock);
	chip->leds_bak[chip->led_id].enable = enable;
	rc = led_stat_set(chip->leds_bak[chip->led_id]);
	if(rc >= 0){
		chip->leds[chip->led_id].enable = chip->leds_bak[chip->led_id].enable;
		chip->leds[chip->led_id].on_ms = chip->leds_bak[chip->led_id].on_ms;
		chip->leds[chip->led_id].off_ms = chip->leds_bak[chip->led_id].off_ms;
		chip->leds[chip->led_id].brightness = chip->leds_bak[chip->led_id].brightness;
	}else{
		chip->leds_bak[chip->led_id].enable = 0;
		chip->leds_bak[chip->led_id].on_ms = 0;
		chip->leds_bak[chip->led_id].off_ms = 0;
		chip->leds_bak[chip->led_id].brightness = 0;
	}
	mutex_unlock(&chip->lock);

	return rc<0?rc:count;
}
static ssize_t led_on_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->num_leds<=0){
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", chip->leds[chip->led_id].on_ms);
}

static ssize_t led_on_ms_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	int on_ms;

	if(chip->num_leds<=0){
		return -ENODEV;
	}

	rc = kstrtoint(buf, 10, &on_ms);
	if (rc < 0)
		return rc;

	mutex_lock(&chip->lock);
	chip->leds_bak[chip->led_id].on_ms = on_ms;
	mutex_unlock(&chip->lock);

	return count;
}
static ssize_t led_off_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->num_leds<=0){
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", chip->leds[chip->led_id].off_ms);
}

static ssize_t led_off_ms_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	int off_ms;

	if(chip->num_leds<=0){
		return -ENODEV;
	}

	rc = kstrtoint(buf, 10, &off_ms);
	if (rc < 0)
		return rc;

	mutex_lock(&chip->lock);
	chip->leds_bak[chip->led_id].off_ms = off_ms;
	mutex_unlock(&chip->lock);

	return count;
}
static ssize_t led_fade_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->num_leds<=0){
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", chip->leds[chip->led_id].fade_ms);
}

static ssize_t led_fade_ms_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	int fade_ms;

	if(chip->num_leds<=0){
		return -ENODEV;
	}

	rc = kstrtoint(buf, 10, &fade_ms);
	if (rc < 0)
		return rc;

	mutex_lock(&chip->lock);
	chip->leds_bak[chip->led_id].fade_ms = fade_ms;
	mutex_unlock(&chip->lock);

	return count;
}
static ssize_t led_brightness_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if(chip->num_leds<=0){
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", chip->leds[chip->led_id].brightness);
}

static ssize_t led_brightness_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	int brightness;

	if(chip->num_leds<=0){
		return -ENODEV;
	}

	rc = kstrtoint(buf, 10, &brightness);
	if (rc < 0)
		return rc;

	mutex_lock(&chip->lock);
	chip->leds_bak[chip->led_id].brightness = brightness;
	mutex_unlock(&chip->lock);

	return count;
}

static  struct kobj_attribute fan_enable_attr = __ATTR(fan_enable, 0664, fan_enable_show, fan_enable_store);
static  struct kobj_attribute fan_speed_level_attr = __ATTR(fan_speed_level, 0664, fan_level_show, fan_level_store);
static  struct kobj_attribute fan_speed_pwm_attr = __ATTR(fan_speed_pwm, 0664, NULL, fan_pwm_store);
static  struct kobj_attribute fan_speed_count_attr = __ATTR(fan_speed_count, 0664, fan_speed_show, NULL);
static  struct kobj_attribute led_enable_attr = __ATTR(led_enable, 0664, led_enable_show, led_enable_store);
static  struct kobj_attribute led_id_attr = __ATTR(led_id, 0664, led_id_show, led_id_store);
static  struct kobj_attribute led_on_ms_attr = __ATTR(led_on_ms, 0664, led_on_ms_show, led_on_ms_store);
static  struct kobj_attribute led_off_ms_attr = __ATTR(led_off_ms, 0664, led_off_ms_show, led_off_ms_store);
static  struct kobj_attribute led_fade_ms_attr = __ATTR(led_fade_ms, 0664, led_fade_ms_show, led_fade_ms_store);
static  struct kobj_attribute led_brightness_attr = __ATTR(led_brightness, 0664, led_brightness_show, led_brightness_store);

static struct attribute *chip_attrs[] = {
	&fan_enable_attr.attr,
    &fan_speed_level_attr.attr,
	&fan_speed_pwm_attr.attr,
	&fan_speed_count_attr.attr,
	&led_enable_attr.attr,
	&led_id_attr.attr,
	&led_on_ms_attr.attr,
	&led_off_ms_attr.attr,
	&led_fade_ms_attr.attr,
	&led_brightness_attr.attr,
   NULL,
};

static struct attribute_group chip_attr_group = {
    .attrs = chip_attrs,
};

static int nb_fan_parse_dt(struct nb_chip *chip)
{
	struct device_node *node = chip->dev->of_node, *child_node;
	struct led_dev *led;
	struct pwm_device *pwm_dev;
	struct pwm_state state;
	const char *label = NULL;
	int rc = 0, i = 0;

	rc = of_get_available_child_count(node);
	if (rc <= 0) {
		FAN_DBG("No led child node defined\n");
		return -ENODEV;
	}

	chip->num_leds = rc - 1;
	if(chip->num_leds >= 1){
		chip->leds = devm_kcalloc(chip->dev, chip->num_leds, sizeof(struct led_dev), GFP_KERNEL);
		if (!chip->leds)
			return -ENOMEM;
		
		chip->leds_bak = devm_kcalloc(chip->dev, chip->num_leds, sizeof(struct led_dev), GFP_KERNEL);
		if (!chip->leds)
			return -ENOMEM;
		
		memset(chip->leds, 0, sizeof(struct led_dev)*chip->num_leds);
		memset(chip->leds_bak, 0, sizeof(struct led_dev)*chip->num_leds);
	}

	chip->fan = devm_kcalloc(chip->dev, 1, sizeof(struct fan_dev), GFP_KERNEL);
	if (!chip->fan)
		return -ENOMEM;

	memset(chip->fan, 0, sizeof(struct fan_dev));

	for_each_available_child_of_node(node, child_node) {
		
		label = of_get_property(child_node, "label", NULL) ? :child_node->name;
		if(label == NULL){
			FAN_DBG("Get node label failed!");
			return -ENODEV;
		}

		pwm_dev = devm_fwnode_pwm_get(chip->dev, of_fwnode_handle(child_node), NULL);
		if (IS_ERR(pwm_dev)) {
			rc = PTR_ERR(pwm_dev);
			if (rc != -EPROBE_DEFER)
				FAN_DBG("Get pwm device for %s failed, rc=%d\n", label, rc);
			return rc;
		}

		if(!strncmp(label, LABEL_LED, strlen(LABEL_LED)) && chip->leds){
			led = &chip->leds[i++];
			led->label = label;
			led->pwm_dev = pwm_dev;
			FAN_DBG("Find %s node with pwm_dev=%08x from dt!", label, pwm_dev);
		}
		if(!strncmp(label, LABEL_FAN, strlen(LABEL_FAN)) && chip->fan){
			chip->fan->label = label;
			chip->fan->pwm_dev = pwm_dev;
			pwm_init_state(chip->fan->pwm_dev, &state);
			state.enabled = false;
			rc = pwm_apply_state(chip->fan->pwm_dev, &state);
			if (rc) {
				FAN_DBG("Find %s node but init failed!", label);
				return rc;
			}
			FAN_DBG("Find %s node and init pwm to off!", label);
		}
	}

	if(chip->leds && chip->leds_bak)
		memcpy(chip->leds_bak, chip->leds, sizeof(struct led_dev)*chip->num_leds);

	return rc;
}

/*
static int nb_fan_power_proc(struct nb_chip *chip)
{
	int ret = 0;
	u32 voltage = 0;

	chip->avdd_ldo = regulator_get(chip->dev, "fan,avdd");
	if (IS_ERR(chip->avdd_ldo)) {
		FAN_DBG("Failed to get power regulator\n");
		ret = PTR_ERR(chip->avdd_ldo);
		return ret;
	}

	ret = of_property_read_u32(chip->dev->of_node, "fan,voltage", &voltage);
	if(ret){
		FAN_DBG("Regulator vdd_fan get vtg failed rc=%d from dt, use default vol.\n", ret);
		//return ret;
		voltage = FAN_DEF_VOL;
	}
	
	ret = regulator_set_voltage(chip->avdd_ldo, voltage, voltage);
	if (ret) {
		FAN_DBG("Regulator vdd_fan set vtg failed rc=%d\n", ret);
		return ret;
	}
	FAN_DBG("Regulator vdd_fan set vtg=%d success.\n", voltage);

	ret = regulator_is_enabled(chip->avdd_ldo);
	if(ret){
		FAN_DBG("Regulator vdd_fan is enabled, maybe have a wrong set for the power!\n");
	}else{
		FAN_DBG("Regulator vdd_fan is disabled!\n");
	}
	ret = regulator_enable(chip->avdd_ldo);
	if (ret) {
		FAN_DBG("fan Regulator enable failed rc=%d\n", ret);
		return ret;
	}
	return ret;
}
*/
static int nb_fan_power_proc(struct nb_chip *chip)
{
	return 0;
}

static int nb_fan_pinctrl_proc(struct nb_chip *chip)
{
    int rc;
    struct pinctrl *pinctrl;

    pinctrl = devm_pinctrl_get(chip->dev);
     if (IS_ERR_OR_NULL(pinctrl)) {
        FAN_DBG("Failed to get pin ctrl\n");
        return PTR_ERR(pinctrl);
    }else{
		FAN_DBG("get pinctrl success");
	}

    chip->pins_active = pinctrl_lookup_state(pinctrl, "default");
    if (IS_ERR_OR_NULL(chip->pins_active)) {
        FAN_DBG("Failed to lookup pinctrl default state\n");
        return PTR_ERR(chip->pins_active);
    }else{
		FAN_DBG("lookup default pinctrl success");
	}

    rc = pinctrl_select_state(pinctrl, chip->pins_active);

    return rc;
}

static int nb_fan_gpio_proc(struct nb_chip *chip)
{
	int ret = 0;
	struct device_node *np = chip->dev->of_node;
	
	if(!chip->fan)
		return -ENODEV;

	chip->fan->speed_gpio =of_get_named_gpio(np,"fan,speed-gpio", 0);
	if(chip->fan->speed_gpio < 0){
		FAN_DBG("Find speed_gpio failed!\n");
		return chip->fan->speed_gpio;
	}
	ret = gpio_request(chip->fan->speed_gpio, "fan_speed_gpio");
	if (ret) {
		FAN_DBG("Find speed_gpio=%d requst failed.\n", chip->fan->speed_gpio);
		return ret;
	}
	chip->fan->speed_irq = gpio_to_irq(chip->fan->speed_gpio);
	ret = request_irq(chip->fan->speed_irq, fan_speed_irq_proc, IRQF_TRIGGER_FALLING, "fan_speed_int", chip);	//IRQF_TRIGGER_FALLING
	if (ret < 0) {
		pr_err("failed to register speed IRQ handler\n");
		return ret;
	}
	disable_irq(chip->fan->speed_irq);

	chip->fan->pwm_gpio =of_get_named_gpio(np,"fan,pwm-gpio", 0);
	if(chip->fan->pwm_gpio < 0){
		FAN_DBG("Find pwm_gpio failed!\n");
		return chip->fan->pwm_gpio;
	}
	FAN_DBG("pwm gpio status is:%d.", gpio_get_value(chip->fan->pwm_gpio));
	
	chip->fan->en_gpio =of_get_named_gpio(np, "fan,en-gpio", 0);
	if(chip->fan->en_gpio < 0){
		FAN_DBG("Find en_gpio failed!\n");
		return chip->fan->en_gpio;
	}
	ret = gpio_request(chip->fan->en_gpio, "fan_en_gpio");
	if (ret) {
		FAN_DBG("Find en_gpio=%d requst failed.\n", chip->fan->en_gpio);
		return ret;
	}
	FAN_DBG("fan power enable gpio status is:%d.", gpio_get_value(chip->fan->en_gpio));
	gpio_direction_output(chip->fan->en_gpio, 0);
	FAN_DBG("fan power enable gpio init status is:%d.", gpio_get_value(chip->fan->en_gpio));

	return 0;
}

static int nb_fan_probe(struct platform_device *pdev)
{
	int rc = 0;

	FAN_DBG("Chip(led and fan)probe start!\n");
	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	FAN_DBG("dev=%08x!\n", chip->dev);

	rc = nb_fan_power_proc(chip);
	if(rc){
		FAN_DBG("fan_power_proc failed.ret=%d\n", rc);
		return rc;
	}else{
		FAN_DBG("fan_power_proc success.\n");
	}

	rc = nb_fan_pinctrl_proc(chip);
	if(rc){
		FAN_DBG("nb_fan_pinctrl_proc failed.ret=%d\n", rc);
		return rc;
	}else{
		FAN_DBG("nb_fan_pinctrl_proc success.\n");
	}

	rc = nb_fan_parse_dt(chip);
	if (rc < 0) {
		FAN_DBG("Devicetree properties parsing failed, rc=%d\n", rc);
		return rc;
	}else{
		FAN_DBG("Devicetree properties parsing success.\n");
	}

	rc = nb_fan_gpio_proc(chip);
	if(rc){
		FAN_DBG("nb_fan_gpio_proc failed.ret=%d\n", rc);
		return rc;
	}else{
		FAN_DBG("nb_fan_gpio_proc success");
	}

	mutex_init(&chip->lock);
	hrtimer_init(&chip->fan->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->fan->timer.function = fan_hrtimer_hander;

	dev_set_drvdata(chip->dev, chip);
	chip->kobj = kobject_create_and_add("fan", kernel_kobj);
	if (!chip->kobj)
	{
		FAN_DBG("fan kobj create error\n");
		return -ENOMEM;
	}
	rc = sysfs_create_group(chip->kobj, &chip_attr_group);
	if(rc)
	{
		FAN_DBG("failed to create fan group attributes\n");
		return rc;
	}else{
		FAN_DBG("Create fan group attributes success.\n");
	}
	
	//enable_irq(chip->fan->speed_irq);

	FAN_DBG("Chip(led and fan)=%08x probe finished!\n", chip);

	return rc;
}

static int nb_fan_remove(struct platform_device *pdev)
{
	struct nb_chip *chip = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&chip->lock);
	sysfs_remove_group(chip->kobj, &chip_attr_group);
	dev_set_drvdata(chip->dev, NULL);

	return 0;
}

static const struct of_device_id nubia_fan_of_match[] = {
	{ .compatible = "soc,fan",},
	{ },
};

static struct platform_driver nubia_fan_driver = {
	.driver		= {
		.name		= "soc,fan",
		.of_match_table	= nubia_fan_of_match,
	},
	.probe		= nb_fan_probe,
	.remove		= nb_fan_remove,
};
module_platform_driver(nubia_fan_driver);

MODULE_DESCRIPTION("NUBIA FAN driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: pwm-soc-fan");
