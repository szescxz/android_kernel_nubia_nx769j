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

 /*
 53  * Platform data for the rchip driver.
 54  */
 struct rchip_platform_data {
         struct platform_device *pdev;
	int device_reset;
	int power_1p8_en;
        int power_state;
	int fw_dload;
        int dload_state;
        int bd_gps_sw;
        int bd_gps_sw_state;
        int bd_wifi_sw;
        int bd_wifi_sw_state;
        int bd_lna_en;
        int lna_en_state;
};

static struct rchip_platform_data *rchip_pdata;
/*for rchip debug as soon as possibile, set it to 0 while driver debug finished*/
static int power_on_during_boot = 0;

void rchip_power_on(int on_or_off) 
{
    int on = on_or_off != 0 ? 1 : 0;
    pr_info("%s: power on or off ? %d\n", __func__, on);
        if (on) {
                gpio_direction_output(rchip_pdata->power_1p8_en, 1);
                gpio_direction_output(rchip_pdata->device_reset, 0);
		msleep(120);
                gpio_set_value_cansleep(rchip_pdata->device_reset, 1);
		rchip_pdata->power_state = 1;
                gpio_direction_output(rchip_pdata->bd_gps_sw, 1);
                rchip_pdata->bd_gps_sw_state = 1;
                gpio_direction_output(rchip_pdata->bd_wifi_sw, 1);
                rchip_pdata->bd_wifi_sw_state = 1;
                gpio_direction_output(rchip_pdata->bd_lna_en, 1);
                rchip_pdata->lna_en_state = 1;

	} else {
		gpio_direction_output(rchip_pdata->power_1p8_en, 0);
                gpio_direction_output(rchip_pdata->device_reset, 1);
                rchip_pdata->power_state = 0;
                gpio_direction_output(rchip_pdata->bd_gps_sw, 0);
                rchip_pdata->bd_gps_sw_state = 0;
                gpio_direction_output(rchip_pdata->bd_wifi_sw, 0);
                rchip_pdata->bd_wifi_sw_state = 0;
                gpio_direction_output(rchip_pdata->bd_lna_en, 0);
                rchip_pdata->lna_en_state = 0;
        }

}

static ssize_t fw_dload_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int dload = 0;

        if (sscanf(buff, "%u", &dload) !=1)
		return -EINVAL;

        if (dload) {
                gpio_direction_output(rchip_pdata->fw_dload, 1);
		rchip_pdata->dload_state = 1;
	} else {
                gpio_direction_output(rchip_pdata->fw_dload, 0);
		rchip_pdata->dload_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t fw_dload_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->dload_state);
}
static DEVICE_ATTR(fw_dload, S_IRUGO | S_IWUSR, fw_dload_show,  fw_dload_store);


static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int reset = 0;
        sscanf(buff, "%u", &reset);
        if (reset) {
		pr_info("rchip reset ...!\n");
                gpio_set_value_cansleep(rchip_pdata->device_reset, 0);
		msleep(120);
                gpio_set_value_cansleep(rchip_pdata->device_reset, 1);

	} else {
		pr_info("nothing to be done!\n");
        }
	return strnlen(buff, size);
}


static DEVICE_ATTR(reset, S_IRUGO | S_IWUSR, NULL, reset_store);


static ssize_t bd_gps_sw_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int sw = 0;

        if (sscanf(buff, "%u", &sw) !=1)
		return -EINVAL;

        if (sw) {
                pr_info("switch to beidou anti ...!\n");
                gpio_direction_output(rchip_pdata->bd_gps_sw, 1);
		rchip_pdata->bd_gps_sw_state = 1;
	} else {
                gpio_direction_output(rchip_pdata->bd_gps_sw, 0);
		rchip_pdata->bd_gps_sw_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t bd_gps_sw_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->bd_gps_sw_state);
}
static DEVICE_ATTR(bd_gps_sw, S_IRUGO | S_IWUSR, bd_gps_sw_show,  bd_gps_sw_store);


static ssize_t bd_wifi_sw_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int sw = 0;

        if (sscanf(buff, "%u", &sw) !=1)
		return -EINVAL;

        if (sw) {
                pr_info("switch to beidou anti ...!\n");
                gpio_direction_output(rchip_pdata->bd_wifi_sw, 1);
		rchip_pdata->bd_wifi_sw_state = 1;
	} else {
                gpio_direction_output(rchip_pdata->bd_wifi_sw, 0);
		rchip_pdata->bd_wifi_sw_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t bd_wifi_sw_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->bd_wifi_sw_state);
}
static DEVICE_ATTR(bd_wifi_sw, S_IRUGO | S_IWUSR, bd_wifi_sw_show,  bd_wifi_sw_store);


static ssize_t bd_lna_en_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int enable = 0;

        if (sscanf(buff, "%u", &enable) !=1)
		return -EINVAL;

        if (enable) {
                gpio_direction_output(rchip_pdata->bd_lna_en, 1);
		rchip_pdata->lna_en_state = 1;
	} else {
                gpio_direction_output(rchip_pdata->bd_lna_en, 0);
		rchip_pdata->lna_en_state = 0;
        }
	return strnlen(buff, size);
}

static ssize_t bd_lna_en_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->lna_en_state);
}
static DEVICE_ATTR(lna_en, S_IRUGO | S_IWUSR, bd_lna_en_show,  bd_lna_en_store);


static ssize_t power_on_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int on = 0;

        sscanf(buff, "%u", &on);
	rchip_power_on(on);
        rchip_pdata->power_state = on !=0 ? 1 : 0;
        return strnlen(buff, size);
}

static ssize_t power_on_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->power_state);
}

