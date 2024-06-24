/* zte_ir.c - zte ir driver
 *
 * Copyright (c) 2014-2015, Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define DEBUG

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>

#define ZTEIR_DEV_NAME "zte,irled"

#define IR_SPI_DEFAULT_FREQUENCY	38000
#define IR_SPI_MAX_BUFSIZE		 40000

#define LIRC_SET_SEND_CARRIER          _IOW('i', 0x31, __u32)
#define LIRC_SET_SEND_DUTY_CYCLE       _IOW('i', 0x32, __u32)

static int spidev_major;
#define N_SPI_MINORS 1 /* ... up to 256 */

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static struct class *zte_ir_class;
static const struct of_device_id zte_ir_dt_ids[] = {
	{ .compatible = "zte,zte_ir" },
	{},
};
MODULE_DEVICE_TABLE(of, zte_ir_dt_ids);

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static bool debug_enabled = true;

struct zte_ir_data {
	dev_t devt;

	u16 tx_buf[IR_SPI_MAX_BUFSIZE];
	u16 pulse;
	u16 space;

	/* freq * 16 */
	u32 speed_hz;

	struct spi_device *spi;
	struct regulator *regulator;
	spinlock_t spi_lock;
	struct mutex buf_lock;
	struct list_head device_entry;
};

#ifdef CONFIG_ACPI

/* Dummy SPI devices not to be used in production systems */
#define SPIDEV_ACPI_DUMMY 1

static const struct acpi_device_id zte_ir_acpi_ids[] = {
    /*
     * The ACPI SPT000* devices are only meant for development and
     * testing. Systems used in production should have a proper ACPI
     * description of the connected peripheral and they should also use
     * a proper driver instead of poking directly to the SPI bus.
     */
	{ "IR0001", SPIDEV_ACPI_DUMMY },
	{ "IR0002", SPIDEV_ACPI_DUMMY },
	{ "IR0003", SPIDEV_ACPI_DUMMY },
	{},
};
MODULE_DEVICE_TABLE(acpi, zte_ir_acpi_ids);

static void zte_ir_probe_acpi(struct spi_device *spi)
{
	const struct acpi_device_id *id;

	if (!has_acpi_companion(&spi->dev))
		return;

	id = acpi_match_device(zte_ir_acpi_ids, &spi->dev);
	if (WARN_ON(!id))
		return;
}
#else
static inline void zte_ir_probe_acpi(struct spi_device *spi)
{
}
#endif

static long zte_ir_ioctl(
	struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct zte_ir_data *zte_ir;
	u32 __user *argp = (u32 __user *)(arg);
	u32 val = 0;
	int ret;
	zte_ir = filp->private_data;
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = get_user(val, argp);
		if (ret)
			return ret;
	}
	switch (cmd) {
	case LIRC_SET_SEND_CARRIER:
		pr_info("%s: carrier=%d\n", __func__, val);
		if (!val)
			return -EINVAL;
		zte_ir->speed_hz = val * 16;
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}

static int zte_ir_set_duty_cycle(struct zte_ir_data *zte_ir, u32 duty_cycle)
{
	int bits = (duty_cycle * 15) / 100;

	zte_ir->pulse = GENMASK(bits, 0);
	zte_ir->space = 0;
	return 0;
}

static ssize_t zte_ir_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	struct zte_ir_data *zte_ir;
	int i;
	int ret;
	unsigned int len = 0;
	struct spi_transfer xfer;
	unsigned int *buffer;

	/* convert the pulse/space signal to raw binary signal */
	pr_info("%s: count=%d\n", __func__, count);

	zte_ir = filp->private_data;

	mutex_lock(&zte_ir->buf_lock);
	
	buffer = memdup_user(buf, count);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		goto out_unlock;
	}

	for (i = 0; i < count/4; i++) {
		unsigned int periods;
		unsigned long long temp;
		int j;
		u16 val;
		int freq = zte_ir->speed_hz/16;

		/* periods = DIV_ROUND_CLOSEST(buffer[i] * freq, 1000000); */
		temp = (unsigned long long)buffer[i] * (unsigned long long)freq;
		periods = (temp + 500000) / 1000000;
       
		/* pr_info("%s: buffer[%d]=%d, temp = %llu, periods = %d\n", __func__, i, buffer[i], temp, periods); */

		if (len + periods >= IR_SPI_MAX_BUFSIZE) {
			pr_info("%s:len larger than %d\n", __func__, IR_SPI_MAX_BUFSIZE);
			return -EINVAL;
		}

		/*
		 * the first value in buffer is a pulse, so that 0, 2, 4, ...
		 * contain a pulse duration. On the contrary, 1, 3, 5, ...
		 * contain a space duration.
		 */
		val = (i % 2) ? zte_ir->space : zte_ir->pulse;
		for (j = 0; j < periods; j++)
			zte_ir->tx_buf[len++] = val;
	}

	memset(&xfer, 0, sizeof(xfer));

	xfer.speed_hz = zte_ir->speed_hz;
	xfer.len = len * sizeof(*zte_ir->tx_buf);
	xfer.tx_buf = zte_ir->tx_buf;

	pr_info("%s: xfer.len = %d\n", __func__, xfer.len);

	ret = spi_sync_transfer(zte_ir->spi, &xfer, 1);
	if (ret)
		dev_err(&zte_ir->spi->dev, "unable to deliver the signal,ret = %d\n", ret);

	pr_info("%s: ret=%d\n", __func__, ret);

out_unlock:
	mutex_unlock(&zte_ir->buf_lock);

	return ret ? ret : count;
}


