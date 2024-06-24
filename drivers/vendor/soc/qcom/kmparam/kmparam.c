// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#define ANDROID_BOOT_MODE_FTM          "ffbm-99"
#define ANDROID_BOOT_MODE_FFBM         "ffbm-02"
#define ANDROID_BOOT_MODE_RECOVERY	"recovery"
#define ANDROID_BOOT_MODE_CHARGER      "charger"

#define MAGIC_NUM_FFBM_MODE          0x6D6D5446 /*FFBM*/
#define MAGIC_NUM_NON_FFBM_MODE      0x4D54464E /*NFFBM*/

#define DEBUG_POLICY_USE_KERNEL_LOG_DRIVER                        0x00000001
#define DEBUG_POLICY_DISABLE_DM_VERITY                            0x00000002
#define DEBUG_POLICY_ENABLE_FASTBOOT                              0x00000004
#define DEBUG_POLICY_DISABLE_KERNEL_LOG_LIMIT                     0x00000008
#define DEBUG_POLICY_ENABLE_ADB_TRADEFED                          0x00000010

/*
 * Boot mode definition
 */
enum {
	ENUM_BOOT_MODE_NORMAL            = 0,
	ENUM_BOOT_MODE_FTM               = 1,
	ENUM_BOOT_MODE_RTC_ALARM         = 2,
	ENUM_BOOT_MODE_CHARGER           = 3,
	ENUM_BOOT_MODE_RECOVERY          = 4,
	ENUM_BOOT_MODE_FFBM              = 5,
	ENUM_BOOT_MODE_UNKNOWN,
	ENUM_BOOT_MODE_MAX
};

static char *boot_mode = "normal";
static unsigned int debug_policy = 0;
static int secboot_status = -1;
static int s2_warm_reset = 0;
module_param_named(mode,boot_mode, charp, S_IRUGO);
module_param(debug_policy, uint, S_IRUGO);
module_param(secboot_status, int, S_IRUGO);
module_param(s2_warm_reset, int, S_IRUGO);

/*ZTE ADD for BOOT_MODE start*/
static int g_boot_mode = 0;

int socinfo_get_ftm_flag(void)
{
	return g_boot_mode == ENUM_BOOT_MODE_FTM ? 1 : 0;
}
EXPORT_SYMBOL(socinfo_get_ftm_flag);

int socinfo_get_ffbm_flag(void)
{
	return g_boot_mode == ENUM_BOOT_MODE_FFBM ? 1 : 0;
}
EXPORT_SYMBOL(socinfo_get_ffbm_flag);

int socinfo_get_charger_flag(void)
{
	return g_boot_mode == ENUM_BOOT_MODE_CHARGER ? 1 : 0;
}
EXPORT_SYMBOL(socinfo_get_charger_flag);

int zte_get_boot_mode(void)
{
	return g_boot_mode;
}
EXPORT_SYMBOL(zte_get_boot_mode);
/*ZTE ADD for BOOT_MODE end*/

static int bootmode_init(char *mode)
{
	if (!strncmp(mode, ANDROID_BOOT_MODE_FTM, strlen(ANDROID_BOOT_MODE_FTM))) {
		g_boot_mode = ENUM_BOOT_MODE_FTM;
		pr_info("KERENEL:boot_mode:FTM\n");
	} else if (!strncmp(mode, ANDROID_BOOT_MODE_FFBM, strlen(ANDROID_BOOT_MODE_FFBM))) {
		g_boot_mode = ENUM_BOOT_MODE_FFBM;
		pr_info("KERENEL:boot_mode:FFBM\n");
	} else if (!strncmp(mode, ANDROID_BOOT_MODE_RECOVERY, strlen(ANDROID_BOOT_MODE_RECOVERY))) {
		g_boot_mode = ENUM_BOOT_MODE_RECOVERY;
		pr_info("KERENEL:boot_mode:RECOVERY\n");
	} else if (!strncmp(mode, ANDROID_BOOT_MODE_CHARGER, strlen(ANDROID_BOOT_MODE_CHARGER))) {
		g_boot_mode = ENUM_BOOT_MODE_CHARGER;
		pr_info("KERENEL:boot_mode:CHARGER\n");
	} else {
		g_boot_mode = ENUM_BOOT_MODE_NORMAL;
		pr_info("KERENEL:boot_mode:DEFAULT NORMAL\n");
	}

	return 0;
}

bool is_kernel_log_driver_enabled(void) {
	return (debug_policy & DEBUG_POLICY_USE_KERNEL_LOG_DRIVER);
}
EXPORT_SYMBOL(is_kernel_log_driver_enabled);

bool is_dm_verity_disabled(void) {
	return (debug_policy & DEBUG_POLICY_DISABLE_DM_VERITY);
}
EXPORT_SYMBOL(is_dm_verity_disabled);

bool is_fastboot_enabled(void) {
	return (debug_policy & DEBUG_POLICY_ENABLE_FASTBOOT);
}
EXPORT_SYMBOL(is_fastboot_enabled);

bool is_kernel_log_limit_disabled(void) {
	return (debug_policy & DEBUG_POLICY_DISABLE_KERNEL_LOG_LIMIT);
}
EXPORT_SYMBOL(is_kernel_log_limit_disabled);

bool is_adb_tradefed_enabled(void) {
	return (debug_policy & DEBUG_POLICY_ENABLE_ADB_TRADEFED);
}
EXPORT_SYMBOL(is_adb_tradefed_enabled);

int is_secboot_device(void) {
	return secboot_status;
}
EXPORT_SYMBOL(is_secboot_device);

int is_s2_warm_reset(void) {
    return s2_warm_reset;
}
EXPORT_SYMBOL(is_s2_warm_reset);

static int __init parse_cmdline_and_export_init(void)
{
    printk("init parse_cmdline_and_export drv!\n");

    printk(KERN_ERR "boot_mode = %s \n", boot_mode);
    bootmode_init(boot_mode);

    printk(KERN_ERR "debug_policy = 0x%8x \n", debug_policy);
    return 0;
}

static void __exit parse_cmdline_and_export_exit(void)
{
    printk("exit parse_cmdline_and_export drv!\n");
}   

module_init(parse_cmdline_and_export_init);
module_exit(parse_cmdline_and_export_exit);

MODULE_AUTHOR("boot team");
MODULE_DESCRIPTION("parse module param and export to other vendor module");
MODULE_LICENSE("GPL");
