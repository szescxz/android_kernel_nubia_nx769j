// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * writen by ZTE boot team, 20221209.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/device.h>


typedef enum _BOOST_MODE {
	VOLT_3P35 = 1,
	VOLT_3P45 = 2,
        VOLT_3P63 = 3,
        VOLT_3P85 = 4,
        VOLT_4P25 = 5,
	BOOST_MODE_DISABLED = 0,
	BOOST_MODE_01 = VOLT_3P35,
	BOOST_MODE_02 = VOLT_3P45,
	BOOST_MODE_03 = VOLT_3P63,
	BOOST_MODE_04 = VOLT_3P85,
	BOOST_MODE_05 = VOLT_4P25,
        BOOST_MODE_MAX = BOOST_MODE_05,
} BOOST_MODE;

 /*
  * Platform data for the tt driver.
  */
 struct tt_platform_data {
        struct platform_device *pdev;
	int tt_rst_n;
	int tt_rfvcc_en;
        int power_state;
	int tt2ap_wakeup;
        int dload_state;
        int tt_1p1_en;
        int tt_1p1_en_state;
        int ap2tt_wakeup;
        int ap2tt_wakeup_state;
        int tt_1p8_en;
        int tt_1p8_en_state;
        int tt_boost_en;
        int boost_mode;
};

static struct tt_platform_data *tt_pdata;
/*for tt debug as soon as possibile, set it to 0 while driver debug finished*/
static int power_on_during_boot = 1;

void tt_boost_enable(BOOST_MODE mode);

void tt_power_on(int on_or_off) 
{
    int on = on_or_off != 0 ? 1 : 0;

    pr_info("%s: power on or off ? %d\n", __func__, on);
    if (on) {
        gpio_direction_output(tt_pdata->tt_1p1_en, 1);
        usleep_range(50, 100);
        gpio_direction_output(tt_pdata->tt_1p8_en, 1);
        gpio_direction_output(tt_pdata->tt_rfvcc_en, 1);
        gpio_direction_output(tt_pdata->ap2tt_wakeup, 1);
        //gpio_direction_output(tt_pdata->tt_rst_n, 0);
	//msleep(120);
        //gpio_set_value_cansleep(tt_pdata->tt_rst_n, 1);
        tt_boost_enable(VOLT_4P25);
	tt_pdata->power_state = 1;
        tt_pdata->tt_1p1_en_state = 1;
        tt_pdata->tt_1p8_en_state = 1;
        tt_pdata->boost_mode = VOLT_4P25;

    } else {
	gpio_direction_output(tt_pdata->tt_rfvcc_en, 0);
        gpio_direction_output(tt_pdata->ap2tt_wakeup, 0);
        gpio_direction_output(tt_pdata->tt_1p8_en, 0);
        usleep_range(50, 100);
        gpio_direction_output(tt_pdata->tt_1p1_en, 0);
        tt_boost_enable(BOOST_MODE_DISABLED);
        tt_pdata->power_state = 0;
        tt_pdata->tt_1p1_en_state = 0;
        tt_pdata->tt_1p8_en_state = 0;
        tt_pdata->boost_mode = BOOST_MODE_DISABLED;
    }
}

void tt_boost_enable(BOOST_MODE mode)
{
	int i;

        gpio_direction_output(tt_pdata->tt_boost_en, 0);
	if (mode) {
		gpio_set_value_cansleep(tt_pdata->tt_boost_en, 1);
		msleep(60);
	        for (i = 0; i <= mode; i++) {
			gpio_set_value_cansleep(tt_pdata->tt_boost_en, 0);
			usleep_range(900, 1100);
	                gpio_set_value_cansleep(tt_pdata->tt_boost_en, 1);
                        usleep_range(900, 1100);
        	}

                gpio_set_value_cansleep(tt_pdata->tt_boost_en, 0);
                usleep_range(900, 1100);
                gpio_set_value_cansleep(tt_pdata->tt_boost_en, 1);
	} else {
                msleep(60);
		gpio_direction_output(tt_pdata->tt_boost_en, 0);
                usleep_range(900, 1100);
	        gpio_set_value_cansleep(tt_pdata->tt_boost_en, 1);
                usleep_range(900, 1100);
                gpio_direction_output(tt_pdata->tt_boost_en, 0);
	}

        tt_pdata->boost_mode = mode;
        msleep(2);
        return;
}

static ssize_t power_on_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int on = 0;

        sscanf(buff, "%u", &on);
	tt_power_on(on);
        tt_pdata->power_state = on !=0 ? 1 : 0;
        return strnlen(buff, size);
}

static ssize_t power_on_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", tt_pdata->power_state);
}

static DEVICE_ATTR(power_on, S_IRUGO | S_IWUSR, power_on_show, power_on_store);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int reset = 0;
        sscanf(buff, "%u", &reset);
        if (reset) {
		pr_info("tt reset ...!\n");
                gpio_set_value_cansleep(tt_pdata->tt_rst_n, 1);
		msleep(100);
                gpio_set_value_cansleep(tt_pdata->tt_rst_n, 0);
                msleep(100);
                gpio_set_value_cansleep(tt_pdata->tt_rst_n, 1);

	} else {
		pr_info("nothing to be done!\n");
        }
	return strnlen(buff, size);
}


