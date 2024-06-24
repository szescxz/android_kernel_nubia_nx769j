#define pr_fmt(fmt)	"ZLOG_COMM: %s: " fmt, __func__

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/miscdevice.h>

#include "zlog_exception.h"
#include "zlog_common.h"

static struct zlog_device g_zlog_server;

#define ZLOG_COMM_NAME			"zlog_comm"

void zlog_client_notify(struct zlog_client *client, int error_no)
{
	if (!g_zlog_server.init_finished) {
		pr_err("%s g_zlog_server not init_finished\n", __func__);
		return;
	}

	if (!client) {
		pr_err("%s no client to record\n", __func__);
		return;
	}

	if (client) {
		mutex_lock(&client->client_lock);

		client->error_no = error_no;
		set_bit(BUFF_STATUS_NOTIFY, &client->buff_flag);

		mutex_unlock(&client->client_lock);

		cancel_delayed_work(&g_zlog_server.handle_work);
		queue_delayed_work(g_zlog_server.handle_workqueue, &g_zlog_server.handle_work, msecs_to_jiffies(200));
	}
}
EXPORT_SYMBOL(zlog_client_notify);

int zlog_client_record(struct zlog_client *client, const char *fmt, ...)
{
	va_list ap;
	int size = 0;
	size_t avail = 0;

	if (!g_zlog_server.init_finished) {
		pr_err("%s g_zlog_server not init_finished\n", __func__);
		return 0;
	}

	if (!client) {
		pr_err("%s no client to record\n", __func__);
		return 0;
	}

	mutex_lock(&client->client_lock);

	if (client->used_size >= ZLOG_MAX_MSG_LEN) {
		pr_err("%s no buffer to record\n", __func__);
		goto out;
	}

	avail = client->buff_size - client->used_size - 1;
	va_start(ap, fmt);
	size = vsnprintf((char *)&client->dump_buff[client->used_size],
			 avail, fmt, ap);
	va_end(ap);

	if (size < 0) {
		pr_err("%s:record buffer failed\n", __func__);
		goto out;
	}

	client->used_size += size;
	if (client->used_size >= client->buff_size)
		client->used_size = client->buff_size - 1;

	set_bit(BUFF_STATUS_APPEND, &client->buff_flag);

out:
	mutex_unlock(&client->client_lock);

	return size;
}
EXPORT_SYMBOL(zlog_client_record);

static int zlog_get_free_client_safed(void)
{
	int i = 0;

	for (i = 0; i < MAX_CLIENT_CNT; i++) {
		mutex_lock(&g_zlog_server.client_list[i].client_lock);
		if (!g_zlog_server.client_list[i].initialized) {
			mutex_unlock(&g_zlog_server.client_list[i].client_lock);
			break;
		} else {
			mutex_unlock(&g_zlog_server.client_list[i].client_lock);
			continue;
		}
	}

	if (i >= MAX_CLIENT_CNT) {
		i = -EINVAL;
	}

	return i;
}

static int zlog_client_dump_by_type(int type)
{
	int i = 0;

	for (i = 0; i < MAX_CLIENT_CNT; i++) {
		if ((g_zlog_server.client_list[i].initialized)
				&& (g_zlog_server.client_list[i].module_no == type)
				&& g_zlog_server.client_list[i].cops
				&& g_zlog_server.client_list[i].cops->dump_func) {
			g_zlog_server.client_list[i].cops->dump_func((void *)&g_zlog_server.client_list[i], type);
		}

	}

	return 0;
}

static int zlog_get_already_registered_client(struct zlog_mod_info *mod_info)
{
	int i = 0;

	for (i = 0; i < MAX_CLIENT_CNT; i++) {
		mutex_lock(&g_zlog_server.client_list[i].client_lock);
		if ((g_zlog_server.client_list[i].initialized)
				&& (g_zlog_server.client_list[i].module_no == mod_info->module_no)
				&& (strncmp(g_zlog_server.client_list[i].client_name, mod_info->name,
					sizeof(g_zlog_server.client_list[i].client_name) -1) == 0)
				&& (strncmp(g_zlog_server.client_list[i].device_name, mod_info->device_name, 
					sizeof(g_zlog_server.client_list[i].device_name) -1) == 0)
				&& (strncmp(g_zlog_server.client_list[i].ic_name, mod_info->ic_name,
					sizeof(g_zlog_server.client_list[i].ic_name) -1) == 0)
				&& (strncmp(g_zlog_server.client_list[i].module_name, mod_info->module_name, 
					sizeof(g_zlog_server.client_list[i].module_name) -1) == 0)) {
			mutex_unlock(&g_zlog_server.client_list[i].client_lock);
			pr_err("%s g_zlog_server module_no:%d client_name:%s already registered.\n", 
				__func__, mod_info->module_no, mod_info->name);
			break;
		} else {
			mutex_unlock(&g_zlog_server.client_list[i].client_lock);
			continue;
		}
	}

	if (i >= MAX_CLIENT_CNT) {
		i = -EINVAL;
	}

	return i;
}

