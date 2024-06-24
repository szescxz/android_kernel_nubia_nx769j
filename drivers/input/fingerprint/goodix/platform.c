/*
 * platform indepent driver interface
 * Copyright (C) 2016 Goodix
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
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif


int zte_goodix_pinctrl_init(struct gf_dev *gf_dev)
{
	int retval;

	pr_info("%s: enter\n", __func__);
	gf_dev->fp_pinctrl = devm_pinctrl_get(&gf_dev->spi->dev);
	if (IS_ERR_OR_NULL(gf_dev->fp_pinctrl)) {
		dev_err(&gf_dev->spi->dev,
			"Target does not use pinctrl\n");
		retval = PTR_ERR(gf_dev->fp_pinctrl);
		gf_dev->fp_pinctrl = NULL;
		return retval;
	}

	gf_dev->gpio_state_active
		= pinctrl_lookup_state(gf_dev->fp_pinctrl, "goodix_active");
	if (IS_ERR_OR_NULL(gf_dev->gpio_state_active)) {
		dev_err(&gf_dev->spi->dev,
			"Can not get goodix_active pinstate\n");
		retval = PTR_ERR(gf_dev->gpio_state_active);
		gf_dev->fp_pinctrl = NULL;
		return retval;
	}

	gf_dev->gpio_state_suspend
		= pinctrl_lookup_state(gf_dev->fp_pinctrl, "goodix_suspend");
	if (IS_ERR_OR_NULL(gf_dev->gpio_state_suspend)) {
		dev_err(&gf_dev->spi->dev,
			"Can not get goodix_suspend pinstate\n");
		retval = PTR_ERR(gf_dev->gpio_state_suspend);
		gf_dev->fp_pinctrl = NULL;
		return retval;
	}

	gf_dev->pwr_state_active
		= pinctrl_lookup_state(gf_dev->fp_pinctrl, "pwr_active");
	if (IS_ERR_OR_NULL(gf_dev->pwr_state_active)) {
		dev_err(&gf_dev->spi->dev,
			"Can not get pwr_active pinstate\n");
		retval = PTR_ERR(gf_dev->pwr_state_active);
		gf_dev->fp_pinctrl = NULL;
		return retval;
	}

	return 0;
}

int zte_goodix_pinctrl_select(struct gf_dev *gf_dev, bool on)
{
	struct pinctrl_state *pins_state;
	int retval;

	pr_info("%s: enter, state=%d\n", __func__, on);
	pins_state = on ? gf_dev->gpio_state_active
		: gf_dev->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state)) {
		retval = pinctrl_select_state(gf_dev->fp_pinctrl, pins_state);
		if (retval) {
			dev_err(&gf_dev->spi->dev,
				"can not set %s pins\n",
				on ? "goodix_active" : "goodix_suspend");
			return retval;
		}
	} else {
		dev_err(&gf_dev->spi->dev,
			"not a valid '%s' pinstate\n",
				on ? "goodix_active" : "goodix_suspend");
		return -EINVAL;
	}

	pins_state = gf_dev->pwr_state_active;
	if (!IS_ERR_OR_NULL(pins_state)) {
		retval = pinctrl_select_state(gf_dev->fp_pinctrl, pins_state);
		if (retval) {
			dev_err(&gf_dev->spi->dev,
				"can not set %s pins\n",
				"pwr_state_active");
			return retval;
		}
	} else {
		dev_err(&gf_dev->spi->dev,
			"not a valid '%s' pinstate\n",
				"pwr_state_active");
		return -EINVAL;
	}

	return 0;
}

int gf_parse_dts(struct gf_dev *gf_dev)
{
	int rc = 0;
	struct device *dev = &gf_dev->spi->dev;
	struct device_node *np = dev->of_node;

	rc = zte_goodix_pinctrl_init(gf_dev);
	if (!rc && gf_dev->fp_pinctrl) {
		rc = zte_goodix_pinctrl_select(gf_dev, false);
		if (rc < 0) {
			pr_err("%s: Failed to select pin to suspend state.\n", __func__);
			goto err;
		}
	} else {
		pr_err("%s: failed to init pinctrl, rc=%d\n", __func__, rc);
		goto err;
	}
	pr_info("%s, set reset gpio suspend state\n", __func__);
	usleep_range(10000, 10100);

	gf_dev->pwr_gpio = of_get_named_gpio(np, "goodix,gpio_pwr", 0);
	if (gf_dev->pwr_gpio < 0) {
		pr_err("%s: falied to get pwr gpio!\n", __func__);
		rc = -1;
		goto err;
	}
	pr_info("gf_dev->pwr_gpio, gpio = %d\n", gf_dev->pwr_gpio);
	rc = devm_gpio_request(dev, gf_dev->pwr_gpio, "goodix_pwr");
	if (rc) {
		pr_err("%s: failed to request power gpio, rc = %d\n", __func__, rc);
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
		if (gf_dev->zlog_fp_client) {
			zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp pwr gpio\n");
			zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_PWR_GPIO_ERROR_NO);
		}
#endif
		goto err;
	}
	gpio_direction_output(gf_dev->pwr_gpio, 1);
	usleep_range(20000, 20100);
	if (gf_dev->fp_pinctrl) {
		rc = zte_goodix_pinctrl_select(gf_dev, true);
		if (rc < 0) {
			pr_err("%s: Failed to select pin to active state.\n", __func__);
			goto err_pwr;
		}
	} else {
		pr_err("%s: failed to init pinctrl, rc=%d\n", __func__, rc);
		goto err_pwr;
	}
	pr_info("%s, set reset and pwr gpio active state\n", __func__);

	gf_dev->reset_gpio = of_get_named_gpio(np, "fp-gpio-reset", 0);
	if (gf_dev->reset_gpio < 0) {
		pr_err("%s: falied to get reset gpio!\n", __func__);
		rc = -1;
		goto err_pwr;
	}
	pr_info("gf_dev->reset_gpio, gpio = %d\n", gf_dev->reset_gpio);
	rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		pr_err("%s: failed to request reset gpio, rc = %d\n", __func__, rc);
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
		if (gf_dev->zlog_fp_client) {
			zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp rst gpio\n");
			zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_RST_GPIO_ERROR_NO);
		}
#endif
		goto err_pwr;
	}
	//rc = gpio_direction_output(gf_dev->reset_gpio, 1);
	pr_info("gf_dev->reset_gpio, gpio = %d\n", gf_dev->reset_gpio);

	gf_dev->irq_gpio = of_get_named_gpio(np, "fp-gpio-irq", 0);
	if (gf_dev->irq_gpio < 0) {
		pr_err("%s: falied to get irq gpio!\n", __func__);
		rc = -1;
		goto err_reset;
	}
	pr_info("gf_dev->irq_gpio, gpio = %d\n", gf_dev->irq_gpio);
	rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		pr_err("%s: failed to request irq gpio, rc = %d\n", __func__, rc);
#ifdef CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM
		if (gf_dev->zlog_fp_client) {
			zlog_client_record(gf_dev->zlog_fp_client, "Failed to request goodixfp irq gpio\n");
			zlog_client_notify(gf_dev->zlog_fp_client,  ZLOG_FP_REQUEST_INT_GPIO_ERROR_NO);
		}
#endif
		goto err_reset;
	}
	gpio_direction_input(gf_dev->irq_gpio);

	pr_info("%s: end, rc=%d\n", __func__, rc);
	return rc;

err_reset:
	pr_err("%s: err_reset\n", __func__);
	//devm_gpio_free(dev, gf_dev->reset_gpio);
err_pwr:
	pr_err("%s: err_pwr\n", __func__);
	//devm_gpio_free(dev, gf_dev->pwr_gpio);
err:
	pr_err("%s: failed to parse dt, rc=%d\n", __func__, rc);
	return rc;
}

void gf_cleanup(struct gf_dev *gf_dev)
{
	pr_info("[info] %s\n", __func__);

	if (gpio_is_valid(gf_dev->irq_gpio)) {
		gpio_free(gf_dev->irq_gpio);
		pr_info("remove irq_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_free(gf_dev->reset_gpio);
		pr_info("remove reset_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->pwr_gpio)) {
		/*gpio_set_value(gf_dev->pwr_gpio, 0);*/
		gpio_free(gf_dev->pwr_gpio);
		pr_info("remove pwr_gpio success\n");
	}
}

int gf_power_on(struct gf_dev *gf_dev)
{
	int rc = 0;

	/* TODO: add your power control here */
	return rc;
}

int gf_power_off(struct gf_dev *gf_dev)
{
	int rc = 0;

	/* TODO: add your power control here */

	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	if (!gf_dev) {
		pr_err("Input buff is NULL.\n");
		return -ENODEV;
	}

	if (gpio_is_valid(gf_dev->reset_gpio)) {
		pr_info("do reset");
		/*gpio_direction_output(gf_dev->reset_gpio, 1);*/
		gpio_set_value(gf_dev->reset_gpio, 0);
		usleep_range(10000, 10100);
		gpio_set_value(gf_dev->reset_gpio, 1);
		mdelay(delay_ms);
	} else
		pr_err("reset_gpio is invalid");

	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if (!gf_dev) {
		pr_err("Input buff is NULL.\n");
		return -ENODEV;
	} else {
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}

