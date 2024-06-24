// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Sony Mobile Communications Inc.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#ifdef CONFIG_ZTE_BOOT_CODE
#include <linux/qcom_scm.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif

#define PON_REV2			0x01

#define PON_SUBTYPE			0x05

#define PON_SUBTYPE_PRIMARY		0x01
#define PON_SUBTYPE_SECONDARY		0x02
#define PON_SUBTYPE_1REG		0x03
#define PON_SUBTYPE_GEN2_PRIMARY	0x04
#define PON_SUBTYPE_GEN2_SECONDARY	0x05
#define PON_SUBTYPE_GEN3_PBS		0x08
#define PON_SUBTYPE_GEN3_HLOS		0x09

#define PON_RT_STS			0x10
#define  PON_KPDPWR_N_SET		BIT(0)
#define  PON_RESIN_N_SET		BIT(1)
#define  PON_GEN3_RESIN_N_SET		BIT(6)
#define  PON_GEN3_KPDPWR_N_SET		BIT(7)

#define PON_PS_HOLD_RST_CTL		0x5a
#define PON_PS_HOLD_RST_CTL2		0x5b
#define  PON_PS_HOLD_ENABLE		BIT(7)
#define  PON_PS_HOLD_TYPE_MASK		0x0f
#define  PON_PS_HOLD_TYPE_WARM_RESET	1
#define  PON_PS_HOLD_TYPE_SHUTDOWN	4
#define  PON_PS_HOLD_TYPE_HARD_RESET	7

#define PON_PULL_CTL			0x70
#define  PON_KPDPWR_PULL_UP		BIT(1)
#define  PON_RESIN_PULL_UP		BIT(0)

#define PON_DBC_CTL			0x71
#define  PON_DBC_DELAY_MASK		0x7

#ifdef CONFIG_VOL_DOWN_RESIN
#define PON_PBS_RESIN_AND_KPDPWR_RESET_SW_CTL  0x4A
#else
#define PWRKEY_S2_RESET_PBS_OFFSET      0x42
#endif
#define PON_POWER_OFF_TYPE_WARM_RESET   0x1

struct pm8941_data {
	unsigned int	pull_up_bit;
	unsigned int	status_bit;
	bool		supports_ps_hold_poff_config;
	bool		supports_debounce_config;
	bool		has_pon_pbs;
	const char	*name;
	const char	*phys;
};

struct pm8941_pwrkey {
	struct device *dev;
	int irq;
	u32 baseaddr;
	u32 pon_pbs_baseaddr;
	struct regmap *regmap;
	struct input_dev *input;

	unsigned int revision;
	unsigned int subtype;
	struct notifier_block reboot_notifier;

	u32 code;
	u32 sw_debounce_time_us;
	ktime_t sw_debounce_end_time;
	bool last_status;
	const struct pm8941_data *data;
#ifdef CONFIG_ZTE_BOOT_CODE
	struct timer_list       timer;
	struct work_struct      pwrkey_poweroff_work;
	int                     vol_up_gpio;
	int                     vol_dn_gpio;
#endif
};

#ifdef CONFIG_ZTE_BOOT_CODE
extern int socinfo_get_ftm_flag(void);
extern int is_s2_warm_reset(void);

static bool vendor_volume_keys_pressed(struct pm8941_pwrkey *pon)
{
	int vol_up, vol_dn;
	struct device_node *gpio_key_node = NULL;
	struct device_node *child_node = NULL;
	bool ret = false;
	static bool key_gpio_initialized = false;

	if (!key_gpio_initialized) {
		gpio_key_node = of_find_compatible_node(NULL, NULL, "gpio-keys");
		if (gpio_key_node) {
			for_each_available_child_of_node(gpio_key_node, child_node) {
				if (!strcmp(child_node->name, "vol_up")) {
					pon->vol_up_gpio = of_get_named_gpio(child_node, "gpios", 0);
            #ifdef CONFIG_VOL_DOWN_RESIN
				}
            #else
				} else if (!strcmp(child_node->name, "vol_down")) {
					pon->vol_dn_gpio = of_get_named_gpio(child_node, "gpios", 0);
				}
            #endif
			}
		} else {
			dev_err(pon->dev, "unable to find DT node: gpio-keys\n");
		}
		key_gpio_initialized = true;
	}

	if (gpio_is_valid(pon->vol_up_gpio) && gpio_is_valid(pon->vol_dn_gpio)) {
		vol_up = !gpio_get_value(pon->vol_up_gpio);
		vol_dn = !gpio_get_value(pon->vol_dn_gpio);
		pr_debug("%s: vol_up(%d), vol_dn(%d)\n", __func__, vol_up, vol_dn);
		ret = vol_up && vol_dn;
	} else {
		dev_err(pon->dev, "%s: invalid gpio\n", __func__);
	}
	return ret;
}

