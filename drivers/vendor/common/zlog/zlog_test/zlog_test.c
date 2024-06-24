/*
 * zlog_test.c
 *
 * the file is for dsm test
 *
 * Copyright (c) 2015-2019 ZTE Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

 #define pr_fmt(fmt)	"ZLOG_TEST: %s: " fmt, __func__


#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/sched/rt.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <vendor/common/zte_misc.h>

#include "zlog_exception.h"
#include "zlog_common.h"


#define DSM_TEST_BUFF		16
#define DSM_TEST_DEVICE_SUM	5
#define DEV_ONE_BUFF_SIZE	500
#define DEV_TWO_BUFF_SIZE	1024
#define DEV_THREE_BUFF_SIZE	10240
#define DEV_FOUR_BUFF_SIZE	300
#define DEV_FIVE_BUFF_SIZE	500
#define DSM_RANDOM_ODD_RESULT	0
#define DSM_RANDOM_EVEN_RESULT	5
#define DSM_EVEN_NUM            2
#define DSM_SCHEDULE_TIMEOUT    10

static struct workqueue_struct *zlog_test1_workqueue;
static struct delayed_work zlog_test1_work;
static struct workqueue_struct *zlog_test2_workqueue;
static struct delayed_work zlog_test2_work;
static struct workqueue_struct *zlog_test3_workqueue;
static struct delayed_work zlog_test3_work;
static struct workqueue_struct *zlog_test4_workqueue;
static struct delayed_work zlog_test4_work;
static struct workqueue_struct *zlog_test5_workqueue;
static struct delayed_work zlog_test5_work;
static int zlog_enable_test_flag = 0;

static int poll(void)
{
	unsigned int random;
	int result;

	random = get_random_u32();
	result = (random % DSM_EVEN_NUM) ?
		 DSM_RANDOM_ODD_RESULT : DSM_RANDOM_EVEN_RESULT;
	pr_info("%s enter random num %u, result %d\n",
		     __func__, random, result);
	return result;
}

static int dump(void *data, int type)
{
	struct zlog_client *client = (struct zlog_client *)data;
	int used_size = 0;

	pr_info("%s called, type %d\n", __func__, type);
	zlog_client_record(client, "dumpdumpdumpdumpdumpdump1\n");
	zlog_client_record(client, "dumpdumpdumpdumpdumpdump2\n");
	zlog_client_record(client, "dumpdumpdumpdumpdumpdump3\n");
	zlog_client_record(client, "dumpdumpdumpdumpdumpdump4\n");
	zlog_client_record(client, "dumpdumpdumpdumpdumpdump5\n");
	zlog_client_notify(client, ZLOG_CHG_DUMP_NO);
	pr_info("%s zlog_client_notify\n", client->client_name);

	return used_size;
}



static struct zlog_client_ops ops = {
	.poll_state = poll,
	.dump_func = dump,
};

static struct zlog_mod_info test_dev_1 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "test_dev_1",
	.device_name = "TP",
	.ic_name = "Synopsys",
	.module_name = "Syn2322",
	.fops = &ops,
};

static struct zlog_mod_info test_dev_2 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "test_dev_2",
	.device_name = "CHGING",
	.ic_name = "mt6360",
	.module_name = "mtk",
	.fops = &ops,
};

static struct zlog_mod_info test_dev_3 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "test_dev_3",
	.device_name = "FG",
	.ic_name = "bq27z561",
	.module_name = "cosmx",
	.fops = &ops,
};

static struct zlog_mod_info test_dev_4 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "test_dev_4",
	.device_name = "CP",
	.ic_name = "bq25970",
	.module_name = "TI",
	.fops = &ops,
};

static struct zlog_mod_info test_dev_5 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "test_dev_5",
	.device_name = "CHG_DET",
	.ic_name = "wt6670",
	.module_name = "wt",
	.fops = &ops,
};

static struct zlog_client *client_1 = NULL;
static struct zlog_client *client_2 = NULL;
static struct zlog_client *client_3 = NULL;
static struct zlog_client *client_4 = NULL;
static struct zlog_client *client_5 = NULL;

static void zlog_test1_thread(struct work_struct *work)
{
	unsigned int random = 0;

	random = get_random_u32();
	random = random % DSM_TEST_DEVICE_SUM;

	zlog_client_record(client_1, "111111111111111111111\n");
	zlog_client_record(client_1, "222222222222222222222\n");
	zlog_client_record(client_1, "333333333333333333333\n");
	zlog_client_record(client_1, "444444444444444444444\n");
	zlog_client_record(client_1, "555555555555555555555\n");
	zlog_client_notify(client_1, DSM_TEST_DEVICE_SUM);
	pr_info("%s zlog_client_notify\n", client_1->client_name);

	queue_delayed_work(zlog_test1_workqueue, &zlog_test1_work, msecs_to_jiffies(random * 1000));
}

static void zlog_test2_thread(struct work_struct *work)
{
	unsigned int random = 0;

	random = get_random_u32();
	random = random % DSM_TEST_DEVICE_SUM;

	zlog_client_record(client_2, "1010101010101010101010\n");
	zlog_client_record(client_2, "2020202020202020202020\n");
	zlog_client_record(client_2, "3030303030303030303030\n");
	zlog_client_record(client_2, "4040404040404040404040\n");
	zlog_client_record(client_2, "5050505050505050505050\n");
	zlog_client_notify(client_2, DSM_TEST_DEVICE_SUM);
	pr_info("%s zlog_client_notify\n", client_2->client_name);

	queue_delayed_work(zlog_test2_workqueue, &zlog_test2_work, msecs_to_jiffies(random * 1000));
}

static void zlog_test3_thread(struct work_struct *work)
{
	unsigned int random = 0;

	random = get_random_u32();
	random = random % DSM_TEST_DEVICE_SUM;

	zlog_client_record(client_3, "AAAAAAAAAAAAAAAAAAAAA1\n");
	zlog_client_record(client_3, "BBBBBBBBBBBBBBBBBBBBB2\n");
	zlog_client_record(client_3, "CCCCCCCCCCCCCCCCCCCCC3\n");
	zlog_client_record(client_3, "DDDDDDDDDDDDDDDDDDD4\n");
	zlog_client_record(client_3, "EEEEEEEEEEEEEEEEEEEEEEE5\n");
	zlog_client_notify(client_3, DSM_TEST_DEVICE_SUM);
	pr_info("%s zlog_client_notify\n", client_3->client_name);

	queue_delayed_work(zlog_test3_workqueue, &zlog_test3_work, msecs_to_jiffies(random * 1000));
}

static void zlog_test4_thread(struct work_struct *work)
{
	unsigned int random = 0;

	random = get_random_u32();
	random = random % DSM_TEST_DEVICE_SUM;

	zlog_client_record(client_4, "Kaka is an amazing palyer\n");
	zlog_client_record(client_4, "Ronaldo is an amazing palyer\n");
	zlog_client_record(client_4, "Ronney is an amazing palyer\n");
	zlog_client_record(client_4, "Henny is an amazing palyer\n");
	zlog_client_record(client_4, "Veri is an amazing palyer\n");
	zlog_client_notify(client_4, DSM_TEST_DEVICE_SUM);
	pr_info("%s zlog_client_notify\n", client_4->client_name);

	queue_delayed_work(zlog_test4_workqueue, &zlog_test4_work, msecs_to_jiffies(random * 1000));
}

static void zlog_test5_thread(struct work_struct *work)
{
	unsigned int random = 0;

	random = get_random_u32();
	random = random % DSM_TEST_DEVICE_SUM;

	zlog_client_record(client_5, "National derby:\n");
	zlog_client_record(client_5, "Real Madrid vs  Barcelona\n");
	zlog_client_record(client_5, "Manchester United vs Chelsea\n");
	zlog_client_record(client_5, "AC milan vs Juventus\n");
	zlog_client_record(client_5, "Bayern Munich vs Hertha BSC\n");
	zlog_client_notify(client_5, DSM_TEST_DEVICE_SUM);
	pr_info("%s zlog_client_notify\n", client_5->client_name);

	queue_delayed_work(zlog_test5_workqueue, &zlog_test5_work, msecs_to_jiffies(random * 1000));
}

static int zlog_enable_test_set(const char *val, const void *arg)
{
	int tmp = 0, ret = 0;
	arg = (arg) ? arg : NULL;

	ret = sscanf(val, "%d", &tmp);
	if (ret != 1) {
		pr_info("para is invalid\n");
		return -EINVAL;
	}

	pr_info("zlog_enable_test_set %d\n", !!tmp);

	if (zlog_enable_test_flag == !!tmp) {
		pr_info("zlog_enable_test_flag alreay is %d\n", zlog_enable_test_flag);
		return 0;
	}

	zlog_enable_test_flag = !!tmp;

	if (zlog_enable_test_flag) {

		client_1 = zlog_register_client(&test_dev_1);
		if (!client_1)
			pr_err("dsm1 reg failed\n");

		client_2 = zlog_register_client(&test_dev_2);
		if (!client_2)
			pr_err("dsm2 reg failed\n");

		client_3 = zlog_register_client(&test_dev_3);
		if (!client_3)
			pr_err("dsm3 reg failed\n");

		client_4 = zlog_register_client(&test_dev_4);
		if (!client_4)
			pr_err("dsm4 reg failed\n");

		client_5 = zlog_register_client(&test_dev_5);
		if (!client_5)
			pr_err("dsm5 reg failed\n");

		queue_delayed_work(zlog_test1_workqueue, &zlog_test1_work, msecs_to_jiffies(0));
		queue_delayed_work(zlog_test2_workqueue, &zlog_test2_work, msecs_to_jiffies(0));
		queue_delayed_work(zlog_test3_workqueue, &zlog_test3_work, msecs_to_jiffies(0));
		queue_delayed_work(zlog_test4_workqueue, &zlog_test4_work, msecs_to_jiffies(0));
		queue_delayed_work(zlog_test5_workqueue, &zlog_test5_work, msecs_to_jiffies(0));
	} else {
		flush_delayed_work(&zlog_test1_work);
		cancel_delayed_work_sync(&zlog_test1_work);
		flush_delayed_work(&zlog_test2_work);
		cancel_delayed_work_sync(&zlog_test2_work);
		flush_delayed_work(&zlog_test3_work);
		cancel_delayed_work_sync(&zlog_test3_work);
		flush_delayed_work(&zlog_test4_work);
		cancel_delayed_work_sync(&zlog_test4_work);
		flush_delayed_work(&zlog_test5_work);
		cancel_delayed_work_sync(&zlog_test5_work);

		zlog_unregister_client(client_1);
		zlog_unregister_client(client_2);
		zlog_unregister_client(client_3);
		zlog_unregister_client(client_4);
		zlog_unregister_client(client_5);

		client_1 = NULL;
		client_2 = NULL;
		client_3 = NULL;
		client_4 = NULL;
		client_5 = NULL;
	}

	return 0;
}

static int zlog_enable_test_get(char *val, const void *arg)
{
	arg = (arg) ? arg : NULL;

	return snprintf(val, PAGE_SIZE, "%d", zlog_enable_test_flag);
}

struct zte_misc_ops zlog_enable_test_node = {
	.node_name = "zlog_enable_test",
	.set = zlog_enable_test_set,
	.get = zlog_enable_test_get,
	.free = NULL,
	.arg = NULL,
};

static int __init zlog_test_init(void)
{
	int ret = 0;

	zlog_test1_workqueue = create_singlethread_workqueue("zlog_test1_workqueue");
	INIT_DELAYED_WORK(&zlog_test1_work, zlog_test1_thread);

	zlog_test2_workqueue = create_singlethread_workqueue("zlog_test2_workqueue");
	INIT_DELAYED_WORK(&zlog_test2_work, zlog_test2_thread);

	zlog_test3_workqueue = create_singlethread_workqueue("zlog_test3_workqueue");
	INIT_DELAYED_WORK(&zlog_test3_work, zlog_test3_thread);

	zlog_test4_workqueue = create_singlethread_workqueue("zlog_test4_workqueue");
	INIT_DELAYED_WORK(&zlog_test4_work, zlog_test4_thread);

	zlog_test5_workqueue = create_singlethread_workqueue("zlog_test5_workqueue");
	INIT_DELAYED_WORK(&zlog_test5_work, zlog_test5_thread);

	ret = zte_misc_register_callback(&zlog_enable_test_node, NULL);
	if (ret < 0) {
		pr_info("%s zlog_enable_test_node register error ret :%d\n", __func__, ret);	
	}

	pr_info("%s end\n", __func__);

	return 0;
}

static void __exit zlog_test_exit(void)
{
	pr_info("driver remove finished\n");
}

module_init(zlog_test_init);
module_exit(zlog_test_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("zlog test");
MODULE_AUTHOR("zte.com.cn");

