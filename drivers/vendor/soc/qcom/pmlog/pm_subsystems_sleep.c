/*
 *This program is used for recode the ap and modem's sleep and wake time.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/version.h>
#include <linux/tick.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <soc/qcom/qseecom_scm.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>
#include <soc/qcom/socinfo.h>
#include <linux/seq_file.h>
#include <linux/fb.h>
#include <linux/version.h>
#include <linux/syscore_ops.h>
#include <trace/hooks/gic.h>
#include <linux/irqchip/arm-gic-v3.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <trace/events/power.h>
#include <linux/soc/qcom/smem.h>

#include "pm_subsystems_sleep.h"


#if IS_ENABLED(CONFIG_QCOM_SMEM)
#define ZSW_SUBSYSINFO_ENABLE
#endif


#define ZSW_MODEM_CRASH 1
#define ZSW_ADSP_CRASH 2
#define ZSW_SLPI_CRASH 3
#define ZSW_CDSP_CRASH 4
#define ZSW_NO_CRASH 99

#define ZSW_MODEM_CRASH_FLAG 0x1
#define ZSW_ADSP_CRASH_FLAG 0x2
#define ZSW_SLPI_CRASH_FLAG 0x4
#define ZSW_CDSP_CRASH_FLAG 0x8

#define ZTE_RECORD_NUM 20

#ifdef ZSW_SUBSYSINFO_ENABLE

struct subsystem_data {
	const char *name;
	u32 smem_item;
	u32 pid;
};

static struct subsystem_data subsystems[] = {
	{ "modem", 605, 1 },
	{ "wpss", 605, 13 },
	{ "adsp", 606, 2 },
	{ "adsp_island", 613, 2 },
	{ "cdsp", 607, 5 },
	{ "slpi", 608, 3 },
	{ "slpi_island", 613, 3 },
	{ "gpu", 609, 0 },
	{ "display", 610, 0 },
	{ "apss", 631, QCOM_SMEM_HOST_ANY },
};

struct subsystem_data_zte {
struct subsystem_data ss_data;
	u32 used;
	u64 accumulated_suspend;
	u64 accumulated_resume;

	/*part wake time if >x% then record the current time
	else set value 0
	*/
	u64 starttime_partwake;
};

static struct subsystem_data_zte subsystems_zte[ZTE_RECORD_NUM] = {};
static u64 Ap_sleepcounter_time_thistime = 0;
static bool zsw_getsmem_error = false;

static int sleep_zswresumeparam_mask = 0;
module_param(sleep_zswresumeparam_mask, int, 0644);


static int sleep_zswsubsys_flag = 0;

/* 0 screen on;  1: screen off */
static int sleep_zswscnoff_state = 0;


/* 0 canot check subsys state;  1: check subsy state */
static int sleep_zswaod_state = 0;

static int trigger_wakepercent = 80;
static int time_screen_off = 180;
static int time_subsyswake = 120;
static int time_nexttrigger = 120;
static int forcetrigger_which = 0;


struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};


/*ZTE LCD ++++++ */
/*screenontime record each on -off time*/
static u64 zswscreenoffcounter_time = 0;
static u64 zswscreenoffcounter_startpoint = 0;
static u64 zswscreenoffcounter_endpoint = 0;

/* 0 screen on;  1: screen off */
static int zswsceenoff_state = 0;

static u64 screenofftime_startpoint = 0;
static u64 screenofftime_endpoint = 0;
static u64 screenofftime_delta = 0;

static u64 zswtrigercrashtime_start = 0;
/*ZTE LCD ------ */

#endif