static void vendor_mod_ponreg(struct pm8941_pwrkey *pon)
{
	int rc;
#ifdef CONFIG_VOL_DOWN_RESIN
	dev_info(pon->dev, "%s: nubia modify kpdpwr-resin s2 warm reset\n", __func__);
	if (pon->pon_pbs_baseaddr) {
		dev_err(pon->dev, "use pon pbs address=0x%04X\n", pon->pon_pbs_baseaddr);
		rc = regmap_write(pon->regmap, pon->pon_pbs_baseaddr + PON_PBS_RESIN_AND_KPDPWR_RESET_SW_CTL, PON_POWER_OFF_TYPE_WARM_RESET);
		if (rc)
			dev_err(pon->dev, "nubia kpdpwr-resin s2 register write failed, rc=%d\n", rc);
	}
#else
	dev_info(pon->dev, "%s: modify pwrkey s2 warm reset\n", __func__);
	if (pon->pon_pbs_baseaddr) {
		dev_err(pon->dev, "use pon pbs address=0x%04X\n", pon->pon_pbs_baseaddr);
		rc = regmap_write(pon->regmap, pon->pon_pbs_baseaddr + PWRKEY_S2_RESET_PBS_OFFSET, PON_POWER_OFF_TYPE_WARM_RESET);
		if (rc)
			dev_err(pon->dev, "pwrkey s2 register write failed, rc=%d\n", rc);
	}
#endif
}

static void pwrkey_poweroff(struct work_struct *work)
{
	struct pm8941_pwrkey *pon = container_of(work, struct pm8941_pwrkey, pwrkey_poweroff_work);
	//if ((vendor_volume_keys_pressed(pon)) && usb_cable_connected(pon)) {
	if (vendor_volume_keys_pressed(pon)) {
		dev_err(pon->dev, "%s: power key long pressed, trigger s2 warm reset\n", __func__);
		//qcom_scm_set_download_mode(QCOM_DOWNLOAD_FULLDUMP, 0);
		vendor_mod_ponreg(pon);
	} else {
		if (!is_s2_warm_reset()) {
			dev_err(pon->dev, "%s: power key long pressed, trigger kernel reboot\n", __func__);
			kernel_restart("LONGPRESS");
		}
	}
}

static void pwrkey_timer(struct timer_list *t)
{

	struct pm8941_pwrkey *pon = from_timer(pon, t, timer);

	schedule_work(&pon->pwrkey_poweroff_work);
}

void zte_set_timer(struct pm8941_pwrkey *pon)
{
	if (socinfo_get_ftm_flag()) {
		pon->timer.expires = jiffies + 3 * HZ;
		dev_info(pon->dev, "%s: FTM mode, reboot in 3 Secs\n", __func__);
	} else {
		#ifdef CONFIG_ZTE_PWRKEY_HARDRESET_TIMEOUT
			pon->timer.expires = jiffies + CONFIG_ZTE_PWRKEY_HARDRESET_TIMEOUT * HZ;
			dev_info(pon->dev, "%s: Normal mode, reboot in %d Secs\n", __func__,
				CONFIG_ZTE_PWRKEY_HARDRESET_TIMEOUT);
		#else
			pon->timer.expires = jiffies + 10 * HZ;
			dev_info(pon->dev, "%s: Normal mode, reboot in 10 Secs\n", __func__);
		#endif
	}
	mod_timer(&pon->timer, pon->timer.expires);
}
#endif

