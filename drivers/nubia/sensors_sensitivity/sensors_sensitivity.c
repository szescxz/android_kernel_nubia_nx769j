/*
* This file is part to support sensor sensitivity adjust.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
* 02110-1301 USA
*
*Reversion
*

when         	who         		Remark : what, where, why          							version
-----------   	------------     	-----------------------------------   					    ------------------
2019/12/4       GaoKuan              Add acceleration and gyrocopter sensitivity adjust         v1.0
====================================================================================
*/

#include "sensors_sensitivity.h"

//int SENSORS_SENS_LOG_LEVEL = SENSOR_ERR;
/*for R&D debug, relase version need set SENSOR_ERR*/
int SENSORS_SENS_LOG_LEVEL = SENSOR_DEBUG;

#define SNENSORS_SENS_CLASS_NAME "sensors_sensitivity"
#define SENSOR_ACCEL_DEV_NAME "accel"
#define SENSOR_GYRO_DEV_NAME "gyro"


static dev_t  sensors_accel_dev_t;
static dev_t  sensors_gyro_dev_t;

static struct class  *sensors_sens_class;
static struct sensors_sens *sensors_sens_data;

static int sensor_create_sysfs_interfaces(struct device *dev,
        struct device_attribute *attr, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (device_create_file(dev, attr + i))
			goto exit;
	return 0;
exit:
	for (; i >= 0 ; i--)
		device_remove_file(dev, attr + i);
	SENSOR_LOG_ERROR("failed to create sysfs interface\n");
	return -ENODEV;
}

static void sensor_remove_sysfs_interfaces(struct device *dev,
        struct device_attribute *attr, int size)
{
	int i;
	for (i = 0; i < size; i++)
		device_remove_file(dev, attr + i);
}

static ssize_t accel_enable_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t size)
{
	int ret;
	int enable;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &enable);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("accel sensitivity adjust enable is %d \n", enable);
	switch(enable) {
	case 0:
		NUBIA_MUTEX_LOCK(&data->sensors_sens_lock);
		data->accel_sens_enable = false;
		NUBIA_MUTEX_UNLOCK(&data->sensors_sens_lock);
		break;

	case 1:
		NUBIA_MUTEX_LOCK(&data->sensors_sens_lock);
		data->accel_sens_enable = true;
		NUBIA_MUTEX_UNLOCK(&data->sensors_sens_lock);
		break;
	default:
		return -EINVAL;
	}
	return size;
}
static ssize_t accel_enable_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("accel sensitivity adjust enable is %d\n",data->accel_sens_enable);
	return sprintf(buf, "%d\n", data->accel_sens_enable);
}

static ssize_t accel_x_axial_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("accel x sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("accel x sensitivity value illegal\n");
		return -EINVAL;
	}
	data->accel_x = value;
	return size;
}
static ssize_t accel_x_axial_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("accel x sensitivity is %d\n",data->accel_x);
	return sprintf(buf, "%d\n", data->accel_x);
}

static ssize_t accel_y_axial_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("accel y sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("accel y sensitivity value illegal\n");
		return -EINVAL;
	}
	data->accel_y = value;
	return size;
}
static ssize_t accel_y_axial_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("accel y sensitivity is %d\n",data->accel_y);
	return sprintf(buf, "%d\n", data->accel_y);
}

static ssize_t accel_z_axial_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("accel z sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("accel z sensitivity value illegal\n");
		return -EINVAL;
	}
	data->accel_z = value;
	return size;
}
static ssize_t accel_z_axial_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("accel z sensitivity is %d\n",data->accel_z);
	return sprintf(buf, "%d\n", data->accel_z);
}

static struct device_attribute attrs_sensors_sens_accel_device[] = {
	__ATTR(accel_enable, 0664, accel_enable_show, accel_enable_store),
	__ATTR(accel_x, 0664, accel_x_axial_show, accel_x_axial_store),
	__ATTR(accel_y, 0664, accel_y_axial_show, accel_y_axial_store),
	__ATTR(accel_z, 0664, accel_z_axial_show, accel_z_axial_store)
};

