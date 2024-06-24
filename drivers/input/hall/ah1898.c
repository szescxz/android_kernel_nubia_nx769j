/*
 * TEE driver for ZTE hall sensor
 * Copyright (C) 2021 ZTE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/regulator/consumer.h>
/*#define AH1898_IRQ 11*/

#define HALL_SENSOR_DOWN "hall_event_down=true"
#define HALL_SENSOR_UP "hall_event_up=true"
#ifndef ZTE_UNUSE_HALL
static int hall_status = 1;
#endif
struct pinctrl *hall_gpio_pinctrl = NULL;
struct pinctrl_state *hall_gpio_state = NULL;


#ifndef ZTE_UNUSE_HALL
static int set_hall_gpio_state(struct device *dev);

module_param(hall_status, int, 0644);
#endif
struct ah1898_chip {

	/*struct mutex lock;*/
	struct input_dev *input;
	struct work_struct work;
	struct regulator *vddio;
	struct platform_device *pdev;
	u32 min_uv;	/* device allow minimum voltage */
	u32 max_uv;	/* device allow max voltage */
	int irq;
	bool ah1898_enabled;
} *ah1898_chip_data;

static inline void report_uevent(struct ah1898_chip *ah1898_chip_data, char *str)
{
	char *envp[2];

	envp[0] = str;
	envp[1] = NULL;
	kobject_uevent_env(&(ah1898_chip_data->pdev->dev.kobj), KOBJ_CHANGE, envp);
}

#ifndef ZTE_UNUSE_HALL
static void ah1898_work_func(struct work_struct *work)
{

	int value;

	if (ah1898_chip_data->input == NULL || ah1898_chip_data == NULL) {
		pr_info("ah1898_work_fuc ERROR");
		return;
	}

	value = gpio_get_value(ah1898_chip_data->irq);

	pr_info("%s:hall test value=%d, enabled = %d\n", __func__, value, ah1898_chip_data->ah1898_enabled);

	if (ah1898_chip_data->ah1898_enabled == 1) {

		if (value == 1) {
			/*log for off*/
			pr_info("%s:hall ===switch is off!!the value = %d\n", __func__, value);
			/* delete KEY_POWER input report
			input_report_key(ah1898_chip_data->input, KEY_POWER, 1);
			input_sync(ah1898_chip_data->input);
			input_report_key(ah1898_chip_data->input, KEY_POWER, 0);
			*/
			report_uevent(ah1898_chip_data, HALL_SENSOR_UP);
			hall_status = 1;
		} else {
			/*log for on*/
			pr_info("%s:hall ===switch is on!!the value = %d\n", __func__, value);
			/* delete KEY_POWER input report
			input_report_key(ah1898_chip_data->input, KEY_POWER, 1);
			input_sync(ah1898_chip_data->input);
			input_report_key(ah1898_chip_data->input, KEY_POWER, 0);
			*/
			report_uevent(ah1898_chip_data, HALL_SENSOR_DOWN);
			hall_status = 0;
		}
	}
	/*enable_irq(ah1898_chip_data->irq);*/
}


static irqreturn_t ah1898_interrupt(int irq, void *dev_id)
{
	if (ah1898_chip_data == NULL) {
       /* printk("++++++++ah1898_interrupt\n ddata = %x", (unsigned long)ah1898_chip_data);*/
	return IRQ_NONE;
	}
	pr_info("%s:chenhui ah1898_interrupt!!\n", __func__);
	/*disable_irq_nosync(irq);*/

	schedule_work(&ah1898_chip_data->work);

	return IRQ_HANDLED;
}

static int ah1898_parse_dt(struct platform_device *pdev)
{
	u32 tempval;
	int rc;

	rc = of_property_read_u32(pdev->dev.of_node, "linux,max-uv", &tempval);
	if (rc) {
		dev_err(&pdev->dev, "unable to read max-uv\n");
		return -EINVAL;
	}

	ah1898_chip_data->max_uv = tempval;
	rc = of_property_read_u32(pdev->dev.of_node, "linux,min-uv", &tempval);
	if (rc) {
		dev_err(&pdev->dev, "unable to read min-uv\n");
		return -EINVAL;
	}
	ah1898_chip_data->min_uv = tempval;

	ah1898_chip_data->irq = of_get_named_gpio(pdev->dev.of_node, "ah,gpio_irq", 0);

	if (!gpio_is_valid(ah1898_chip_data->irq)) {
		pr_info("gpio irq pin %d is invalid.\n", ah1898_chip_data->irq);
		return -EINVAL;
	}

	return 0;
}

