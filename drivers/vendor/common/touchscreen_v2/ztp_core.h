/*
 * file: ztp_core.h
 *
 */
#ifndef  _ZTP_CORE_H_
#define _ZTP_CORE_H_


#ifdef CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3
extern int  ilitek_plat_dev_init(void);
extern void  ilitek_plat_dev_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
extern int  himax_common_init(void);
extern void  himax_common_exit(void);
#endif
#if 0
extern void lcd_notify_register(void);
extern void lcd_notify_unregister(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_CHIPONE
extern int  cts_i2c_driver_init(void);
extern void  cts_i2c_driver_exit(void);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9916R
extern int  goodix_ts_core_init(void);
extern void  goodix_ts_core_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_9966
extern int  goodix_ts_core_init(void);
extern void  goodix_ts_core_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_V2
extern int  goodix_ts_core_init(void);
extern void  goodix_ts_core_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_TCM
extern int  ovt_tcm_module_init(void);
extern void  ovt_tcm_module_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V2
extern int  cts_driver_init(void);
extern void  cts_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHIPONE_V3
extern int  cts_driver_init(void);
extern void  cts_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_FTS_3681
extern int  fts_ts_init(void);
extern void  fts_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
int ufp_mac_init(void);
void  ufp_mac_exit(void);
#endif
void tpd_clean_all_event(void);
int tpd_report_work_init(void);
void tpd_report_work_deinit(void);
void tpd_resume_work_init(void);
void tpd_resume_work_deinit(void);

#endif

