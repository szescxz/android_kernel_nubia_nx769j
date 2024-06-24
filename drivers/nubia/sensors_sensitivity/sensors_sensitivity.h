/*
* This file is part to support acceleration and gyrocopter sensitivity adjust.

*Reversion
*
====================================================================================
*/
#ifndef __SENSORS_SENS_H__
#define __SENSORS_SENS_H__

#include <linux/types.h>

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DRIVER_VERSION "1.0"

#define LOG_TAG "NUBIA_SENSORS_SENS"

#define SENSOR_ERR 1
#define SENSOR_INFO 2
#define SENSOR_DEBUG 3
#define SENSOR_VERBOSE 4

extern int SENSORS_SENS_LOG_LEVEL;

#define __sensor_log(level, fmt, args...) do { \
    if (level <= SENSORS_SENS_LOG_LEVEL) { \
        printk(KERN_ERR "[%s] [%s:%d] " fmt,\
						LOG_TAG, __FUNCTION__, __LINE__, ##args); \
    } \
} while (0)


#define __sensor_log_limite(level, fmt, args...) do { \
	if (level <= SENSORS_SENS_LOG_LEVEL) { \
		printk_ratelimited(KERN_ERR "[%s] [%s:%d] " fmt,\
						LOG_TAG, __FUNCTION__, __LINE__, ##args); \
	} \
} while (0)

#define SENSOR_LOG_ERROR(fmt, args...) printk(KERN_DEBUG "[%s] [%s:%d] "  fmt,\
					LOG_TAG, __FUNCTION__, __LINE__, ##args)

#define SENSOR_LOG_INFO(fmt, args...) \
	do { \
		__sensor_log(SENSOR_INFO, fmt, ##args);\
	} while (0)

#define SENSOR_LOG_DEBUG(fmt, args...) \
	do { \
		__sensor_log(SENSOR_DEBUG, fmt, ##args);\
	} while (0)

#define SENSOR_LOG_VERBOSE(fmt, args...) \
	do { \
		__sensor_log(SENSOR_VERBOSE, fmt, ##args);\
	} while (0)

#define SENSOR_LOG_INFO_LIMIT(fmt, args...) \
	do { \
		__sensor_log_limite(SENSOR_INFO, fmt, ##args);\
	} while (0)

#define SENSOR_LOG_DEBUG_LIMIT(fmt, args...) \
		do { \
			__sensor_log_limite(SENSOR_DEBUG, fmt, ##args);\
		} while (0)

#ifdef NUBIA_MUTEX_DEBUG
#define NUBIA_MUTEX_LOCK(m) { \
        printk(KERN_INFO "%s: Mutex Lock\n", __func__); \
        mutex_lock(m); \
    }
#define NUBIA_MUTEX_UNLOCK(m) { \
        printk(KERN_INFO "%s: Mutex Unlock\n", __func__); \
        mutex_unlock(m); \
    }
#else
#define NUBIA_MUTEX_LOCK(m) { \
        mutex_lock(m); \
    }
#define NUBIA_MUTEX_UNLOCK(m) { \
        mutex_unlock(m); \
    }
#endif

struct sensors_sens {
	//struct device *sensors_sens_dev;
	struct device *sensors_accel_dev;
	struct device *sensors_gyro_dev;
	struct mutex sensors_sens_lock;

	bool accel_sens_enable;
	int accel_x;  /*accel X-axis sensitivity,0~200%,defult is 100%*/
	int accel_y;
	int accel_z;

	bool gyro_sens_enable;
	int gyro_x;  /*gyro X-axis sensitivity,0~200%,defult is 100%*/
	int gyro_y;
	int gyro_z;
};

#endif