#ifdef ZSW_SUBSYSINFO_ENABLE
static void update_screenoff_time(bool lcdoff)
{

	if (zswsceenoff_state != lcdoff) {
		zswsceenoff_state = lcdoff;
	} else {
		pr_info("[PM_V] update_screenoff_time return lcdoff=%d zswsceenoff_state=%d \n", lcdoff, zswsceenoff_state);
		return;
	}


	if (!lcdoff) {

		/* screen on */
		zswscreenoffcounter_endpoint = arch_timer_read_counter();
		if (zswscreenoffcounter_endpoint > zswscreenoffcounter_startpoint) {
			/* screen off time Approximately equal to true time */
			/* it calculate from this driver first suspend or resume */
			/* if this diver not suspend or resume, Ap may not enter sleep */
			zswscreenoffcounter_time = zswscreenoffcounter_endpoint - zswscreenoffcounter_startpoint;
		}
		pr_info("[PM_V] turn LCD off=%d scroffcountertime=%llu, curentcounter=%llu\n", lcdoff,
					zswscreenoffcounter_time, zswscreenoffcounter_endpoint);

		screenofftime_endpoint = ktime_get_real_seconds();
		if (screenofftime_endpoint > screenofftime_startpoint) {
			screenofftime_delta = screenofftime_endpoint - screenofftime_startpoint;
		}
		pr_info("[PM_V] turn LCD off=%d scrofftime=%llu, curenttime=%llu\n", lcdoff,
			screenofftime_delta, screenofftime_endpoint);
	} else {

		/* when sceen off start, reset all screen off couter time to 0 */
		zswscreenoffcounter_time = 0;
		zswscreenoffcounter_startpoint = arch_timer_read_counter();
		zswscreenoffcounter_endpoint = 0;

		screenofftime_delta = 0;
		screenofftime_startpoint = ktime_get_real_seconds();
		screenofftime_endpoint = 0;

		pr_info("[PM_V] turn LCD off=%d counter_startpoint=%llu, time_startpoint=%llu\n", lcdoff,
			zswscreenoffcounter_startpoint, screenofftime_startpoint);
	}
}

static int param_set_sleep_zswscnoff_state(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;
	int old_sleep_zswscnoff_state;

	old_sleep_zswscnoff_state = sleep_zswscnoff_state;
	pr_info("[PM_V] param_set_sleep_zswscnoff_state 0 sleep_zswscnoff_state=%d\n", sleep_zswscnoff_state);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_sleep_zswscnoff_state 1 sleep_zswscnoff_state=%d\n", sleep_zswscnoff_state);

	//reset resume mask to ZSW_NO_CRASH
	if (sleep_zswscnoff_state == 3)
	{
		sleep_zswscnoff_state = old_sleep_zswscnoff_state;
		sleep_zswresumeparam_mask = ZSW_NO_CRASH;
		pr_info("[PM_V] param_set_sleep_zswscnoff_state rest sleep_zswresumeparam_mask=%d\n", sleep_zswresumeparam_mask);
		return ret;
	}

	update_screenoff_time(sleep_zswscnoff_state);

	return ret;
}

static const struct kernel_param_ops zswscnoff_state_ops = {
    .set = param_set_sleep_zswscnoff_state,
    .get = param_get_int,
};
module_param_cb(sleep_zswscnoff_state, &zswscnoff_state_ops,
        &sleep_zswscnoff_state, 0644);


//Aod start
static int param_set_aod_state(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_aod_state 0 sleep_zswaod_state=%d\n", sleep_zswaod_state);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_aod_state 1 sleep_zswaod_state=%d\n", sleep_zswaod_state);

	return ret;
}

static const struct kernel_param_ops zswaod_state_ops = {
    .set = param_set_aod_state,
    .get = param_get_int,
};
module_param_cb(sleep_zswaod_state, &zswaod_state_ops,
        &sleep_zswaod_state, 0644);
//Aod end

//cloud update start
//trigger_wakepercent start
static int param_set_trigger_wakepercent(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_trigger_wakepercent 0 trigger_wakepercent=%d\n", trigger_wakepercent);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_trigger_wakepercent 1 trigger_wakepercent=%d\n", trigger_wakepercent);

	return ret;
}