static DEVICE_ATTR(reset, S_IWUSR, NULL, reset_store);


static ssize_t tt_1p1_en_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int en = 0;

        if (sscanf(buff, "%u", &en) !=1)
		return -EINVAL;

        if (en) {
                pr_info("switch to beidou anti ...!\n");
                gpio_direction_output(tt_pdata->tt_1p1_en, 1);
		tt_pdata->tt_1p1_en_state = 1;
	} else {
                gpio_direction_output(tt_pdata->tt_1p1_en, 0);
		tt_pdata->tt_1p1_en_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t tt_1p1_en_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", tt_pdata->tt_1p1_en_state);
}
static DEVICE_ATTR(tt_1p1_en, S_IRUGO | S_IWUSR, tt_1p1_en_show,  tt_1p1_en_store);


static ssize_t tt_1p8_en_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int enable = 0;

        if (sscanf(buff, "%u", &enable) !=1)
		return -EINVAL;

        if (enable) {
                gpio_direction_output(tt_pdata->tt_1p8_en, 1);
		tt_pdata->tt_1p8_en_state = 1;
	} else {
                gpio_direction_output(tt_pdata->tt_1p8_en, 0);
		tt_pdata->tt_1p8_en_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t tt_1p8_en_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", tt_pdata->tt_1p8_en_state);
}
static DEVICE_ATTR(tt_1p8_en, S_IRUGO | S_IWUSR, tt_1p8_en_show,  tt_1p8_en_store);

static ssize_t tt_rfvcc_en_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int enable = 0;

        if (sscanf(buff, "%u", &enable) !=1)
		return -EINVAL;

        if (enable) {
                gpio_direction_output(tt_pdata->tt_rfvcc_en, 1);
		tt_pdata->tt_rfvcc_en_state = 1;
	} else {
                gpio_direction_output(tt_pdata->tt_rfvcc_en, 0);
		tt_pdata->tt_rfvcc_en_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t tt_rfvcc_en_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", tt_pdata->tt_rfvcc_en_state);
}
static DEVICE_ATTR(tt_rfvcc_en, S_IRUGO | S_IWUSR, tt_1p8_en_show,  tt_rfvcc_en_store);


static ssize_t boost_mode_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int mode = 0;

        sscanf(buff, "%u", &mode);
	if ((mode > BOOST_MODE_MAX) || (mode < BOOST_MODE_DISABLED)) {
		pr_info("%s: invalid input paramer : %d\n", __func__, mode);
		return -EINVAL;
	}

        tt_boost_enable((BOOST_MODE)mode);
        return strnlen(buff, size);
}

static ssize_t boost_mode_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", tt_pdata->boost_mode);
}
static DEVICE_ATTR(boost_mode, S_IRUGO | S_IWUSR, boost_mode_show, boost_mode_store);

static ssize_t ap2tt_wakeup_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int en = 0;

        if (sscanf(buff, "%u", &en) !=1)
		return -EINVAL;

        if (en) {
                pr_info("ap wakeup tt ...!\n");
                gpio_direction_output(tt_pdata->ap2tt_wakeup, 0);
                msleep(5);
                gpio_direction_output(tt_pdata->ap2tt_wakeup, 1);
                msleep(5);
                gpio_direction_output(tt_pdata->ap2tt_wakeup, 0);
	} else {
                gpio_direction_output(tt_pdata->ap2tt_wakeup, 0);
        }
	return strnlen(buff, size);
}
static DEVICE_ATTR(ap2tt_wakeup, S_IWUSR, NULL, ap2tt_wakeup_store);

static struct attribute  *tt_attrs[] = {
        &dev_attr_power_on.attr,
        &dev_attr_reset.attr,
        &dev_attr_tt_1p1_en.attr,
        &dev_attr_tt_1p8_en.attr,
        &dev_attr_tt_rfvcc_en.attr,
        &dev_attr_boost_mode.attr,
        &dev_attr_ap2tt_wakeup.attr,
        NULL,
};

static struct attribute_group tt_attr_grp = {
        .name = "zte-tt",
        .attrs = tt_attrs
};