/* set hall gpio input and no pull*/
static int set_hall_gpio_state(struct device *dev)
{
	int error = 0;

	hall_gpio_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(hall_gpio_pinctrl)) {
		pr_info("Can not get hall_gpio_pinctrl\n");
		error = PTR_ERR(hall_gpio_pinctrl);
		return error;
	}
	hall_gpio_state = pinctrl_lookup_state(hall_gpio_pinctrl, "zte_hall_gpio_active");
	if (IS_ERR_OR_NULL(hall_gpio_state)) {
		pr_info("Can not get hall_gpio_state\n");
		error = PTR_ERR(hall_gpio_state);
		return error;
	}

	error = pinctrl_select_state(hall_gpio_pinctrl, hall_gpio_state);
	if (error) {
		pr_info("can not set hall_gpio pins to zte_hall_gpio_active states\n");
	} else {
		pr_info("set_hall_gpio_state success.\n");
	}
	return error;
}

static int ah1898_config_regulator(struct platform_device *dev, bool on)
{
	int rc = 0;

	if (on) {
		ah1898_chip_data->vddio = devm_regulator_get(&dev->dev, "vddio");
		if (IS_ERR(ah1898_chip_data->vddio)) {
			rc = PTR_ERR(ah1898_chip_data->vddio);
			dev_err(&dev->dev, "Regulator vddio get failed rc=%d\n",
					rc);
			ah1898_chip_data->vddio = NULL;
			return rc;
		}

		if (regulator_count_voltages(ah1898_chip_data->vddio) > 0) {
			rc = regulator_set_voltage(
					ah1898_chip_data->vddio,
					ah1898_chip_data->min_uv,
					ah1898_chip_data->max_uv);
			if (rc) {
				dev_err(&dev->dev, "Regulator vddio Set voltage failed rc=%d\n",
						rc);
				goto deinit_vregs;
			}
		}
		return rc;
	}

	goto deinit_vregs;


deinit_vregs:
	if (regulator_count_voltages(ah1898_chip_data->vddio) > 0)
		regulator_set_voltage(ah1898_chip_data->vddio, 0, ah1898_chip_data->max_uv);

	return rc;
}

static int ah1898_set_regulator(struct platform_device *dev, bool on)
{
	int rc = 0;

	if (on) {
		if (!IS_ERR_OR_NULL(ah1898_chip_data->vddio)) {
			rc = regulator_enable(ah1898_chip_data->vddio);
			if (rc) {
				dev_err(&dev->dev, "Enable regulator vddio failed rc=%d\n",
					rc);
				goto disable_regulator;
			}
		}
		return rc;
	}

	if (!IS_ERR_OR_NULL(ah1898_chip_data->vddio)) {
		rc = regulator_disable(ah1898_chip_data->vddio);
		if (rc)
			dev_err(&dev->dev, "Disable regulator vddio failed rc=%d\n",
				rc);
	}
	return 0;

disable_regulator:
	if (!IS_ERR_OR_NULL(ah1898_chip_data->vddio))
		regulator_disable(ah1898_chip_data->vddio);
	return rc;
}
#endif