static const struct kernel_param_ops zsw_trigger_wakepercent_ops = {
    .set = param_set_trigger_wakepercent,
    .get = param_get_int,
};
module_param_cb(trigger_wakepercent, &zsw_trigger_wakepercent_ops,
        &trigger_wakepercent, 0644);
//trigger_wakepercent end

//time_screen_off start
static int param_set_time_screen_off(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_time_screen_off 0 time_screen_off=%d\n", time_screen_off);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_time_screen_off 1 time_screen_off=%d\n", time_screen_off);

	return ret;
}

static const struct kernel_param_ops zsw_time_screen_off_ops = {
    .set = param_set_time_screen_off,
    .get = param_get_int,
};
module_param_cb(time_screen_off, &zsw_time_screen_off_ops,
        &time_screen_off, 0644);
//time_screen_off end

//time_subsyswake start
static int param_set_time_subsyswake(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_time_subsyswake 0 time_subsyswake=%d\n", time_subsyswake);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_time_subsyswake 1 time_subsyswake=%d\n", time_subsyswake);

	return ret;
}

static const struct kernel_param_ops zsw_time_subsyswake_ops = {
    .set = param_set_time_subsyswake,
    .get = param_get_int,
};
module_param_cb(time_subsyswake, &zsw_time_subsyswake_ops,
        &time_subsyswake, 0644);
//time_subsyswake end

//time_nexttrigger start
static int param_set_time_nexttrigger(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_time_nexttrigger 0 time_nexttrigger=%d\n", time_nexttrigger);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_time_nexttrigger 1 time_nexttrigger=%d\n", time_nexttrigger);

	return ret;
}

static const struct kernel_param_ops zsw_time_nexttrigger_ops = {
    .set = param_set_time_nexttrigger,
    .get = param_get_int,
};
module_param_cb(time_nexttrigger, &zsw_time_nexttrigger_ops,
        &time_nexttrigger, 0644);
//time_nexttrigger end

//forcetrigger_which start
static int param_set_forcetrigger_which(const char *kmessage,
				   const struct kernel_param *kp)
{
	int ret;

	pr_info("[PM_V] param_set_forcetrigger_which 0 forcetrigger_which=%d\n", forcetrigger_which);
	ret = param_set_int(kmessage, kp);
	pr_info("[PM_V] param_set_forcetrigger_which 1 forcetrigger_which=%d\n", forcetrigger_which);

	return ret;
}

static const struct kernel_param_ops zsw_forcetrigger_which_ops = {
    .set = param_set_forcetrigger_which,
    .get = param_get_int,
};
module_param_cb(forcetrigger_which, &zsw_forcetrigger_which_ops,
        &forcetrigger_which, 0644);
//forcetrigger_which end



static void zsw_pm_record_suspend_time(const char* name, struct sleep_stats *stat, int j)
{
	u64 accumulated = stat->accumulated;
	/*
	 * If a subsystem is in sleep when reading the sleep stats adjust
	 * the accumulated sleep duration to show actual sleep time.
	 */
	if (stat->last_entered_at > stat->last_exited_at)
		accumulated += arch_timer_read_counter()
			       - stat->last_entered_at;

	subsystems_zte[j].accumulated_suspend = accumulated;
	//pr_info("%s  Count = %u  Last Entered At = %llu  Last Exited At = %llu  Accumulated Duration = %llu  \n", name, stat->count, stat->last_entered_at, stat->last_exited_at, accumulated);
}

void zsw_pm_record_suspend_stats(void)
{
	int j = 0;

	for (j = 0; j < ARRAY_SIZE(subsystems_zte); j++) {
		if (1 == subsystems_zte[j].used) {

			struct subsystem_data *subsystem = &(subsystems_zte[j].ss_data);
			struct sleep_stats *stat;

			stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
			if (IS_ERR(stat)) {
				zsw_getsmem_error = true;
				pr_info("[PM_V] zsw_pm_record_suspend_stats error \n");
				return ;
			}
			zsw_pm_record_suspend_time(subsystem->name, stat, j);
		}
	}
}