static DEVICE_ATTR(power_on, S_IRUGO | S_IWUSR, power_on_show, power_on_store);


static struct attribute  *rchip_attrs[] = {
        &dev_attr_power_on.attr,
        &dev_attr_reset.attr,
        &dev_attr_fw_dload.attr,
        &dev_attr_lna_en.attr,
        &dev_attr_bd_wifi_sw.attr,
        &dev_attr_bd_gps_sw.attr,
        NULL,
};

static struct attribute_group rchip_attr_grp = {
        .name = "zte-rchip",
        .attrs = rchip_attrs
};


static int rchip_populate_dt_pinfo(struct platform_device *pdev)
{
         pr_info("%s\n", __func__);
         if (!rchip_pdata) {
		pr_info("%s rchip_pdata is null \n", __func__);
                 return -ENOMEM;
	}
         if (pdev->dev.of_node) {
                pr_info("%s: dev.of_node exist!\n", __func__);
		rchip_pdata->device_reset =
                         of_get_named_gpio(pdev->dev.of_node,
						"reset-gpio", 0);
                pr_info("%s: device_reset = %d\n", __func__, rchip_pdata->device_reset);
		if (gpio_is_valid(rchip_pdata->device_reset)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->device_reset, "reset-gpio")) {
				pr_err("failed to request reset_gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->power_1p8_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "power_1p8_en", 0);
		pr_info("%s: power_1p8_en = %d\n", __func__, rchip_pdata->power_1p8_en);
		if (gpio_is_valid(rchip_pdata->power_1p8_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->power_1p8_en, "power_1p8_en")) {
				pr_err("failed to request power_1p8_en gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->fw_dload =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "fw_dload", 0);
                pr_info("%s: fw_dload  = %d\n", __func__, rchip_pdata->fw_dload);
		if (gpio_is_valid(rchip_pdata->fw_dload)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->fw_dload, "fw_dload")) {
				pr_err("failed to request fw_dload gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->bd_gps_sw =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "bd_gps_sw", 0);
		pr_info("%s: bd_gps_sw = %d\n", __func__, rchip_pdata->bd_gps_sw);
		if (gpio_is_valid(rchip_pdata->bd_gps_sw)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->bd_gps_sw, "bd_gps_sw")) {
				pr_err("failed to request bd_gps_sw gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->bd_wifi_sw =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "bd_wifi_sw", 0);
		pr_info("%s: bd_wifi_sw = %d\n", __func__, rchip_pdata->bd_wifi_sw);
		if (gpio_is_valid(rchip_pdata->bd_wifi_sw)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->bd_wifi_sw, "bd_wifi_sw")) {
				pr_err("failed to request bd_wifi_sw gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->bd_lna_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "bd_lna_en", 0);
		pr_info("%s: bd_lna_en = %d\n", __func__, rchip_pdata->bd_lna_en);
		if (gpio_is_valid(rchip_pdata->bd_lna_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->bd_lna_en, "bd_lna_en")) {
				pr_err("failed to request bd_lna_en gpio\n");
				return -EINVAL;
			}
		}

                pr_info("%s: success to request the rchip gpio\n", __func__);
	} else {
		pr_err("%s: dev.of_node is null\n",__func__);
		return -EINVAL;
	}

	return 0;
}


 static const struct of_device_id zte_rchip_of_match[] = {
        { .compatible = "zte-rchip", },
        { },
};

MODULE_DEVICE_TABLE(of, zte_rchip_of_match);

static int zte_rchip_probe(struct platform_device *pdev)
{
	int ret = 0;
        pr_info("%s into\n", __func__);

        rchip_pdata = kzalloc(sizeof(*rchip_pdata), GFP_KERNEL);
        if (!rchip_pdata) {
	        pr_info("%s rchip_pdata is null \n", __func__);
                 return -ENOMEM;
	}

        rchip_pdata->pdev = pdev;

	ret = rchip_populate_dt_pinfo(pdev);
        if(ret < 0) {
		pr_err("%s:failed to populate device tree info: ret = %d\n", __func__, ret);
		goto out;
        }

	pdev->dev.platform_data = rchip_pdata;

        ret = sysfs_create_group(&pdev->dev.kobj, &rchip_attr_grp);
	if (ret) {
		pr_err("%s:failed to create the rchip attr group；%d\n", __func__, ret);
                goto out;
	}

	/* for debug */
        if (power_on_during_boot) {
                pr_info("%s power on rchip device\n", __func__);
		rchip_power_on(1);
		rchip_pdata->power_state = 1;
	} else {
                pr_info("%s power off rchip device\n", __func__);
		rchip_power_on(0);
		rchip_pdata->power_state = 0;
	}

        pr_info("%s end\n", __func__);
        return 0;

out:
	kfree((void *)rchip_pdata);
	return ret;
}

static int  zte_rchip_remove(struct platform_device *pdev)
{
        return 0;
}


static struct platform_driver zte_rchip_driver = {
        .probe          = zte_rchip_probe,
        .remove         = zte_rchip_remove,
        .driver         = {
                .name   = "zte-rchip",
                .owner  = THIS_MODULE,
                .of_match_table = zte_rchip_of_match,
        }
};



static int __init zte_rchip_init (void)
{
	pr_info("%s: enter!!\n", __func__);
	return platform_driver_register(&zte_rchip_driver);
}

static void __exit zte_rchip_exit(void)
{
	pr_info("%s: enter!!\n", __func__);
	return platform_driver_unregister(&zte_rchip_driver);
}

late_initcall(zte_rchip_init);
module_exit(zte_rchip_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE light Inc.");