static int zte_ir_open(struct inode *inode, struct file *filp)
{
	struct zte_ir_data *zte_ir;
	int status = -ENXIO;

	mutex_lock(&device_list_lock);

	list_for_each_entry (zte_ir, &device_list, device_entry) {
		if (zte_ir->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if (status) {
		pr_info("zte_ir: nothing for minor %d\n", iminor(inode));
		goto err_find_dev;
	}

	if (debug_enabled)
		pr_info("zte_ir: open\n");

	filp->private_data = zte_ir;
	nonseekable_open(inode, filp);

	mutex_unlock(&device_list_lock);
	return 0;

err_find_dev:
	mutex_unlock(&device_list_lock);
	return status;
}

static int zte_ir_release(struct inode *inode, struct file *filp)
{
	struct zte_ir_data *zte_ir;

	mutex_lock(&device_list_lock);
	zte_ir = filp->private_data;
	filp->private_data = NULL;

	if (debug_enabled)
		pr_info("zte_ir: release\n");

	mutex_unlock(&device_list_lock);

	return 0;
}


static const struct file_operations zte_ir_fops = {
	.owner = THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
     * gets more complete API coverage.  It'll simplify things
     * too, except for the locking.
     */
	.write = zte_ir_write,
	.unlocked_ioctl = zte_ir_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zte_ir_ioctl,
#endif
	.open = zte_ir_open,
	.release = zte_ir_release,
	.llseek = no_llseek,
};

/* Change CS_TIME for ST54 */
static int zte_ir_probe(struct spi_device *spi)
{
	struct zte_ir_data *zte_ir;
	int status;
	unsigned long minor;

	/*
	 * zte_ir should never be referenced in DT without a specific
	 * compatible string, it is a Linux implementation thing
	 * rather than a description of the hardware.
	 */

	zte_ir_probe_acpi(spi);

	/* Allocate driver data */
	zte_ir = kzalloc(sizeof(*zte_ir), GFP_KERNEL);
	if (!zte_ir)
		return -ENOMEM;

	/* Initialize the driver data */
	zte_ir->spi = spi;
	spin_lock_init(&zte_ir->spi_lock);
	mutex_init(&zte_ir->buf_lock);

	INIT_LIST_HEAD(&zte_ir->device_entry);

    /* If we can allocate a minor number, hook up this device.
     * Reusing minors is fine so long as udev or mdev is working.
     */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		zte_ir->devt = MKDEV(spidev_major, minor);
		dev = device_create(zte_ir_class, &spi->dev, zte_ir->devt,
				    zte_ir, "zte_ir");
		status = PTR_ERR_OR_ZERO(dev);
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&zte_ir->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	zte_ir->speed_hz = IR_SPI_DEFAULT_FREQUENCY * 16;
	dev_dbg(&spi->dev, "zte_ir->speed_hz=%d\n", zte_ir->speed_hz);

	zte_ir_set_duty_cycle(zte_ir, 30);

	if (status == 0)
		spi_set_drvdata(spi, zte_ir);
	else
		kfree(zte_ir);

	return status;
}

static void zte_ir_remove(struct spi_device *spi)
{
	struct zte_ir_data *zte_ir = spi_get_drvdata(spi);

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&zte_ir->spi_lock);
	zte_ir->spi = NULL;
	spin_unlock_irq(&zte_ir->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&zte_ir->device_entry);
	device_destroy(zte_ir_class, zte_ir->devt);
	clear_bit(MINOR(zte_ir->devt), minors);
	kfree(zte_ir);

	mutex_unlock(&device_list_lock);

	return;
}

static struct spi_driver zte_ir_spi_driver = {
	.driver =
		{
			.name = "zte_ir",
			.of_match_table = of_match_ptr(zte_ir_dt_ids),
			.acpi_match_table = ACPI_PTR(zte_ir_acpi_ids),
		},
	.probe = zte_ir_probe,
	.remove = zte_ir_remove,
};

static int __init zte_ir_init(void)
{
	int status;
    pr_info("Loading zte_ir driver\n");
	/* Claim our 256 reserved device numbers.  Then register a class
	 * that will key udev/mdev to add/remove /dev nodes.  Last, register
	 * the driver which manages those device numbers.
	 */
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	spidev_major =
		__register_chrdev(0, 0, N_SPI_MINORS, "spi", &zte_ir_fops);
	pr_info("Loading zte_ir driver, major: %d\n", spidev_major);

	zte_ir_class = class_create(THIS_MODULE, "zte_ir_class");
	if (IS_ERR(zte_ir_class)) {
		unregister_chrdev(spidev_major, zte_ir_spi_driver.driver.name);
		return PTR_ERR(zte_ir_class);
	}

	status = spi_register_driver(&zte_ir_spi_driver);
	if (status < 0) {
		class_destroy(zte_ir_class);
		unregister_chrdev(spidev_major, zte_ir_spi_driver.driver.name);
	}
	pr_info("Loading zte_ir driver: %d\n", status);
	return status;
}

static void __exit zte_ir_exit(void)
{
	spi_unregister_driver(&zte_ir_spi_driver);
	class_destroy(zte_ir_class);
	unregister_chrdev(spidev_major, zte_ir_spi_driver.driver.name);
}

module_init(zte_ir_init);
module_exit(zte_ir_exit);

MODULE_DESCRIPTION("PWM IR Transmitter");
MODULE_AUTHOR("xu min<xu.min4@zte.com>");
MODULE_LICENSE("GPL");