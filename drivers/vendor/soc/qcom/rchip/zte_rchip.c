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
 53  * Platform data for the rchip driver.
 54  */
 struct rchip_platform_data {
         struct platform_device *pdev;
	int device_reset;
	int power_1p8_en;
	int power_1p2_en;
        int power_state;
	int fw_dload;
        int dload_state;
	int lna_ldo_en;
        int lna_state;
        int sim_3p3_en;
        int sim_ls_en;
        int lna_out_en;
        int sim_state;
        int boost_en;
        int boost_mode;
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
                gpio_direction_output(rchip_pdata->power_1p2_en, 1);
                gpio_direction_output(rchip_pdata->device_reset, 0);
		msleep(120);
                gpio_set_value_cansleep(rchip_pdata->device_reset, 1);
                gpio_direction_output(rchip_pdata->lna_ldo_en, 1);
                gpio_direction_output(rchip_pdata->sim_3p3_en, 1);
                gpio_direction_output(rchip_pdata->sim_ls_en, 1);
                gpio_direction_output(rchip_pdata->lna_out_en, 1);
		rchip_pdata->power_state = 1;

	} else {
		gpio_direction_output(rchip_pdata->power_1p8_en, 0);
		gpio_direction_output(rchip_pdata->power_1p2_en, 0);
                gpio_direction_output(rchip_pdata->device_reset, 1);
                gpio_direction_output(rchip_pdata->lna_ldo_en, 0);
                gpio_direction_output(rchip_pdata->sim_3p3_en, 0);
                gpio_direction_output(rchip_pdata->sim_ls_en, 0);
                gpio_direction_output(rchip_pdata->lna_out_en, 0);
                rchip_pdata->power_state = 0;
        }

}

void rchip_boost_enable(BOOST_MODE mode)
{
	int i;

	if (mode) {
		gpio_direction_output(rchip_pdata->boost_en, 0);
		gpio_set_value_cansleep(rchip_pdata->boost_en, 1);
		msleep(42);
	        for (i = 0; i <= mode; i++) {
			gpio_set_value_cansleep(rchip_pdata->boost_en, 0);
			msleep(1);
	                gpio_set_value_cansleep(rchip_pdata->boost_en, 1);
        	}
	} else {
		gpio_direction_output(rchip_pdata->boost_en, 0);
	}

        msleep(2);
        return;
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

static ssize_t trans_power_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int trans_on = 0;
        sscanf(buff, "%u", &trans_on);
        if (trans_on) {
                gpio_direction_output(rchip_pdata->lna_ldo_en, 1);
		rchip_pdata->lna_state = 1;
	} else {
                gpio_direction_output(rchip_pdata->lna_ldo_en, 0);
		rchip_pdata->lna_state = 0;
        }
	return strnlen(buff, size);
}


static ssize_t trans_power_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->lna_ldo_en);
}

static DEVICE_ATTR(trans_power, S_IRUGO | S_IWUSR, trans_power_show, trans_power_store);

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


static ssize_t sim_on_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int on = 0;

        sscanf(buff, "%u", &on);
        if (on) {
            gpio_direction_output(rchip_pdata->sim_3p3_en, 1);
            gpio_direction_output(rchip_pdata->sim_ls_en, 1);
        } else {
            gpio_direction_output(rchip_pdata->sim_3p3_en, 0);
            gpio_direction_output(rchip_pdata->sim_ls_en, 0);
        }
        rchip_pdata->sim_state = on !=0 ? 1 : 0;
        return strnlen(buff, size);
}

static ssize_t sim_on_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->sim_state);
}

static DEVICE_ATTR(sim_on, S_IRUGO | S_IWUSR, sim_on_show, sim_on_store);


static ssize_t lna_on_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int on = 0;

        sscanf(buff, "%u", &on);
        if (on) {
            gpio_direction_output(rchip_pdata->lna_ldo_en, 1);
            gpio_direction_output(rchip_pdata->lna_out_en, 1);
        } else {
            gpio_direction_output(rchip_pdata->lna_ldo_en, 0);
            gpio_direction_output(rchip_pdata->lna_out_en, 0);
        }
        rchip_pdata->lna_state = on !=0 ? 1 : 0;
        return strnlen(buff, size);
}