struct zlog_client *zlog_register_client(struct zlog_mod_info *mod_info)
{
	int client_pos = 0;
	struct zlog_client *ptr = NULL;

	if (!g_zlog_server.init_finished) {
		pr_err("%s g_zlog_server not init_finished\n", __func__);
		goto out;
	}

	if (!mod_info) {
		pr_err("mod_info clients is NULL\n");
		goto out;
	}

	client_pos = zlog_get_already_registered_client(mod_info);
	if (client_pos < 0) {
		pr_info("%s clients not registered, try to register.\n", mod_info->name);
	} else {
		ptr = &g_zlog_server.client_list[client_pos];
		pr_info("%s client client_id:%d already registered, return the registered client.\n", ptr->client_name, ptr->client_id);
		goto out;
	}

	client_pos = zlog_get_free_client_safed();
	if (client_pos < 0) {
		pr_err("clients register failed, reach the upper limit %d\n", MAX_CLIENT_CNT);
		goto out;
	}

	ptr = &g_zlog_server.client_list[client_pos];

	mutex_lock(&ptr->client_lock);
	ptr->dump_buff = kzalloc(ZLOG_MAX_MSG_LEN, GFP_KERNEL);
	if (!ptr->dump_buff) {
		pr_err("kzalloc dump_buff failed!!\n");
		mutex_unlock(&ptr->client_lock);
		ptr = NULL;
		goto out;
	}

	ptr->buff_size = ZLOG_MAX_MSG_LEN;
	ptr->read_size = 0;
	ptr->used_size = 0;

	strncpy(ptr->client_name, mod_info->name, ZLOG_CLIENT_NAME_LEN - 1);
	ptr->client_name[ZLOG_CLIENT_NAME_LEN - 1] = '\0';

	if (mod_info->device_name) {
		strncpy(ptr->device_name,
			mod_info->device_name,
			ZLOG_MAX_DEVICE_NAME_LEN - 1);
		ptr->device_name[ZLOG_MAX_DEVICE_NAME_LEN - 1] = '\0';
	}

	if (mod_info->ic_name) {
		strncpy(ptr->ic_name,
			mod_info->ic_name,
			ZLOG_MAX_IC_NAME_LEN - 1);
		ptr->ic_name[ZLOG_MAX_IC_NAME_LEN - 1] = '\0';
	}

	if (mod_info->module_name) {
		strncpy(ptr->module_name,
			mod_info->module_name,
			ZLOG_MAX_MODULE_NAME_LEN - 1);
		ptr->module_name[ZLOG_MAX_MODULE_NAME_LEN - 1] = '\0';
	}

	ptr->module_no = mod_info->module_no;
	ptr->client_id = client_pos;
	ptr->cops = mod_info->fops;
	ptr->initialized = 1;

	set_bit(BUFF_STATUS_READY, &ptr->buff_flag);
	mutex_unlock(&ptr->client_lock);
	pr_info("client %s client_id:%d register success\n", ptr->client_name, ptr->client_id);

out:
	return ptr;
}
EXPORT_SYMBOL(zlog_register_client);

void zlog_unregister_client(struct zlog_client *client)
{
	int client_pos = 0;

	if (!g_zlog_server.init_finished) {
		pr_err("%s g_zlog_server not init_finished\n", __func__);
		goto out;
	}

	client_pos = client->client_id;
	if (client_pos < 0) {
		pr_err("clients unregister failed, client_id %d\n", client_pos);
		goto out;
	}

	mutex_lock(&client->client_lock);

	if(client->dump_buff)
		kfree(client->dump_buff);

	client->buff_size = ZLOG_MAX_MSG_LEN;
	client->read_size = 0;
	client->used_size = 0;

	memset(client->client_name, 0, sizeof(client->client_name));

	memset(client->device_name, 0, sizeof(client->device_name));

	memset(client->ic_name, 0, sizeof(client->ic_name));

	memset(client->module_name, 0, sizeof(client->module_name));

	memset(client->device_name, 0, sizeof(client->device_name));


	client->client_id = 0;
	client->cops = NULL;
	client->initialized = 0;

	set_bit(BUFF_STATUS_NOT_INIT, &client->buff_flag);
	mutex_unlock(&client->client_lock);
	pr_info("client %d unregister success\n", client_pos);

out:
	return;
}
EXPORT_SYMBOL(zlog_unregister_client);

