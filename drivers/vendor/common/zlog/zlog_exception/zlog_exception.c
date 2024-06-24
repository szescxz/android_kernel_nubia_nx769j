#define pr_fmt(fmt)	"ZLOG_EXCEPTION: %s: " fmt, __func__

#include <linux/alarmtimer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>

#include "zlog_exception.h"

#define ZLOG_NAME			"zlog_exception"

#define MAX_MSG_CNT			1024

#ifndef MIN
#define MIN(a, b)       ((a < b) ? (a) : (b))
#endif

struct zlog_msg_t {
	struct list_head list;
	unsigned int log_length;
	char * log_data;
};

struct zlog_info_t {
	struct mutex list_mutex;
	struct list_head log_list_head;
	struct miscdevice misc;
	atomic_t use_cnt;
	wait_queue_head_t poll_wq;
	int list_flag;
	int list_cnt;
};

struct zlog_info_t g_zlog_info;

/*
 * zlog_open - the log's open() file operation
 *
 * Note how near a no-op this is in the write-only case. Keep it that way!
 */
static int zlog_open(struct inode *inode, struct file *file)
{

	atomic_inc(&g_zlog_info.use_cnt);

	pr_err("open successed, use_cnt %d", atomic_read(&g_zlog_info.use_cnt));

	return 0;
}

/*
 * zlog_release - the log's release file operation
 *
 * Note this is a total no-op in the write-only case. Keep it that way!
 */
static int zlog_release(struct inode *ignored, struct file *file)
{

	atomic_dec(&g_zlog_info.use_cnt);

	pr_err("close successed, use_cnt %d", atomic_read(&g_zlog_info.use_cnt));

	return 0;
}


/*
 * zlog_read - our log's read() method
 *
 * Behavior:
 *
 *	- O_NONBLOCK works
 *	- If there are no log entries to read, blocks until log is written to
 *	- Atomically reads exactly one log entry
 *
 * Will set errno to EINVAL if read
 * buffer is insufficient to hold next entry.
 */
static ssize_t zlog_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	struct zlog_msg_t *log_msg = NULL;
	int retval = 0, is_empty = 0;

	mutex_lock(&g_zlog_info.list_mutex);
	is_empty = list_empty(&g_zlog_info.log_list_head);
	mutex_unlock(&g_zlog_info.list_mutex);

	if (is_empty) {
		g_zlog_info.list_flag = false;
		/*pr_info("####### wait -------------!!!\n");*/
		retval = wait_event_interruptible(g_zlog_info.poll_wq, g_zlog_info.list_flag);
		if (retval) {
			pr_err("wait event interruptible failed\n");
			retval = -EAGAIN;
			goto exit_loop;
		}
	}

	mutex_lock(&g_zlog_info.list_mutex);
	
	if (!list_empty(&g_zlog_info.log_list_head)) {
		log_msg = list_last_entry(&g_zlog_info.log_list_head,
										struct zlog_msg_t, list);

		list_del(&(log_msg->list));
		g_zlog_info.list_cnt--;
	}

	mutex_unlock(&g_zlog_info.list_mutex);

	if (!log_msg) {
		pr_err("log_msg is null\n");
		retval = -EAGAIN;
		goto exit_loop;
	}

	count = MIN(count, log_msg->log_length);

	if (copy_to_user(buf, log_msg->log_data, count)) {
		pr_err("%s copy_to_user failed!\n", __func__);
		retval = -EFAULT;
		goto exit_loop;
	}

exit_loop:
	if (log_msg && log_msg->log_data)
		kfree(log_msg->log_data);

	if (log_msg)
		kfree(log_msg);

	return (retval < 0) ? retval :  count;
}

static ssize_t zlog_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct zlog_msg_t *log_msg = NULL;
	int retval = 0;

	if (g_zlog_info.list_cnt > MAX_MSG_CNT) {
		pr_err("zlog cache is full\n");
		retval = -ENOMEM;
		goto exit_loop;
	}

	log_msg = kzalloc(sizeof(struct zlog_msg_t), GFP_KERNEL);
	if (log_msg == NULL) {
		pr_err("falied to kzalloc fragment info\n");
		retval = -ENOMEM;
		goto exit_loop;
	}

	/*filling length*/
	log_msg->log_length = count + 1;

	/*filling data*/
	log_msg->log_data = kzalloc(log_msg->log_length, GFP_KERNEL);
	if (log_msg->log_data == NULL) {
		pr_err("falied to vzalloc fragment info\n");
		goto exit_loop;
	}

	if (copy_from_user(log_msg->log_data, buf, count)) {
		pr_err("copy from user failed\n");
		retval = -ENOMEM;
		goto exit_loop;
	}

	log_msg->log_data[count] = '\0';

	mutex_lock(&g_zlog_info.list_mutex);
	list_add(&(log_msg->list), &g_zlog_info.log_list_head);
	g_zlog_info.list_flag = true;
	g_zlog_info.list_cnt++;
	mutex_unlock(&g_zlog_info.list_mutex);
	
	wake_up_interruptible(&g_zlog_info.poll_wq);

	return count;