static ssize_t lna_on_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->sim_state);
}

static DEVICE_ATTR(lna_on, S_IRUGO | S_IWUSR, lna_on_show, lna_on_store);


static ssize_t boost_mode_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned int mode = 0;

        sscanf(buff, "%u", &mode);
	if ((mode > BOOST_MODE_MAX) || (mode < BOOST_MODE_DISABLED)) {
		pr_info("%s: invalid input paramer : %d\n", __func__, mode);
		return -EINVAL;
	}

        rchip_boost_enable((BOOST_MODE)mode);
        rchip_pdata->boost_mode = mode;
        return strnlen(buff, size);
}

static ssize_t boost_mode_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", rchip_pdata->boost_mode);
}

static DEVICE_ATTR(boost_mode, S_IRUGO | S_IWUSR, boost_mode_show, boost_mode_store);


static struct attribute  *rchip_attrs[] = {
        &dev_attr_power_on.attr,
        &dev_attr_reset.attr,
        &dev_attr_trans_power.attr,
        &dev_attr_fw_dload.attr,
        &dev_attr_sim_on.attr,
        &dev_attr_lna_on.attr,
        &dev_attr_boost_mode.attr,
        NULL,
};

static struct attribute_group rchip_attr_grp = {
        .name = "zte-rchip",
        .attrs = rchip_attrs
};


static int rchip_populate_dt_pinfo(struct platform_device *pdev)
{
         pr_info("%s\n", __func__);
         if (!rchip_pdata)
                 return -ENOMEM;

         if (pdev->dev.of_node) {
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
                                                 "volt_1p8_en", 0);
		pr_info("%s: power_1p8_en = %d\n", __func__, rchip_pdata->power_1p8_en);
		if (gpio_is_valid(rchip_pdata->power_1p8_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->power_1p8_en, "power_1p8_en")) {
				pr_err("failed to request power_1p8_en gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->power_1p2_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "volt_1p2_en", 0);
                pr_info("%s: power_1p2_en = %d\n", __func__, rchip_pdata->power_1p2_en);
		if (gpio_is_valid(rchip_pdata->power_1p2_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->power_1p2_en, "power_1p2_en")) {
				pr_err("failed to request power_1p2_en gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->sim_3p3_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "sim_3p3_en", 0);
                pr_info("%s: sim_3p3_en = %d\n", __func__, rchip_pdata->sim_3p3_en);
		if (gpio_is_valid(rchip_pdata->sim_3p3_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->sim_3p3_en, "sim_3p3_en")) {
				pr_err("failed to request sim_3p3_en gpio\n");
				return -EINVAL;
			}
		}


		rchip_pdata->sim_ls_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "sim_ls_en", 0);
                pr_info("%s: sim_ls_en = %d\n", __func__, rchip_pdata->sim_ls_en);
		if (gpio_is_valid(rchip_pdata->sim_ls_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->sim_ls_en, "sim_ls_en")) {
				pr_err("failed to request sim_ls_en gpio\n");
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

		rchip_pdata->lna_ldo_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "lna_ldo_en", 0);
                pr_info("%s: lna_ldo_en  = %d\n", __func__, rchip_pdata->lna_ldo_en);
		if (gpio_is_valid(rchip_pdata->lna_ldo_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->lna_ldo_en, "lna_ldo_en")) {
				pr_err("failed to request lna_ldo_en gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->lna_out_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "lna_out_en", 0);
                pr_info("%s: lna_out_en  = %d\n", __func__, rchip_pdata->lna_out_en);
		if (gpio_is_valid(rchip_pdata->lna_out_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->lna_out_en, "lna_out_en")) {
				pr_err("failed to request lns_out_en gpio\n");
				return -EINVAL;
			}
		}

		rchip_pdata->boost_en =
                         of_get_named_gpio(pdev->dev.of_node,
                                                 "boost_en", 0);
                pr_info("%s: boost_en  = %d\n", __func__, rchip_pdata->boost_en);
		if (gpio_is_valid(rchip_pdata->boost_en)) {
			if (devm_gpio_request(&pdev->dev, rchip_pdata->boost_en, "boost_en")) {
				pr_err("failed to request boost_en gpio\n");
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
        if (!rchip_pdata)
                 return -ENOMEM;

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