static int tt_populate_dt_pinfo(struct platform_device *pdev)
{
         pr_info("%s\n", __func__);
         if (!tt_pdata) {
		pr_info("%s tt_pdata is null \n", __func__);
                 return -ENOMEM;
	}
         if (pdev->dev.of_node) {
                pr_info("%s: dev.of_node exist!\n", __func__);
		tt_pdata->tt_rst_n =
                         of_get_named_gpio(pdev->dev.of_node,
						"tt_rst_n", 0);
                pr_info("%s: tt_rst_n = %d\n", __func__, tt_pdata->tt_rst_n);
		if (gpio_is_valid(tt_pdata->tt_rst_n)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->tt_rst_n, "tt_rst_n")) {
				pr_err("failed to request tt_rst_n\n");
				return -EINVAL;
			}
		}

		tt_pdata->tt_rfvcc_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "tt_rfvcc_en", 0);
		pr_info("%s: tt_rfvcc_en = %d\n", __func__, tt_pdata->tt_rfvcc_en);
		if (gpio_is_valid(tt_pdata->tt_rfvcc_en)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->tt_rfvcc_en, "tt_rfvcc_en")) {
				pr_err("failed to request tt_rfvcc_en gpio\n");
				return -EINVAL;
			}
		}

		tt_pdata->tt_1p1_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "tt_1p1_en", 0);
		pr_info("%s: tt_1p1_en = %d\n", __func__, tt_pdata->tt_1p1_en);
		if (gpio_is_valid(tt_pdata->tt_1p1_en)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->tt_1p1_en, "tt_1p1_en")) {
				pr_err("failed to request tt_1p1_en gpio\n");
				return -EINVAL;
			}
		}

		tt_pdata->tt_1p8_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "tt_1p8_en", 0);
		pr_info("%s: tt_1p8_en = %d\n", __func__, tt_pdata->tt_1p8_en);
		if (gpio_is_valid(tt_pdata->tt_1p8_en)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->tt_1p8_en, "tt_1p8_en")) {
				pr_err("failed to request tt_1p8_en gpio\n");
				return -EINVAL;
			}
		}


		tt_pdata->ap2tt_wakeup =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "ap2tt_wakeup", 0);
		pr_info("%s: ap2tt_wakeup = %d\n", __func__, tt_pdata->ap2tt_wakeup);
		if (gpio_is_valid(tt_pdata->ap2tt_wakeup)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->ap2tt_wakeup, "ap2tt_wakeup")) {
				pr_err("failed to request ap2tt_wakeup gpio\n");
				return -EINVAL;
			}
		}

		tt_pdata->tt2ap_wakeup =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "tt2ap_wakeup", 0);
                pr_info("%s: tt2ap_wakeup  = %d\n", __func__, tt_pdata->tt2ap_wakeup);
		if (gpio_is_valid(tt_pdata->tt2ap_wakeup)) {
			if (devm_gpio_request(&pdev->dev, tt_pdata->tt2ap_wakeup, "tt2ap_wakeup")) {
				pr_err("failed to request tt2ap_wakeup gpio\n");
				return -EINVAL;
			}
		}

                pr_info("%s: success to request the tt gpio\n", __func__);
	} else {
		pr_err("%s: dev.of_node is null\n",__func__);
		return -EINVAL;
	}

	return 0;
}


 static const struct of_device_id zte_tt_of_match[] = {
        { .compatible = "zte-tiantong", },
        { },
};

MODULE_DEVICE_TABLE(of, zte_tt_of_match);

static int zte_tt_probe(struct platform_device *pdev)
{
	int ret = 0;
        pr_info("%s into\n", __func__);

        tt_pdata = kzalloc(sizeof(*tt_pdata), GFP_KERNEL);
        if (!tt_pdata) {
	        pr_info("%s tt_pdata is null \n", __func__);
                 return -ENOMEM;
	}

        tt_pdata->pdev = pdev;

	ret = tt_populate_dt_pinfo(pdev);
        if(ret < 0) {
		pr_err("%s:failed to populate device tree info: ret = %d\n", __func__, ret);
		goto out;
        }

	pdev->dev.platform_data = tt_pdata;

        ret = sysfs_create_group(&pdev->dev.kobj, &tt_attr_grp);
	if (ret) {
		pr_err("%s:failed to create the tt attr group；%d\n", __func__, ret);
                goto out;
	}

	/* for debug */
        if (power_on_during_boot) {
                pr_info("%s power on tt device\n", __func__);
		tt_power_on(1);
		tt_pdata->power_state = 1;
	} else {
                pr_info("%s power off tt device\n", __func__);
		tt_power_on(0);
		tt_pdata->power_state = 0;
	}

        pr_info("%s end\n", __func__);
        return 0;

out:
	kfree((void *)tt_pdata);
	return ret;
}

static int  zte_tt_remove(struct platform_device *pdev)
{
        return 0;
}


static struct platform_driver zte_tt_driver = {
        .probe          = zte_tt_probe,
        .remove         = zte_tt_remove,
        .driver         = {
                .name   = "zte-tiantong",
                .owner  = THIS_MODULE,
                .of_match_table = zte_tt_of_match,
        }
};



static int __init zte_tt_init (void)
{
	pr_info("%s: enter!!\n", __func__);
	return platform_driver_register(&zte_tt_driver);
}

static void __exit zte_tt_exit(void)
{
	pr_info("%s: enter!!\n", __func__);
	return platform_driver_unregister(&zte_tt_driver);
}

late_initcall(zte_tt_init);
module_exit(zte_tt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE light Inc.");