static int pm8941_reboot_notify(struct notifier_block *nb,
				unsigned long code, void *unused)
{
	struct pm8941_pwrkey *pwrkey = container_of(nb, struct pm8941_pwrkey,
						    reboot_notifier);
	unsigned int enable_reg;
	unsigned int reset_type;
	int error;

	/* PMICs with revision 0 have the enable bit in same register as ctrl */
	if (pwrkey->revision == 0)
		enable_reg = PON_PS_HOLD_RST_CTL;
	else
		enable_reg = PON_PS_HOLD_RST_CTL2;

	error = regmap_update_bits(pwrkey->regmap,
				   pwrkey->baseaddr + enable_reg,
				   PON_PS_HOLD_ENABLE,
				   0);
	if (error)
		dev_err(pwrkey->dev,
			"unable to clear ps hold reset enable: %d\n",
			error);

	/*
	 * Updates of PON_PS_HOLD_ENABLE requires 3 sleep cycles between
	 * writes.
	 */
	usleep_range(100, 1000);

	switch (code) {
	case SYS_HALT:
	case SYS_POWER_OFF:
		reset_type = PON_PS_HOLD_TYPE_SHUTDOWN;
		break;
	case SYS_RESTART:
	default:
		if (reboot_mode == REBOOT_WARM)
			reset_type = PON_PS_HOLD_TYPE_WARM_RESET;
		else
			reset_type = PON_PS_HOLD_TYPE_HARD_RESET;
		break;
	}

	error = regmap_update_bits(pwrkey->regmap,
				   pwrkey->baseaddr + PON_PS_HOLD_RST_CTL,
				   PON_PS_HOLD_TYPE_MASK,
				   reset_type);
	if (error)
		dev_err(pwrkey->dev, "unable to set ps hold reset type: %d\n",
			error);

	error = regmap_update_bits(pwrkey->regmap,
				   pwrkey->baseaddr + enable_reg,
				   PON_PS_HOLD_ENABLE,
				   PON_PS_HOLD_ENABLE);
	if (error)
		dev_err(pwrkey->dev, "unable to re-set enable: %d\n", error);

	return NOTIFY_DONE;
}

static irqreturn_t pm8941_pwrkey_irq(int irq, void *_data)
{
	struct pm8941_pwrkey *pwrkey = _data;
	unsigned int sts;
	int err;

	if (pwrkey->sw_debounce_time_us) {
		if (ktime_before(ktime_get(), pwrkey->sw_debounce_end_time)) {
			dev_dbg(pwrkey->dev,
				"ignoring key event received before debounce end %llu us\n",
				pwrkey->sw_debounce_end_time);
			return IRQ_HANDLED;
		}
	}

	err = regmap_read(pwrkey->regmap, pwrkey->baseaddr + PON_RT_STS, &sts);
	if (err)
		return IRQ_HANDLED;

	sts &= pwrkey->data->status_bit;

	if (pwrkey->sw_debounce_time_us && !sts)
		pwrkey->sw_debounce_end_time = ktime_add_us(ktime_get(),
						pwrkey->sw_debounce_time_us);

	/*
	 * Simulate a press event in case a release event occurred without a
	 * corresponding press event.
	 */
	if (!pwrkey->last_status && !sts) {
		input_report_key(pwrkey->input, pwrkey->code, 1);
		input_sync(pwrkey->input);
	}
	pwrkey->last_status = sts;

	input_report_key(pwrkey->input, pwrkey->code, sts);
	input_sync(pwrkey->input);

	if (!strcmp(pwrkey->input->name, "pmic_resin")) {
		dev_err(pwrkey->dev, "skip zte set timer part 2\n");
	} else {
#ifdef CONFIG_ZTE_BOOT_CODE
		if (sts)
			zte_set_timer(pwrkey);
		else
			del_timer(&pwrkey->timer);
#endif
	}

	return IRQ_HANDLED;
}

static int pm8941_pwrkey_sw_debounce_init(struct pm8941_pwrkey *pwrkey)
{
	unsigned int val, addr, mask;
	int error;

	if (pwrkey->data->has_pon_pbs && !pwrkey->pon_pbs_baseaddr) {
		dev_err(pwrkey->dev,
			"PON_PBS address missing, can't read HW debounce time\n");
		return 0;
	}

	if (pwrkey->pon_pbs_baseaddr)
		addr = pwrkey->pon_pbs_baseaddr + PON_DBC_CTL;
	else
		addr = pwrkey->baseaddr + PON_DBC_CTL;
	error = regmap_read(pwrkey->regmap, addr, &val);
	if (error)
		return error;

	if (pwrkey->subtype >= PON_SUBTYPE_GEN2_PRIMARY)
		mask = 0xf;
	else
		mask = 0x7;

	pwrkey->sw_debounce_time_us =
		2 * USEC_PER_SEC / (1 << (mask - (val & mask)));

	dev_dbg(pwrkey->dev, "SW debounce time = %u us\n",
		pwrkey->sw_debounce_time_us);

	return 0;
}

static int __maybe_unused pm8941_pwrkey_suspend(struct device *dev)
{
	struct pm8941_pwrkey *pwrkey = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(pwrkey->irq);

	return 0;
}