void zlog_reset_client(struct zlog_client *client)
{
	if (!g_zlog_server.init_finished) {
		pr_err("%s g_zlog_server not init_finished\n", __func__);
		return;
	}

	if (!client) {
		pr_err("client is null\n");
		return;
	}

	mutex_lock(&client->client_lock);

	client->used_size = 0;
	client->read_size = 0;
	client->error_no = 0;
	memset(client->dump_buff, 0, client->buff_size);
	clear_bit(BUFF_STATUS_READY, &client->buff_flag);

	mutex_unlock(&client->client_lock);
}
EXPORT_SYMBOL(zlog_reset_client);

static int zlog_create_event(struct zlog_client *client, struct zlog_event *zevent)
{
	mutex_lock(&client->client_lock);

	zevent->event_buff_len = client->used_size + ZLOG_MODULE_NO_LEN * 2 + strlen(ZLOG_TIME_STAMP_LEN) * 2
		+ ZLOG_ERROR_NO_LEN * 2 + ZLOG_CLIENT_NAME_LEN  * 2	+ ZLOG_MAX_DEVICE_NAME_LEN  * 2
		+ ZLOG_MAX_MODULE_NAME_LEN  * 2 + ZLOG_MAX_IC_NAME_LEN  * 2 +  ZLOG_CONTENT_NO_LEN;

	mutex_unlock(&client->client_lock);

	zevent->event_buff = kzalloc(zevent->event_buff_len, GFP_KERNEL);
	if (!zevent->event_buff) {
		pr_err("kmalloc event_buff failed\n");
		return -ENOMEM;
	}

	return 0;
}

static int get_time_str(char *output)
{
    struct  timespec64 now;
    struct rtc_time tm;

    ktime_get_real_ts64(&now);

    now.tv_sec -= 60*sys_tz.tz_minuteswest;

    /* Calculate the ktime such as YYMMDD in tm */
    rtc_time64_to_tm(now.tv_sec, &tm);

    /*printk("zlog get_time_str: [%04d-%02d-%02d %02d:%02d:%02d]\n"
		, tm.tm_year + 1900
        , tm.tm_mon + 1
        , tm.tm_mday
        , tm.tm_hour
        , tm.tm_min
        , tm.tm_sec);*/

    return sprintf(output, "[%04d-%02d-%02d %02d:%02d:%02d]"
        , tm.tm_year + 1900
        , tm.tm_mon + 1
        , tm.tm_mday
        , tm.tm_hour
        , tm.tm_min
        , tm.tm_sec);
}

static int zlog_format_msg(struct zlog_client *client, struct zlog_event *zevent)
{
	int buff_used = 0;
	char tmpbuf[128] = { 0 };
	int ret_len = 0;

	memset(tmpbuf, 0, sizeof(tmpbuf));
	mutex_lock(&client->client_lock);

	ret_len = get_time_str(tmpbuf);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "MOD_NO: %d; ", client->module_no);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used,
				"ENO: 0x%x; ", ZLOG_COMB_MODULE_ID(client->module_no) | client->error_no);

	if(ret_len > 0) {
		tmpbuf[ret_len] = '\0';
		buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "TIME: %s; ", tmpbuf);
	}

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "CLT_NAME: %s; ", client->client_name);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "DEV_NAME: %s; ", client->device_name);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "MOD_NAME: %s; ", client->module_name);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "IC_NAME: %s; ", client->ic_name);

	buff_used += snprintf(zevent->event_buff + buff_used, zevent->event_buff_len - buff_used, "CONTENT: ");

	memcpy(zevent->event_buff + buff_used, client->dump_buff, client->used_size);

	zevent->event_used_size = buff_used + client->used_size;

	mutex_unlock(&client->client_lock);

	return 0;
}

static void zlog_report_event(struct zlog_client *client)
{
	struct zlog_event zevent;
	int rc= 0;

	rc = zlog_create_event(client, &zevent);
	if (rc < 0) {
		pr_err("zlog_create_event failed\n");
		return;
	}

	rc = zlog_format_msg(client, &zevent);
	if (rc < 0) {
		pr_err("zlog_format_msg failed\n");
		goto out;
	}

	rc = zlog_write_internal(zevent.event_buff, zevent.event_used_size);
	if (rc < 0) {
		pr_err("zlog_write_internal failed\n");
		goto out;
	}

out:
	kfree(zevent.event_buff);
	zevent.event_used_size = 0;
	zevent.event_buff_len = 0;
}