static void zsw_pm_record_resume_time(const char* name, struct sleep_stats *stat, int j)
{
	u64 accumulated = stat->accumulated;
	/*
	 * If a subsystem is in sleep when reading the sleep stats adjust
	 * the accumulated sleep duration to show actual sleep time.
	 */
	if (stat->last_entered_at > stat->last_exited_at)
		accumulated += arch_timer_read_counter()
			       - stat->last_entered_at;

	subsystems_zte[j].accumulated_resume = accumulated;

	//if (631 == subsystems_zte[j].smem_item) {
	if (!strcmp(subsystems_zte[j].ss_data.name, "apss")) {
		if (subsystems_zte[j].accumulated_resume > subsystems_zte[j].accumulated_suspend) {
			Ap_sleepcounter_time_thistime = subsystems_zte[j].accumulated_resume - subsystems_zte[j].accumulated_suspend;
		} else {
			Ap_sleepcounter_time_thistime = 0;
		}
	}
	//pr_info("%s  Count = %u  Last Entered At = %llu  Last Exited At = %llu  Accumulated Duration = %llu  \n", name, stat->count, stat->last_entered_at, stat->last_exited_at, accumulated);
}

void zsw_pm_record_resume_stats(void)
{
	int j = 0;

	for (j = 0; j < ARRAY_SIZE(subsystems_zte); j++) {
		if (1 == subsystems_zte[j].used) {

			struct subsystem_data *subsystem = &(subsystems_zte[j].ss_data);
			struct sleep_stats *stat;

			stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
			if (IS_ERR(stat)) {
				zsw_getsmem_error = true;
				pr_info("[PM_V] zsw_pm_record_resume_stats error \n");
				return ;
			}
			zsw_pm_record_resume_time(subsystem->name, stat, j);
		}
	}
}