static int __maybe_unused pm8941_pwrkey_resume(struct device *dev)
{
	struct pm8941_pwrkey *pwrkey = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(pwrkey->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pm8941_pwr_key_pm_ops,
			 pm8941_pwrkey_suspend, pm8941_pwrkey_resume);

static int pm8941_pwrkey_probe(struct platform_device *pdev)
{
	struct pm8941_pwrkey *pwrkey;
	bool pull_up;
	struct device *parent;
	struct device_node *regmap_node;
	const __be32 *addr;
	u32 req_delay;
	int error;

	if (of_property_read_u32(pdev->dev.of_node, "debounce", &req_delay))
		req_delay = 15625;

	if (req_delay > 2000000 || req_delay == 0) {
		dev_err(&pdev->dev, "invalid debounce time: %u\n", req_delay);
		return -EINVAL;
	}

	pull_up = of_property_read_bool(pdev->dev.of_node, "bias-pull-up");

	pwrkey = devm_kzalloc(&pdev->dev, sizeof(*pwrkey), GFP_KERNEL);
	if (!pwrkey)
		return -ENOMEM;

	pwrkey->dev = &pdev->dev;
	pwrkey->data = of_device_get_match_data(&pdev->dev);
	if (!pwrkey->data) {
		dev_err(&pdev->dev, "match data not found\n");
		return -ENODEV;
	}

	parent = pdev->dev.parent;
	regmap_node = pdev->dev.of_node;
	pwrkey->regmap = dev_get_regmap(parent, NULL);
	if (!pwrkey->regmap) {
		regmap_node = parent->of_node;
		/*
		 * We failed to get regmap for parent. Let's see if we are
		 * a child of pon node and read regmap and reg from its
		 * parent.
		 */
		pwrkey->regmap = dev_get_regmap(parent->parent, NULL);
		if (!pwrkey->regmap) {
			dev_err(&pdev->dev, "failed to locate regmap\n");
			return -ENODEV;
		}
	}

	addr = of_get_address(regmap_node, 0, NULL, NULL);
	if (!addr) {
		dev_err(&pdev->dev, "reg property missing\n");
		return -EINVAL;
	}
	pwrkey->baseaddr = be32_to_cpup(addr);

	if (pwrkey->data->has_pon_pbs) {
		/* PON_PBS base address is optional */
		addr = of_get_address(regmap_node, 1, NULL, NULL);
		if (addr)
			pwrkey->pon_pbs_baseaddr = be32_to_cpup(addr);
	}

	pwrkey->irq = platform_get_irq(pdev, 0);
	if (pwrkey->irq < 0)
		return pwrkey->irq;

	error = regmap_read(pwrkey->regmap, pwrkey->baseaddr + PON_REV2,
			    &pwrkey->revision);
	if (error) {
		dev_err(&pdev->dev, "failed to read revision: %d\n", error);
		return error;
	}

	error = regmap_read(pwrkey->regmap, pwrkey->baseaddr + PON_SUBTYPE,
			    &pwrkey->subtype);
	if (error) {
		dev_err(&pdev->dev, "failed to read subtype: %d\n", error);
		return error;
	}

	error = of_property_read_u32(pdev->dev.of_node, "linux,code",
				     &pwrkey->code);
	if (error) {
		dev_dbg(&pdev->dev,
			"no linux,code assuming power (%d)\n", error);
		pwrkey->code = KEY_POWER;
	}

	pwrkey->input = devm_input_allocate_device(&pdev->dev);
	if (!pwrkey->input) {
		dev_dbg(&pdev->dev, "unable to allocate input device\n");
		return -ENOMEM;
	}

	input_set_capability(pwrkey->input, EV_KEY, pwrkey->code);

	pwrkey->input->name = pwrkey->data->name;
	pwrkey->input->phys = pwrkey->data->phys;

	if (pwrkey->data->supports_debounce_config) {
		req_delay = (req_delay << 6) / USEC_PER_SEC;
		req_delay = ilog2(req_delay);

		error = regmap_update_bits(pwrkey->regmap,
					   pwrkey->baseaddr + PON_DBC_CTL,
					   PON_DBC_DELAY_MASK,
					   req_delay);
		if (error) {
			dev_err(&pdev->dev, "failed to set debounce: %d\n",
				error);
			return error;
		}
	}

	error = pm8941_pwrkey_sw_debounce_init(pwrkey);
	if (error)
		return error;

	if (pwrkey->data->pull_up_bit) {
		error = regmap_update_bits(pwrkey->regmap,
					   pwrkey->baseaddr + PON_PULL_CTL,
					   pwrkey->data->pull_up_bit,
					   pull_up ? pwrkey->data->pull_up_bit :
						     0);
		if (error) {
			dev_err(&pdev->dev, "failed to set pull: %d\n", error);
			return error;
		}
	}

	if (!strcmp(pwrkey->input->name, "pmic_resin")) {
		dev_err(&pdev->dev, "skip zte set timer part 1\n");
	} else {
#ifdef CONFIG_ZTE_BOOT_CODE
	if (is_s2_warm_reset()) {
		dev_err(&pdev->dev, "%s: s2 warm reset is enabled by vendorcfg, power key long press to memory dump\n", __func__);
		vendor_mod_ponreg(pwrkey);
	}
	timer_setup(&pwrkey->timer, pwrkey_timer, 0);
	INIT_WORK(&pwrkey->pwrkey_poweroff_work, pwrkey_poweroff);
#endif
	}

	error = devm_request_threaded_irq(&pdev->dev, pwrkey->irq,
					  NULL, pm8941_pwrkey_irq,
					  IRQF_ONESHOT,
					  pwrkey->data->name, pwrkey);
	if (error) {
		dev_err(&pdev->dev, "failed requesting IRQ: %d\n", error);
		return error;
	}

	error = input_register_device(pwrkey->input);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device: %d\n",
			error);
		return error;
	}

	if (pwrkey->data->supports_ps_hold_poff_config) {
		pwrkey->reboot_notifier.notifier_call = pm8941_reboot_notify;
		error = register_reboot_notifier(&pwrkey->reboot_notifier);
		if (error) {
			dev_err(&pdev->dev, "failed to register reboot notifier: %d\n",
				error);
			return error;
		}
	}

	platform_set_drvdata(pdev, pwrkey);
	device_init_wakeup(&pdev->dev, 1);

	return 0;
}

