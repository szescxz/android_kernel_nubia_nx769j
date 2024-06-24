#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/version.h>
/*This program is used for dump sleep gpio stats.*/
#ifndef ZTE_GPIO_DEBUG
#define ZTE_GPIO_DEBUG
#endif

#ifdef ZTE_GPIO_DEBUG

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,106)
#define MODULE_PARAM_V2
#endif

extern int msm_dump_gpios(struct seq_file *s, int curr_len, char *gpio_buffer) __attribute__((weak));
extern int pmic_dump_pins(struct seq_file *s, int curr_len, char *gpio_buffer) __attribute__((weak));
#define BUFFER_SIZE 30000
static char *gpio_sleep_status_info;
static int gpio_debug_mask = 0;

#ifdef MODULE_PARAM_V2
static int gpio_debug_mask_set(const char *val, const struct kernel_param *kp)
#else
static int gpio_debug_mask_set(const char *val, struct kernel_param *kp)
#endif
{
	int rc;

	rc = param_set_int(val, kp);
	if (rc) {
		pr_err("%s: error setting value %d\n", __func__, rc);
		return rc;
	}

	if (!gpio_sleep_status_info) {
		pr_info("%s: malloc buffer for gpio debug\n", __func__);
		gpio_sleep_status_info = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	}
	if (!gpio_sleep_status_info) {
		pr_err("%s: malloc memory failed\n", __func__);
	}

	pr_info("gpio_debug_mask_set is %d\n", gpio_debug_mask);

	return 0;
}

#ifdef MODULE_PARAM_V2
static int gpio_debug_mask_get(char *val, const struct kernel_param *kp)
#else
static int gpio_debug_mask_get(char *val, struct kernel_param *kp)
#endif
{
	pr_info("gpio_debug_mask = %d\n", gpio_debug_mask);
	return snprintf(val, 0x3, "%d\n",  gpio_debug_mask);
}

module_param_call(gpio_debug_mask, gpio_debug_mask_set,
	gpio_debug_mask_get, &gpio_debug_mask, 0644);

int vendor_print_gpio_buffer(struct seq_file *s)
{
	if (gpio_sleep_status_info)
		seq_printf(s, gpio_sleep_status_info);
	else if (gpio_debug_mask) {
		seq_puts(s, "Not suspended yet!\n");
	} else{
		seq_puts(s, " please echo 1 > /sys/module/gpio_show_suspend/parameters/gpio_debug_mask\n");
	}
	return 0;
}
EXPORT_SYMBOL(vendor_print_gpio_buffer);

int vendor_free_gpio_buffer(void)
{
	kfree(gpio_sleep_status_info);
	gpio_sleep_status_info = NULL;

	return 0;
}
EXPORT_SYMBOL(vendor_free_gpio_buffer);

void zte_pm_vendor_before_powercollapse(void)
{
	int curr_len = 0;/*Default close*/

	do {
		if (gpio_debug_mask) {
			if (gpio_sleep_status_info) {
				memset(gpio_sleep_status_info, 0, sizeof(*gpio_sleep_status_info));
			} else {
				pr_info("%s: buffer for gpio debug not allocated\n", __func__);
				break;
			}

			/*how to debug:
			1> echo 1 > /sys/module/gpio_show_suspend/parameters/gpio_debug_mask
			2> let device sleep
			3> cat dump_sleep_gpios
			*/
			curr_len = msm_dump_gpios(NULL, curr_len, gpio_sleep_status_info);
			if (pmic_dump_pins)
				curr_len = pmic_dump_pins(NULL, curr_len, gpio_sleep_status_info);
		}
	} while (0);

}
EXPORT_SYMBOL(zte_pm_vendor_before_powercollapse);
#endif