static ssize_t gyro_enable_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t size)
{
	int ret;
	int enable;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &enable);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("accel sensitivity adjust enable is %d \n", enable);
	switch(enable) {
	case 0:
		NUBIA_MUTEX_LOCK(&data->sensors_sens_lock);
		data->gyro_sens_enable = false;
		NUBIA_MUTEX_UNLOCK(&data->sensors_sens_lock);
		break;

	case 1:
		NUBIA_MUTEX_LOCK(&data->sensors_sens_lock);
		data->gyro_sens_enable = true;
		NUBIA_MUTEX_UNLOCK(&data->sensors_sens_lock);
		break;
	default:
		return -EINVAL;
	}
	return size;
}
static ssize_t gyro_enable_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("accel sensitivity adjust enable is %d\n",data->gyro_sens_enable);
	return sprintf(buf, "%d\n", data->gyro_sens_enable);
}

static ssize_t gyro_x_axial_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("gyro x sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("gyro x sensitivity value illegal\n");
		return -EINVAL;
	}
	data->gyro_x = value;
	return size;
}
static ssize_t gyro_x_axial_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("gyro x sensitivity is %d\n",data->gyro_x);
	return sprintf(buf, "%d\n", data->gyro_x);
}

static ssize_t gyro_y_axial_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("gyro y sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("gyro y sensitivity value illegal\n");
		return -EINVAL;
	}
	data->gyro_y = value;
	return size;
}
static ssize_t gyro_y_axial_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("gyro y sensitivity is %d\n",data->gyro_y);
	return sprintf(buf, "%d\n", data->gyro_y);
}

static ssize_t gyro_z_axial_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t size)
{
	int ret;
	int value;
	struct sensors_sens *data = dev_get_drvdata(dev);
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return -EINVAL;
	SENSOR_LOG_INFO("gyro z sensitivity is %d \n", value);
	if(value > 200 || value < 0) {
		SENSOR_LOG_ERROR("gyro z sensitivity value illegal\n");
		return -EINVAL;
	}
	data->gyro_z = value;
	return size;
}
static ssize_t gyro_z_axial_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	struct sensors_sens *data = dev_get_drvdata(dev);
	SENSOR_LOG_INFO("gyro z sensitivity is %d\n",data->gyro_z);
	return sprintf(buf, "%d\n", data->gyro_z);
}

static struct device_attribute attrs_sensors_sens_gyro_device[] = {
	__ATTR(gyro_enable, 0664, gyro_enable_show, gyro_enable_store),
	__ATTR(gyro_x, 0664, gyro_x_axial_show, gyro_x_axial_store),
	__ATTR(gyro_y, 0664, gyro_y_axial_show, gyro_y_axial_store),
	__ATTR(gyro_z, 0664, gyro_z_axial_show, gyro_z_axial_store)
};