static int pm8941_pwrkey_remove(struct platform_device *pdev)
{
	struct pm8941_pwrkey *pwrkey = platform_get_drvdata(pdev);

	if (pwrkey->data->supports_ps_hold_poff_config)
		unregister_reboot_notifier(&pwrkey->reboot_notifier);

	return 0;
}

static const struct pm8941_data pwrkey_data = {
	.pull_up_bit = PON_KPDPWR_PULL_UP,
	.status_bit = PON_KPDPWR_N_SET,
	.name = "pm8941_pwrkey",
	.phys = "pm8941_pwrkey/input0",
	.supports_ps_hold_poff_config = true,
	.supports_debounce_config = true,
	.has_pon_pbs = false,
};

static const struct pm8941_data resin_data = {
	.pull_up_bit = PON_RESIN_PULL_UP,
	.status_bit = PON_RESIN_N_SET,
	.name = "pm8941_resin",
	.phys = "pm8941_resin/input0",
	.supports_ps_hold_poff_config = true,
	.supports_debounce_config = true,
	.has_pon_pbs = false,
};

static const struct pm8941_data pon_gen3_pwrkey_data = {
	.status_bit = PON_GEN3_KPDPWR_N_SET,
	.name = "pmic_pwrkey",
	.phys = "pmic_pwrkey/input0",
	.supports_ps_hold_poff_config = false,
	.supports_debounce_config = false,
	.has_pon_pbs = true,
};

static const struct pm8941_data pon_gen3_resin_data = {
	.status_bit = PON_GEN3_RESIN_N_SET,
	.name = "pmic_resin",
	.phys = "pmic_resin/input0",
	.supports_ps_hold_poff_config = false,
	.supports_debounce_config = false,
	.has_pon_pbs = true,
};

static const struct of_device_id pm8941_pwr_key_id_table[] = {
	{ .compatible = "qcom,pm8941-pwrkey", .data = &pwrkey_data },
	{ .compatible = "qcom,pm8941-resin", .data = &resin_data },
	{ .compatible = "qcom,pmk8350-pwrkey", .data = &pon_gen3_pwrkey_data },
	{ .compatible = "qcom,pmk8350-resin", .data = &pon_gen3_resin_data },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8941_pwr_key_id_table);

static struct platform_driver pm8941_pwrkey_driver = {
	.probe = pm8941_pwrkey_probe,
	.remove = pm8941_pwrkey_remove,
	.driver = {
		.name = "pm8941-pwrkey",
		.pm = &pm8941_pwr_key_pm_ops,
		.of_match_table = of_match_ptr(pm8941_pwr_key_id_table),
	},
};
module_platform_driver(pm8941_pwrkey_driver);

MODULE_DESCRIPTION("PM8941 Power Key driver");
MODULE_LICENSE("GPL v2");