static int  ah1898_probe(struct platform_device *pdev)
{
#ifdef ZTE_UNUSE_HALL
	pr_err("%s, NODEV return E\n", __func__);
	return -ENODEV;
#else
	int value_status;
	struct device *dev = &pdev->dev;

	int error = 0;
	int irq = 0;

	if (pdev->dev.of_node == NULL) {
		dev_info(&pdev->dev, "can not find device tree node\n");
		return -ENODEV;
	}
	pr_info("++++++++ah1898_probe\n");

	ah1898_chip_data = kzalloc(sizeof(struct ah1898_chip), GFP_KERNEL);

	ah1898_chip_data->input = input_allocate_device();

	ah1898_chip_data->pdev = pdev;
	if (!ah1898_chip_data || !ah1898_chip_data->input) {
		error = -ENOMEM;
		goto fail0;
	}

	/*input*/
	ah1898_chip_data->input->name = "ah1898";

	set_bit(EV_KEY, ah1898_chip_data->input->evbit);
	/*set_bit(KEY_POWER, ah1898_chip_data->input->keybit);*/


	error = input_register_device(ah1898_chip_data->input);
	if (error) {
		pr_err("ah1898: Unable to register input device, error: %d\n", error);
		goto fail2;
	}

	if (pdev->dev.of_node) {
		error = ah1898_parse_dt(pdev);
		if (error < 0) {
			dev_err(&pdev->dev, "Failed to parse device tree\n");
			goto exit;
		}
	} else {
		dev_err(&pdev->dev, "No valid platform data.\n");
		error = -ENODEV;
		goto exit;
	}

	if (ah1898_chip_data->irq) {

		irq = gpio_to_irq(ah1898_chip_data->irq);

		error = gpio_request(ah1898_chip_data->irq, "ah1898_irq");
		if (error) {
			pr_info("%s:ah1898 error3\n", __func__);
			goto fail1;
		}

		error = gpio_direction_input(ah1898_chip_data->irq);
		if (error) {
			pr_info("%s:ah1898 error3\n", __func__);
			goto fail1;
		}
	}

	error = set_hall_gpio_state(dev);
	if (error < 0) {
		pr_info("set_hall_gpio_state failed.\n");
	}

	if (irq) {

		INIT_WORK(&(ah1898_chip_data->work), ah1898_work_func);

		error = request_threaded_irq(irq, NULL, ah1898_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"ah1898_irq", NULL);
		if (error) {
			pr_err("gpio-ah1898: Unable to claim irq %d; error %d\n", irq, error);
			goto fail1;
		}

		enable_irq_wake(irq);
	}

	ah1898_chip_data->ah1898_enabled = 1;
	/*init hall status*/
	value_status = gpio_get_value(ah1898_chip_data->irq);
			pr_err("gpio-ah1898: irq %d;\n", value_status);
	if (value_status == 1) {
		hall_status = 1;
	} else {
		hall_status = 0;
	}

	error = ah1898_config_regulator(pdev, true);
	if (error < 0) {
		dev_err(&pdev->dev, "Configure power failed: %d\n", error);
		goto free_irq;
	}

	error = ah1898_set_regulator(pdev, true);
	if (error < 0) {
		dev_err(&pdev->dev, "power on failed: %d\n", error);
		goto err_regulator_init;
	}

	pr_info("hall Init hall_state=%d\n", hall_status);
	pr_info("%s:hall Init success!\n", __func__);

	return 0;

err_regulator_init:
	ah1898_config_regulator(pdev, false);

free_irq:
	disable_irq_wake(irq);

fail2:
	pr_err("gpio-ah1898 input_allocate_device fail\n");
	input_unregister_device(ah1898_chip_data->input);
	kfree(ah1898_chip_data);

fail1:
	pr_err("ah1898 gpio irq request fail\n");
	gpio_free(ah1898_chip_data->irq);

fail0:
	pr_err("gpio-ah1898 input_register_device fail\n");
	platform_set_drvdata(pdev, NULL);

exit:
	return error;
#endif
}

static int ah1898_remove(struct platform_device *pdev)
{
	gpio_free(ah1898_chip_data->irq);
	input_unregister_device(ah1898_chip_data->input);
	kfree(ah1898_chip_data);

	return 0;
}


static struct of_device_id ah1898_hall_of_match[] = {
	{.compatible = "ah,hall_ic", },
	{},
};


static struct platform_driver ah1898_hall_driver = {
	.probe = ah1898_probe,
	.remove = ah1898_remove,
	.driver = {
		.name = "hall_ic",
		.owner = THIS_MODULE,
		.of_match_table = ah1898_hall_of_match,
		/*.pm = &led_pm_ops,*/
	},

};


static int  ah1898_init(void)
{
	pr_info("++++++++ah1898_init\n");
	return platform_driver_register(&ah1898_hall_driver);
}


static void  ah1898_exit(void)
{
	platform_driver_unregister(&ah1898_hall_driver);
	pr_info("++++++++ah1898_exit\n");
}

module_init(ah1898_init);
module_exit(ah1898_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenhui");
MODULE_DESCRIPTION("hall sensor driver");
MODULE_ALIAS("platform:hall-sensor");