int sensors_sensitivity_register(void)
{
	int err = 0;

	SENSOR_LOG_INFO("sensors_sensitivity_register start\n");

	sensors_sens_data = kzalloc(sizeof(struct sensors_sens), GFP_KERNEL);
	if (!sensors_sens_data) {
		SENSOR_LOG_ERROR("kzalloc sensors_sens_data failed\n");
		err = -ENOMEM;
		goto exit;
	}
	mutex_init(&sensors_sens_data->sensors_sens_lock);
	sensors_sens_data->accel_sens_enable = false;
	sensors_sens_data->accel_x = 100;
	sensors_sens_data->accel_y = 100;
	sensors_sens_data->accel_z = 100;

	sensors_sens_data->gyro_sens_enable = false;
	sensors_sens_data->gyro_x = 100;
	sensors_sens_data->gyro_y = 100;
	sensors_sens_data->gyro_z = 100;

	sensors_sens_class = class_create(THIS_MODULE, SNENSORS_SENS_CLASS_NAME);

	alloc_chrdev_region(&sensors_accel_dev_t, 0, 1, SENSOR_ACCEL_DEV_NAME);
	sensors_sens_data->sensors_accel_dev = device_create(sensors_sens_class, 0,
	                                       sensors_accel_dev_t, 0, SENSOR_ACCEL_DEV_NAME);
	if (IS_ERR(sensors_sens_data->sensors_accel_dev)) {
		SENSOR_LOG_ERROR("device_create failed\n");
		goto accel_create_dev_failed;
	}
	dev_set_drvdata(sensors_sens_data->sensors_accel_dev, sensors_sens_data);

	alloc_chrdev_region(&sensors_gyro_dev_t, 0, 1, SENSOR_GYRO_DEV_NAME);
	sensors_sens_data->sensors_gyro_dev = device_create(sensors_sens_class, 0,
	                                      sensors_gyro_dev_t, 0, SENSOR_GYRO_DEV_NAME);
	if (IS_ERR(sensors_sens_data->sensors_gyro_dev)) {
		SENSOR_LOG_ERROR("device_create failed\n");
		goto gyro_create_dev_failed;
	}
	dev_set_drvdata(sensors_sens_data->sensors_gyro_dev, sensors_sens_data);

	err = sensor_create_sysfs_interfaces(sensors_sens_data->sensors_accel_dev, attrs_sensors_sens_accel_device, ARRAY_SIZE(attrs_sensors_sens_accel_device));
	if (err < 0) {
		SENSOR_LOG_ERROR("create sysfs interfaces failed\n");
		goto create_accel_sysfs_interface_failed;
	}
	err = sensor_create_sysfs_interfaces(sensors_sens_data->sensors_gyro_dev, attrs_sensors_sens_gyro_device, ARRAY_SIZE(attrs_sensors_sens_gyro_device));
	if (err < 0) {
		SENSOR_LOG_ERROR("create sysfs interfaces failed\n");
		goto create_gyro_sysfs_interface_failed;
	}

	SENSOR_LOG_INFO("sensors_sensitivity_register ok.\n");

	return 0;
create_gyro_sysfs_interface_failed:
	sensor_remove_sysfs_interfaces(sensors_sens_data->sensors_gyro_dev, attrs_sensors_sens_gyro_device, ARRAY_SIZE(attrs_sensors_sens_gyro_device));
create_accel_sysfs_interface_failed:
	sensor_remove_sysfs_interfaces(sensors_sens_data->sensors_accel_dev, attrs_sensors_sens_accel_device, ARRAY_SIZE(attrs_sensors_sens_accel_device));
gyro_create_dev_failed:
	sensors_sens_data->sensors_gyro_dev = NULL;
	device_destroy(sensors_sens_class, sensors_gyro_dev_t);
	class_destroy(sensors_sens_class);
accel_create_dev_failed:
	sensors_sens_data->sensors_accel_dev = NULL;
	device_destroy(sensors_sens_class, sensors_accel_dev_t);
	class_destroy(sensors_sens_class);
exit:
	return err;
}

void sensors_sensitivity_unregister(void)
{
	sensor_remove_sysfs_interfaces(sensors_sens_data->sensors_accel_dev, attrs_sensors_sens_accel_device, ARRAY_SIZE(attrs_sensors_sens_accel_device));
	sensor_remove_sysfs_interfaces(sensors_sens_data->sensors_gyro_dev, attrs_sensors_sens_gyro_device, ARRAY_SIZE(attrs_sensors_sens_gyro_device));
	mutex_destroy(&sensors_sens_data->sensors_sens_lock);
	kfree(sensors_sens_data);
}

static int __init sensors_sensitivity_init(void)
{
	SENSOR_LOG_INFO("init\n");
	sensors_sensitivity_register();
	return 0;
}

static void __exit sensors_sensitivity_exit(void)
{

	sensors_sensitivity_unregister();
}

MODULE_AUTHOR("Peripherial team, NUBIA");
MODULE_DESCRIPTION("Sensors sensitivity driver.");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);

module_init(sensors_sensitivity_init);
module_exit(sensors_sensitivity_exit);