exit_loop:
	if (log_msg && log_msg->log_data)
		kfree(log_msg->log_data);

	if (log_msg)
		kfree(log_msg);

	return retval;
}

ssize_t zlog_write_internal(const char *buf, size_t count)
{
	struct zlog_msg_t *log_msg = NULL;
	int retval = 0;

	if (g_zlog_info.list_cnt > MAX_MSG_CNT) {
		pr_err("zlog cache is full\n");
		retval = -ENOMEM;
		goto exit_loop;
	}

	log_msg = kzalloc(sizeof(struct zlog_msg_t), GFP_KERNEL);
	if (log_msg == NULL) {
		pr_err("falied to kzalloc fragment info\n");
		retval = -ENOMEM;
		goto exit_loop;
	}

	/*filling length*/
	log_msg->log_length = count + 1;

	/*filling data*/
	log_msg->log_data = kzalloc(log_msg->log_length, GFP_KERNEL);
	if (log_msg->log_data == NULL) {
		pr_err("falied to vzalloc fragment info\n");
		goto exit_loop;
	}

	memcpy(log_msg->log_data, buf, count);

	log_msg->log_data[count] = '\0';

	mutex_lock(&g_zlog_info.list_mutex);
	list_add(&(log_msg->list), &g_zlog_info.log_list_head);
	g_zlog_info.list_flag = true;
	g_zlog_info.list_cnt++;
	mutex_unlock(&g_zlog_info.list_mutex);
	
	wake_up_interruptible(&g_zlog_info.poll_wq);

	return count;

exit_loop:
	if (log_msg && log_msg->log_data)
		kfree(log_msg->log_data);

	if (log_msg)
		kfree(log_msg);

	return retval;
}
EXPORT_SYMBOL(zlog_write_internal);

/*
 * zlog_poll - the log's poll file operation, for poll/select/epoll
 *
 * Note we always return POLLOUT, because you can always write() to the log.
 * Note also that, strictly speaking, a return value of POLLIN does not
 * guarantee that the log is readable without blocking, as there is a small
 * chance that the writer can lap the reader in the interim between poll()
 * returning and the read() request.
 */
static unsigned int zlog_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	if (!(file->f_mode & FMODE_READ))
		return mask;

	poll_wait(file, &g_zlog_info.poll_wq, wait);

	mutex_lock(&g_zlog_info.list_mutex);
	
	if (!list_empty(&g_zlog_info.log_list_head))
		mask = POLLIN | POLLRDNORM;

	mutex_unlock(&g_zlog_info.list_mutex);

	return mask;
}

static long zlog_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	return 0;
}


static const struct file_operations zlog_fops = {
	.owner = THIS_MODULE,
	.read = zlog_read,
	.write = zlog_write,
	.poll = zlog_poll,
	.unlocked_ioctl = zlog_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zlog_ioctl,
#endif
	.open = zlog_open,
	.release = zlog_release,
};

static int __init zlog_create_log_dev(struct zlog_info_t *zlog_info)
{
	int ret = 0;

	zlog_info->misc.minor = MISC_DYNAMIC_MINOR;
	zlog_info->misc.name = kstrdup(ZLOG_NAME, GFP_KERNEL);
	if (!zlog_info->misc.name) {
		ret = -ENOMEM;
		goto out_free_log;
	}

	zlog_info->misc.fops = &zlog_fops;
	zlog_info->misc.parent = NULL;

	/* finally, initialize the misc device for this log */
	ret = misc_register(&zlog_info->misc);
	if (unlikely(ret)) {
		pr_err("failed to register misc device for log '%s'!\n",
		       zlog_info->misc.name);
		goto out_free_log;
	}

	pr_info("created zlog '%s', ret =%d\n", zlog_info->misc.name, ret);
	return 0;

out_free_log:

	return ret;
}


static int __init zlog_init(void)
{
	int ret = 0;

	ret = zlog_create_log_dev(&g_zlog_info);
	if (unlikely(ret)) {
		pr_err("failed to create misc device for zlog !\n");
		goto out_free_log;
	}

	init_waitqueue_head(&g_zlog_info.poll_wq);

	mutex_init(&g_zlog_info.list_mutex);

	INIT_LIST_HEAD(&g_zlog_info.log_list_head);

	atomic_set(&g_zlog_info.use_cnt, 0);

	pr_info("zlog_init driver finished\n");

out_free_log:
	return ret;
}

static void __exit zlog_exit(void)
{
	pr_info("driver remove finished\n");
}


fs_initcall(zlog_init);
module_exit(zlog_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("zte.charger <zte.charger@zte.com.cn>");
MODULE_DESCRIPTION("Charge policy Service Driver");

