/*
 * writen by ZTE bsp light, 20220622.
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

static int zcard_det_gpio;

static int zcard_det_gpio_init( void )
{
	int retval = 0;
	struct device_node *node;

	node = of_find_node_with_property(NULL, "cd-gpios");
	if (node) {
		/*zcard_det_gpio*/
		retval = of_get_named_gpio(node, "cd-gpios", 0);
		if (retval < 0) {
			pr_err("%s: error invalid zcard_det_gpio err: %d\n", __func__, retval);
			return retval;
		}
		zcard_det_gpio = retval;
	} else {
		pr_err("%s: cannot get zte_card_holder node\n", __func__);
		return -ENODEV;
	}

	/*request zcard_det_gpio*/
	retval = gpio_request(zcard_det_gpio, NULL);
	if (retval) {
		pr_err("request zcard_det_gpio failed, retval=%d\n", retval);
	}

	return 0;
}

static ssize_t zcard_det_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	unsigned int zcard_det_gpio_value = 0;

	zcard_det_gpio_value = gpio_get_value(zcard_det_gpio);
	return sprintf(buff, "%d\n", zcard_det_gpio_value);
}
static struct kobj_attribute zcard_det_attr = __ATTR(zcard_det, S_IRUGO, zcard_det_show, NULL);

static int init_zcard_det_sys_node()
{
	int ret = 0;
	struct kobject *kobj = kobject_create_and_add("zcard_det", NULL);
	if(kobj == NULL){
		pr_err("%s: kobject_create_and_add  zcard_det failed!!\n", __func__);
		return -EINVAL;
	}

	ret = sysfs_create_file(kobj, &zcard_det_attr.attr);
	if(ret) {
		pr_err("%s: sysfs_create_file  zcard_det failed!!\n", __func__);
		return ret;
	}
	return ret;
}

static int __init zte_card_holder_det_init (void)
{
	int ret = 0;

	pr_info("%s: start!!\n", __func__);

	ret = zcard_det_gpio_init();
	if (ret)
		pr_info("%s: zcard_det_gpio_init failed!!\n", __func__);
	
	init_zcard_det_sys_node();

	pr_info("%s: finished!!\n", __func__);
	return ret;
}

static void __exit zte_card_holder_det_exit(void)
{
}

late_initcall(zte_card_holder_det_init);
module_exit(zte_card_holder_det_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE light Inc.");