void zsw_pm_resume_calculate_wakepercent(void)
{
	int j = 0;
	unsigned percent_wake = 0;
	u64 delta = 0;
	u64 subsys_delta = 0;
	u64 zswtrigercrashtime_delta = 0;
	u64 current_s = 0;

    u64 current_time_scr = 0;
    u64 current_time_subsyswake = 0;

    u64 screenofftime_delta_pre = 0;

	bool btrigcrash = false;
	bool bneedcrash = false;

    if (zsw_getsmem_error) {
        pr_info("[PM_V] zsw_pm_resume_calculate_wakepercent zsw_getsmem_error\n");
        return;
    }

	if (screenofftime_startpoint > 0) {
		/*if end point time is not record, record delta here*/
		if (screenofftime_endpoint == 0) {
            current_time_scr = ktime_get_real_seconds();
            screenofftime_delta_pre = screenofftime_delta;
            if (current_time_scr > screenofftime_startpoint) {
                screenofftime_delta = current_time_scr - screenofftime_startpoint;
            } else {
                screenofftime_delta = 0;
                pr_info("[PM_V] calculate_wakepercent resume time decrease reset\n");
            }

            /*if screen off delta > pre screen off delta + 1h，set delta=0
              maybe timezone change or ap long time not enter sleep*/
            if (screenofftime_delta > (screenofftime_delta_pre + 60 * 60)) {
                screenofftime_delta = 0;
                screenofftime_startpoint = current_time_scr;
                pr_info("[PM_V] calculate_wakepercent resume time interval too long reset\n");
            }

			//screenofftime_delta = ktime_get_real_seconds() - screenofftime_startpoint;
			pr_info("[PM_V] calculate_wakepercent record scr off timezone time delta \n");
		} else {
            screenofftime_delta = 0;
        }
	} else {
		screenofftime_delta = 0;
	}
	pr_info("[PM_V] calculate_wakepercent timezone new1 screenofftime_delta=%llu \n", screenofftime_delta);
	sleep_zswsubsys_flag = 0;
	sleep_zswresumeparam_mask = ZSW_NO_CRASH;

	for (j = 0; j < ARRAY_SIZE(subsystems_zte); j++) {
		if (1 == subsystems_zte[j].used) {

			struct subsystem_data *subsystem = &(subsystems_zte[j].ss_data);

			if (subsystems_zte[j].accumulated_resume > subsystems_zte[j].accumulated_suspend) {
				delta = subsystems_zte[j].accumulated_resume - subsystems_zte[j].accumulated_suspend;
			} else {
				delta = 0;
			}

            /*init every loop item*/
			btrigcrash = false;
            subsys_delta = 0;
			percent_wake = 0;
			if ((subsystems_zte[j].accumulated_resume > 0) && (Ap_sleepcounter_time_thistime > 0)) {
				if (delta < Ap_sleepcounter_time_thistime) {
					percent_wake = (delta * 100)/Ap_sleepcounter_time_thistime;
					percent_wake = 100 - percent_wake;
				} else {
					percent_wake = 0;
				}
			}

			if (!zsw_getsmem_error) {
				// if percent > x , write value for subsys crash  test

                if (trigger_wakepercent < 0 || trigger_wakepercent > 100) {
                    trigger_wakepercent = 80;
                }

				if ((percent_wake > trigger_wakepercent) || (forcetrigger_which > 0)) {
					if (subsystems_zte[j].starttime_partwake == 0) {
						/*first record */
						subsystems_zte[j].starttime_partwake = ktime_get_real_seconds();
					} else {
						/*if screen off > 3h:  3 * 60 * 60*/
                        //time_screen_off >= 1 min
                        if (time_screen_off < 1) {
                            time_screen_off = 1;
                        }

                        current_time_subsyswake = ktime_get_real_seconds();
                        if (current_time_subsyswake > subsystems_zte[j].starttime_partwake) {
                            subsys_delta = current_time_subsyswake - subsystems_zte[j].starttime_partwake;
                        }

						if (screenofftime_delta > time_screen_off * 60) {
							//subsys_delta = ktime_get_real_seconds() - subsystems_zte[j].starttime_partwake;
							/*if subsys wakeup time when screen off > 2h*/
                            //time_subsyswake 2 * 60 * 60
                            //time_subsyswake > 1 min
                            if (time_subsyswake < 1) {
                                time_subsyswake = 1;
                            }
							if (subsys_delta > time_subsyswake * 60) {
								btrigcrash = true;
								subsystems_zte[j].starttime_partwake = 0;
							}

                            if (forcetrigger_which > 0) {
                                btrigcrash = true;
                                pr_info("[PM_V] calculate_wakepercent forcetrigger_which=%d\n", forcetrigger_which);
                            }
						}
					}
				} else {
					/* maybe AP resume - suspend = 0 ignore this scene */
					if (Ap_sleepcounter_time_thistime > 0) {
						subsystems_zte[j].starttime_partwake = 0;
					}
				}

				if (btrigcrash) {
					if (zswtrigercrashtime_start == 0) {
						zswtrigercrashtime_start = ktime_get_real_seconds();
						bneedcrash = true;
						pr_info("[PM_V] calculate_wakepercent a zswtrigercrashtime_delta=%llu start=%llu\n", zswtrigercrashtime_delta, zswtrigercrashtime_start);
					} else {
						current_s = ktime_get_real_seconds();
						if (current_s > zswtrigercrashtime_start) {
							zswtrigercrashtime_delta = current_s - zswtrigercrashtime_start;
						} else {
							zswtrigercrashtime_delta = 0;
						}
						/*next crash time need 2 h later */
                        //time_nexttrigger : 2 * 60 * 60
                        //time_nexttrigger > 1 min
                        if (time_nexttrigger < 1) {
                            time_nexttrigger = 1;
                        }
						if (zswtrigercrashtime_delta > time_nexttrigger * 60) {
							bneedcrash = true;
							/*record crash start time from current time + 1 */
                            //record crash start time after loop end
							//zswtrigercrashtime_start = ktime_get_real_seconds() + 1;
						}
						pr_info("[PM_V] calculate_wakepercent zswtrigercrashtime_delta=%llu start=%llu\n", zswtrigercrashtime_delta, zswtrigercrashtime_start);
					}
					if (bneedcrash) {
						sleep_zswresumeparam_mask = ZSW_NO_CRASH;

                        if (forcetrigger_which > 0) {
                            if (forcetrigger_which == ZSW_CDSP_CRASH_FLAG) {
                                sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_CDSP_CRASH_FLAG;
                            }
                            if (forcetrigger_which == ZSW_SLPI_CRASH_FLAG) {
                                sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_SLPI_CRASH_FLAG;
                            }
                            if (forcetrigger_which == ZSW_ADSP_CRASH_FLAG) {
                                sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_ADSP_CRASH_FLAG;
                            }
                            if (forcetrigger_which == ZSW_MODEM_CRASH_FLAG) {
                                sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_MODEM_CRASH_FLAG;
                            }

                            pr_info("[PM_V] calculate_wakepercent test sleep_zswsubsys_flag=%d modem=%d adsp=%d slpi=%d cdsp=%d aod=%d forcetrigger_which=%d\n", sleep_zswsubsys_flag, 
                                     sleep_zswsubsys_flag & ZSW_MODEM_CRASH_FLAG, 
                                     sleep_zswsubsys_flag & ZSW_ADSP_CRASH_FLAG,
                                     sleep_zswsubsys_flag & ZSW_SLPI_CRASH_FLAG,
                                     sleep_zswsubsys_flag & ZSW_CDSP_CRASH_FLAG,
                                     sleep_zswaod_state,
                                     forcetrigger_which);

                            break;
                        }

						if (!strcmp(subsystems_zte[j].ss_data.name, "cdsp")) {
							//sleep_zswresumeparam_mask = ZSW_CDSP_CRASH;
							sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_CDSP_CRASH_FLAG;
						}
						if (!strcmp(subsystems_zte[j].ss_data.name, "slpi")) {
							//sleep_zswresumeparam_mask = ZSW_SLPI_CRASH;
							sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_SLPI_CRASH_FLAG;
						}
						if (!strcmp(subsystems_zte[j].ss_data.name, "adsp")) {
							//sleep_zswresumeparam_mask = ZSW_ADSP_CRASH;
							sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_ADSP_CRASH_FLAG;
						}
						if (!strcmp(subsystems_zte[j].ss_data.name, "modem")) {
							//sleep_zswresumeparam_mask = ZSW_MODEM_CRASH;
							sleep_zswsubsys_flag = sleep_zswsubsys_flag | ZSW_MODEM_CRASH_FLAG;
						}


					} else {
						sleep_zswresumeparam_mask = ZSW_NO_CRASH;
					}
				} else {
					sleep_zswresumeparam_mask = ZSW_NO_CRASH;
				}
			}

			pr_info("[PM_V] calculate_wakepercent 1 delta=%llu subsys_delta=%llu s percent_wake=%llu name=%s Ap_sleep=%llu subsys_flag=%d, mask=%d\n",
					delta, subsys_delta, percent_wake, subsystem->name,
					Ap_sleepcounter_time_thistime, sleep_zswsubsys_flag, sleep_zswresumeparam_mask);

		}
	}

    if (bneedcrash) {
        /*record crash start time from current time + 1 */
        zswtrigercrashtime_start = ktime_get_real_seconds() + 1;
    }

	//modem not sleep
	if ((sleep_zswsubsys_flag & ZSW_MODEM_CRASH_FLAG) != 0) {
		//and adsp sleep, then trigger modem dump
		if ((sleep_zswsubsys_flag & ZSW_ADSP_CRASH_FLAG) == 0) {
			pr_info("[PM_V] calculate_wakepercent a modem crash sleep_zswsubsys_flag=%llu\n", sleep_zswsubsys_flag);
			sleep_zswsubsys_flag = 0;
			sleep_zswresumeparam_mask = ZSW_MODEM_CRASH;
		}

		//and slpi sleep, then trigger modem dump
		if ((sleep_zswsubsys_flag & ZSW_SLPI_CRASH_FLAG) == 0) {
			pr_info("[PM_V] calculate_wakepercent s modem crash sleep_zswsubsys_flag=%llu\n", sleep_zswsubsys_flag);
			sleep_zswsubsys_flag = 0;
			sleep_zswresumeparam_mask = ZSW_MODEM_CRASH;
		}
	}

    //sleep_zswaod_state=1, check subsystems sleep
    if (sleep_zswaod_state == 1) {
        //adsp not sleep, but modem sleep, then trigger adsp dump
        if ((sleep_zswsubsys_flag & ZSW_ADSP_CRASH_FLAG) != 0) {
            if ((sleep_zswsubsys_flag & ZSW_MODEM_CRASH_FLAG) == 0) {
                pr_info("[PM_V] calculate_wakepercent adsp crash sleep_zswsubsys_flag=%llu\n", sleep_zswsubsys_flag);
                sleep_zswsubsys_flag = 0;
                sleep_zswresumeparam_mask = ZSW_ADSP_CRASH;
            }
        }

        //slpi not sleep, but modem sleep, then trigger slpi dump
        if ((sleep_zswsubsys_flag & ZSW_SLPI_CRASH_FLAG) != 0) {
            if ((sleep_zswsubsys_flag & ZSW_MODEM_CRASH_FLAG) == 0) {
                pr_info("[PM_V] calculate_wakepercent slpi crash sleep_zswsubsys_flag=%llu\n", sleep_zswsubsys_flag);
                sleep_zswsubsys_flag = 0;
                sleep_zswresumeparam_mask = ZSW_SLPI_CRASH;
            }
        }
    }

	if ((sleep_zswsubsys_flag & ZSW_CDSP_CRASH_FLAG) != 0) {
		pr_info("[PM_V] calculate_wakepercent cdsp crash sleep_zswsubsys_flag=%llu\n", sleep_zswsubsys_flag);
		sleep_zswsubsys_flag = 0;
		sleep_zswresumeparam_mask = ZSW_CDSP_CRASH;
	}

    //only for not trigger crash test
    //sleep_zswresumeparam_mask = 111;

}