static void zlog_handle_work(struct work_struct *work)
{
	int i;
	struct zlog_client *client = NULL;
	pr_info("%s enter\n", __func__);

	for (i = 0; i < MAX_CLIENT_CNT; i++) {

		if (g_zlog_server.client_list[i].initialized) {
			mutex_lock(&g_zlog_server.client_list[i].client_lock);
			pr_info("No.%d client name %s flag 0x%lx\n", i,
				      g_zlog_server.client_list[i].client_name,
				      g_zlog_server.client_list[i].buff_flag);
			if (!test_and_clear_bit(BUFF_STATUS_NOTIFY, &g_zlog_server.client_list[i].buff_flag)) {
				mutex_unlock(&g_zlog_server.client_list[i].client_lock);
				continue;
			}

			mutex_unlock(&g_zlog_server.client_list[i].client_lock);

			client = &g_zlog_server.client_list[i];
			zlog_report_event(client);
			pr_info("%s finish notify\n", client->client_name);
			zlog_reset_client(client);
		}
	}

	pr_info("%s exit\n", __func__);
}

/*
 * zlog_open - the log's open() file operation
 *
 * Note how near a no-op this is in the write-only case. Keep it that way!
 */
static int zlog_comm_open(struct inode *inode, struct file *file)
{

	atomic_inc(&g_zlog_server.use_cnt);

	pr_err("open successed, use_cnt %d", atomic_read(&g_zlog_server.use_cnt));

	return 0;
}

/*
 * zlog_release - the log's release file operation
 *
 * Note this is a total no-op in the write-only case. Keep it that way!
 */
static int zlog_comm_release(struct inode *ignored, struct file *file)
{

	atomic_dec(&g_zlog_server.use_cnt);

	pr_err("close successed, use_cnt %d", atomic_read(&g_zlog_server.use_cnt));

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
static ssize_t zlog_comm_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{

	return count;
}

static ssize_t zlog_comm_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	zlog_cmd_t zlog_cmd;

	if (sizeof(zlog_cmd_t) != count) {
		pr_err("count(%d) != zlog_cmd_t(%d)\n", count, sizeof(zlog_cmd_t));
		return -EINVAL;
	}

	if (copy_from_user(&zlog_cmd, buf, sizeof(zlog_cmd_t))) {
		pr_err("copy from user failed\n");
		return -ENOMEM;
	}

	zlog_client_dump_by_type(zlog_cmd.tag);

	return count;
}

static long zlog_comm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	return 0;
}


static const struct file_operations zlog_comm_fops = {
	.owner = THIS_MODULE,
	.read = zlog_comm_read,
	.write = zlog_comm_write,
	.unlocked_ioctl = zlog_comm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zlog_comm_ioctl,
#endif
	.open = zlog_comm_open,
	.release = zlog_comm_release,
};

static int __init zlog_comm_create_ctrl_dev(struct zlog_device *zlog_info)
{
	int ret = 0;

	zlog_info->misc.minor = MISC_DYNAMIC_MINOR;
	zlog_info->misc.name = kstrdup(ZLOG_COMM_NAME, GFP_KERNEL);
	if (!zlog_info->misc.name) {
		ret = -ENOMEM;
		goto out_free_log;
	}

	zlog_info->misc.fops = &zlog_comm_fops;
	zlog_info->misc.parent = NULL;

	/* finally, initialize the misc device for this log */
	ret = misc_register(&zlog_info->misc);
	if (unlikely(ret)) {
		pr_err("failed to register misc device for log '%s'!\n",
		       zlog_info->misc.name);
		goto out_free_log;
	}

	pr_info("created zlog '%s'\n", zlog_info->misc.name);

	return 0;

out_free_log:

	return ret;
}

static int __init zlog_common_init(void)
{
	int i = 0;

	memset(&g_zlog_server, 0, sizeof(struct zlog_device));
	g_zlog_server.init_finished = false;

	for (i = 0; i < MAX_CLIENT_CNT; i++) {
		mutex_init(&g_zlog_server.client_list[i].client_lock);
	}

	zlog_comm_create_ctrl_dev(&g_zlog_server);

	g_zlog_server.handle_workqueue = create_singlethread_workqueue("zlog_handle_service");
	INIT_DELAYED_WORK(&g_zlog_server.handle_work, zlog_handle_work);

	atomic_set(&g_zlog_server.use_cnt, 0);
	g_zlog_server.init_finished = true;

	pr_info("%s called\n", __func__);

	return 0;
}

fs_initcall_sync(zlog_common_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("zlog common");
MODULE_AUTHOR("zte.com.cn");