#endif


void pm_subsystems_resume_suspend(bool resume)
{
#ifdef ZSW_SUBSYSINFO_ENABLE
    if (resume) {
        pr_info("[PM_V] zsw_pm_debug_resume new 1\n");
        zsw_pm_record_resume_stats();
        zsw_pm_resume_calculate_wakepercent();
    } else {
        pr_info("[PM_V] zsw_pm_debug_suspend new 1\n");
        zsw_pm_record_suspend_stats();
    }
#endif
}



void pm_subsystems_init(void)
{

    pr_info("pm_subsystems_init init start\n");

    {
#ifdef ZSW_SUBSYSINFO_ENABLE
	const char *name;
	int i, j;
    int n_subsystems = 0;
    char* subsystems_str_8550[8] = {"modem", "adsp", "adsp_island", "cdsp", "apss", "null", "null", "null"};
    char* subsystems_str_default[8] = {"null", "null", "null", "null", "apss", "null", "null", "null"};

	n_subsystems = 8;

    pr_info("pm_subsystems_init %d, %d\n", ARRAY_SIZE(subsystems_str_8550), ARRAY_SIZE(subsystems_str_default));

	for (i = 0; i < n_subsystems; i++) {

#if CONFIG_VENDOR_PMLOG_PLATFORM == 8550
        name = subsystems_str_8550[i];
#else
        name = subsystems_str_default[i];
#endif

		for (j = 0; j < ARRAY_SIZE(subsystems); j++) {
			if (!strcmp(subsystems[j].name, name)) {
				//subsystems_zte
				memcpy(&(subsystems_zte[i].ss_data), &subsystems[j], sizeof(struct subsystem_data));
				subsystems_zte[i].used =1;
				break;
			}
		}
	}
#endif
    }

}
