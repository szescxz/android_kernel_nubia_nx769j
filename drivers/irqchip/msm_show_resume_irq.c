// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011, 2014-2016, 2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <trace/hooks/gic_v3.h>
#include <trace/hooks/cpuidle_psci.h>
#include <linux/notifier.h>
#include <linux/suspend.h>

#define MPIDR_RS(mpidr) (((mpidr) & 0xF0UL) >> 4)
#define GIC_LINE_NR min(GICD_TYPER_SPIS(gic_data->rdists.gicd_typer), 1020U)
#define gic_data_rdist() (this_cpu_ptr(gic_data->rdists.rdist))
#define gic_data_rdist_rd_base() (gic_data_rdist()->rd_base)
#define gic_data_rdist_sgi_base() (gic_data_rdist_rd_base() + SZ_64K)


#include <linux/signal.h>
#include <linux/sched/task.h>
#include <trace/hooks/signal.h>

#include <linux/cgroup.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/sched/jobctl.h>
#ifndef __GENKSYMS__

#include <../drivers/android/binder_internal.h>
#include <linux/sched.h>
#include <uapi/linux/android/binder.h>
#endif
#include <trace/hooks/binder.h>
//#include <../../kernel/sched/sched.h>

#include <../../drivers/android/binder_alloc.h>
#ifndef ZTE_FEATURE_CGROUP_FREEZER_V2
#define ZTE_FEATURE_CGROUP_FREEZER_V2            true
#endif

static void __iomem *base;
static int msm_show_resume_irq_mask = 1;/*zte_pm add 10252021*/
module_param_named(debug_mask, msm_show_resume_irq_mask, int, 0664);


#ifdef ZTE_FEATURE_CGROUP_FREEZER_V2

#define UID_SIZE	100
#define STATE_SIZE	10
#define MIN_FREE_ASYNC_BINDER_BUFFER_SIZE (50 * 1024)
static struct device *dev = NULL;
static int state = 1;
static struct workqueue_struct *unfreeze_eventqueue = NULL;
static struct send_event_data
{
	char *type;
	unsigned int uid;
	unsigned int pid;
	struct work_struct sendevent_work;
} *wsdata;


enum freezer_state_flags {
	CGROUP_FREEZER_ONLINE	= (1 << 0), /* freezer is fully online */
	CGROUP_FREEZING_SELF	= (1 << 1), /* this freezer is freezing */
	CGROUP_FREEZING_PARENT	= (1 << 2), /* the parent freezer is freezing */
	CGROUP_FROZEN		= (1 << 3), /* this and its descendants frozen */

	/* mask for all FREEZING flags */
	CGROUP_FREEZING		= CGROUP_FREEZING_SELF | CGROUP_FREEZING_PARENT,
};

struct freezer {
	struct cgroup_subsys_state	css;
	unsigned int			state;
};


static inline struct freezer *css_freezer(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct freezer, css) : NULL;
}

static inline struct freezer *task_freezer(struct task_struct *task)
{
	return css_freezer(task_css(task, freezer_cgrp_id));
}


bool cgroup_freezing(struct task_struct *task)
{
	bool ret;

	rcu_read_lock();
	ret = task_freezer(task)->state & (CGROUP_FREEZING | CGROUP_FROZEN);
	rcu_read_unlock();

	return ret;
}

bool cgroup_is_cgroup2_freezing(struct task_struct *task)
{
	bool ret = false;
	struct cgroup *task_cgp = NULL;
	task_cgp = task_dfl_cgroup(task);
	if (task_cgp != NULL) {
		ret = task_cgp->freezer.freeze;
	}
	return ret;
}

static void sendevent_handler(struct work_struct *work)
{
	struct send_event_data *temp = container_of(work, struct send_event_data, sendevent_work);
	char buf[UID_SIZE] = {0};
	char *envp[2] = {buf, NULL};
	char *type = NULL;
	int uid = 0;
	int pid = 0;

	uid = temp->uid;
	type = temp->type;
	pid = temp->pid;

	if (pid != 0) {
		snprintf(buf, UID_SIZE, "%sUID=%u:%u", type, uid, pid);
	} else {
		snprintf(buf, UID_SIZE, "%sUID=%u", type, uid);
	}
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	kfree(temp);
	temp = NULL;
	pr_info("have send event uid is %u, reason is %s\n", uid, type);
}
void send_unfreeze_event_withpid(char *type, unsigned int uid, unsigned int pid)
{
	pr_info("Need send event uid is %u, reason is %s\n", uid, type);
	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}
	wsdata = kzalloc(sizeof(struct send_event_data), GFP_ATOMIC);
	if (wsdata == NULL) {
		pr_err("send event malloc workqueue data is error!!!\n");
		return;
	}
	wsdata->type = type;
	wsdata->uid = uid;
	wsdata->pid = pid;
	INIT_WORK(&(wsdata->sendevent_work), sendevent_handler);
	queue_work(unfreeze_eventqueue, &(wsdata->sendevent_work));
}

void send_unfreeze_event(char *type, unsigned int uid)
{
	/* pr_info("Need send event uid is %u, reason is %s\n", uid, type); */

	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}

	wsdata = kzalloc(sizeof(struct send_event_data), GFP_ATOMIC);
	if (wsdata == NULL) {
		pr_err("send event malloc workqueue data is error!!!\n");
		return;
	}
	wsdata->type = type;
	wsdata->uid = uid;
	INIT_WORK(&(wsdata->sendevent_work), sendevent_handler);
	queue_work(unfreeze_eventqueue, &(wsdata->sendevent_work));
}

static void send_unfreeze_event_test(char *buf)
{
	char *s_c[2] = {buf, NULL};

	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}
	send_unfreeze_event("KILL", 1000);
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, s_c);
}



static ssize_t unfreeze_show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("show unfreeze event state is %d\n", state);
	return snprintf(buf, STATE_SIZE, "%d\n", state);
}

static ssize_t unfreeze_set_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	size_t ret = -EINVAL;

	ret = kstrtoint(buf, STATE_SIZE, &state);
	if (ret < 0)
		return ret;

	pr_info("set unfreeze event state is %d\n", state);
	return count;
}

static ssize_t send(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char unfreeze_uid[UID_SIZE] = {0};

	snprintf(unfreeze_uid, sizeof(unfreeze_uid), "UID=%s", buf);
	send_unfreeze_event_test(unfreeze_uid);
	pr_info("send unfreeze event %s\n", unfreeze_uid);
	return count;
}



static struct class unfreeze_event_class = {
	.name = "cpufreezer",
	.owner = THIS_MODULE,
};
#endif

static bool hibernation;
struct gic_chip_data_v3 *gic_data_glb;

struct gic_chip_data_ds {
	unsigned int enabled_irqs[32];
	unsigned int active_irqs[32];
	unsigned int irq_edg_lvl[64];
	unsigned int ppi_edg_lvl;
	unsigned int enabled_sgis;
	unsigned int pending_sgis;
};

static struct gic_chip_data_ds gic_data_ds __read_mostly;

static void gic_suspend_ds(void *data, struct gic_chip_data_v3 *gic_data)
{
	int i;
	void __iomem *rdist_base = gic_data_rdist_sgi_base();

	gic_data_glb = gic_data;

	if (unlikely(!hibernation))
		return;
	gic_data_ds.enabled_sgis = readl_relaxed(rdist_base + GICD_ISENABLER);
	gic_data_ds.pending_sgis = readl_relaxed(rdist_base + GICD_ISPENDR);
	/* Store edge level for PPIs by reading GICR_ICFGR1 */
	gic_data_ds.ppi_edg_lvl = readl_relaxed(rdist_base + GICR_ICFGR0 + 4);

	for (i = 0; i * 32 < GIC_LINE_NR; i++) {
		gic_data_ds.enabled_irqs[i] = readl_relaxed(base + GICD_ISENABLER + i * 4);
		gic_data_ds.active_irqs[i] = readl_relaxed(base + GICD_ISPENDR + i * 4);
	}

	for (i = 2; i < GIC_LINE_NR / 16; i++)
		gic_data_ds.irq_edg_lvl[i] = readl_relaxed(base + GICD_ICFGR + i * 4);
}

static void gic_resume_ds(struct gic_chip_data_v3 *gic_data)
{
	int i;
	void __iomem *rdist_base = gic_data_rdist_sgi_base();

	pr_info("Re-initializing gic in hibernation restore\n");
	gic_v3_dist_init();
	gic_v3_cpu_init();
	writel_relaxed(gic_data_ds.enabled_sgis, rdist_base + GICD_ISENABLER);
	writel_relaxed(gic_data_ds.pending_sgis, rdist_base + GICD_ISPENDR);
	/* Restore edge and level triggers for PPIs from GICR_ICFGR1 */
	writel_relaxed(gic_data_ds.ppi_edg_lvl, rdist_base + GICR_ICFGR0 + 4);
	/* Restore edge and level triggers */
	for (i = 2; i < GIC_LINE_NR / 16; i++)
		writel_relaxed(gic_data_ds.irq_edg_lvl[i], base + GICD_ICFGR + i * 4);
	gic_v3_dist_wait_for_rwp();
	/* Activate and enable interrupts from backup */
	for (i = 0; i * 32 < GIC_LINE_NR; i++) {
		writel_relaxed(gic_data_ds.active_irqs[i], base + GICD_ISPENDR + i * 4);
		writel_relaxed(gic_data_ds.enabled_irqs[i], base + GICD_ISENABLER + i * 4);
	}
	gic_v3_dist_wait_for_rwp();
}


static void msm_show_resume_irqs(void)
{
	unsigned int i;
	u32 enabled;
	u32 pending[32];
	u32 gic_line_nr;
	u32 typer;

	if (unlikely(hibernation))
		gic_resume_ds(gic_data_glb);

	if (!msm_show_resume_irq_mask)
		return;

	typer = readl_relaxed(base + GICD_TYPER);
	gic_line_nr = min(GICD_TYPER_SPIS(typer), 1023u);

	for (i = 0; i * 32 < gic_line_nr; i++) {
		enabled = readl_relaxed(base + GICD_ICENABLER + i * 4);
		pending[i] = readl_relaxed(base + GICD_ISPENDR + i * 4);
		pending[i] &= enabled;
	}

	for (i = find_first_bit((unsigned long *)pending, gic_line_nr);
	     i < gic_line_nr;
	     i = find_next_bit((unsigned long *)pending, gic_line_nr, i + 1)) {

		if (i < 32)
			continue;

		pr_warn("%s: HWIRQ %u\n", __func__, i);
	}
}

static int gic_suspend_notifier(struct notifier_block *nb, unsigned long event, void *dummy)
{
	if (event == PM_HIBERNATION_PREPARE)
		hibernation = true;
	else if (event == PM_POST_HIBERNATION)
		hibernation = false;
	return NOTIFY_OK;
}

static struct notifier_block gic_notif_block = {
	.notifier_call = gic_suspend_notifier,
};

static atomic_t cpus_in_s2idle;

static void gic_s2idle_enter(void *unused, struct cpuidle_device *dev, bool s2idle)
{
	if (!s2idle)
		return;

	atomic_inc(&cpus_in_s2idle);
}

static void gic_s2idle_exit(void *unused, struct cpuidle_device *dev, bool s2idle)
{
	if (!s2idle)
		return;

	if (atomic_read(&cpus_in_s2idle) == num_online_cpus())
		msm_show_resume_irqs();

	atomic_dec(&cpus_in_s2idle);
}

static struct syscore_ops gic_syscore_ops = {
	.resume = msm_show_resume_irqs,
};

#ifdef ZTE_FEATURE_CGROUP_FREEZER_V2

static DEVICE_ATTR(test, S_IRUGO|S_IWUSR, NULL, send);
static DEVICE_ATTR(state, S_IRUGO|S_IWUSR, unfreeze_show_state, unfreeze_set_state);


static const struct attribute *unfreeze_event_attr[] = {
	&dev_attr_test.attr,
	&dev_attr_state.attr,
	NULL,
};
void signal_catch_for_freeze(void *data, int sig, struct task_struct *killer, struct task_struct *dst)
{
	if (sig == SIGQUIT || sig == SIGABRT || sig == SIGKILL || sig == SIGSEGV) {
		if(cgroup_freezing(dst)) {
			// send_unfreeze_event("KILL", (unsigned int)(dst->real_cred->uid.val));
			send_unfreeze_event_withpid("KILL", (unsigned int)(dst->real_cred->uid.val), (unsigned int)(dst->pid));
			pr_warn("send_unfreeze_event signal = %d  %d  %d \n", sig ,dst->real_cred->uid.val ,dst->pid);
		} else if (cgroup_is_cgroup2_freezing(dst)) {
			send_unfreeze_event("KILL", (unsigned int)(dst->real_cred->uid.val));
			pr_warn("send_unfreeze_event cgroup2 signal = %d  %d  %d\n", sig, dst->real_cred->uid.val, dst->pid);
		}
	}
	if ((sig == SIGSTOP || sig == SIGTSTP) && NULL != dst) {
		pr_warn("SIGSTOP SIGTSTP signal = %d  %d  %d\n", sig, dst->real_cred->uid.val, dst->pid);
	}
}
static const struct attribute_group unfreeze_event_attr_group = {
	.attrs = (struct attribute **) unfreeze_event_attr,
};

void binder_catch_for_freeze_notify(void *data, struct binder_proc *proc, struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	struct task_struct *binder_proc_task = NULL;

	if (proc != NULL) {
		binder_proc_task = proc->tsk;
	}
	if (binder_proc_task != NULL && binder_proc_task->real_cred->uid.val > 10000 && sync && (cgroup_is_cgroup2_freezing(binder_proc_task) || cgroup_freezing(binder_proc_task))) {
		send_unfreeze_event("BINDER", (unsigned int)(binder_proc_task->real_cred->uid.val));
	}

}


void binder_trans_for_freeze_notify(void *data, struct binder_proc *target_proc, struct binder_proc *proc, struct binder_thread *thread, struct binder_transaction_data *tr)
{
	if ((target_proc != NULL) && (target_proc->tsk != NULL) && (target_proc->tsk->real_cred->uid.val > 10000) && (tr != NULL) && (tr->flags & 0x01) && (cgroup_is_cgroup2_freezing(target_proc->tsk) || cgroup_freezing(target_proc->tsk))) {
		struct binder_alloc *alloc = (struct binder_alloc *)&target_proc->alloc;

		if (alloc != NULL) {
			size_t free = alloc->free_async_space;
			//pr_warn("binder_trans_for_freeze_notify: target_uid=%d, free=%d, min=%d\n", target_proc->tsk->real_cred->uid.val, free, MIN_FREE_ASYNC_BINDER_BUFFER_SIZE);
			if (free <= MIN_FREE_ASYNC_BINDER_BUFFER_SIZE) {
				pr_warn("send_unfreeze_event uid=%d  insufficient free_async_mem=%d\n", target_proc->tsk->real_cred->uid.val, free);
				send_unfreeze_event("BINDER", (unsigned int)target_proc->tsk->real_cred->uid.val);
			}
		}
	}
}

#endif
static int msm_show_resume_probe(struct platform_device *pdev)
{
#ifdef ZTE_FEATURE_CGROUP_FREEZER_V2
	int ret = 0;

	pr_info("cpufreezer uevent init\n");

	ret = class_register(&unfreeze_event_class);
	if (ret < 0) {
		pr_err("cpufreezer unfreezer event: class_register failed!!!\n");
		return ret;
	}
	dev = device_create(&unfreeze_event_class, NULL, MKDEV(0, 0), NULL, "unfreezer");
	if (IS_ERR(dev)) {
		pr_err("cpufreezer:device_create failed!!!\n");
		ret = IS_ERR(dev);
		goto unregister_class;
	}
	ret = sysfs_create_group(&dev->kobj, &unfreeze_event_attr_group);
	if (ret < 0) {
		pr_err("cpufreezer:sysfs_create_group failed!!!\n");
		goto destroy_device;
	}

	unfreeze_eventqueue = create_workqueue("send_unfreeze_event");
	if (unfreeze_eventqueue == NULL) {
		pr_err("unfreeze event module could not create workqueue!!!");
		ret = -ENOMEM;
		goto destroy_device;
	}
	register_trace_android_vh_do_send_sig_info(signal_catch_for_freeze, NULL);
	register_trace_android_vh_binder_proc_transaction_finish(binder_catch_for_freeze_notify, NULL);
	register_trace_android_vh_binder_trans(binder_trans_for_freeze_notify, NULL);
	goto normal_class;

destroy_device:
	device_destroy(&unfreeze_event_class, MKDEV(0, 0));
unregister_class:
	class_unregister(&unfreeze_event_class);
normal_class:
#endif


	base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR(base)) {
		pr_err("%pOF: error %d: unable to map GICD registers\n",
				pdev->dev.of_node, PTR_ERR(base));
		return -ENXIO;
	}


	register_trace_prio_android_vh_cpuidle_psci_enter(gic_s2idle_enter, NULL, INT_MAX);
	register_trace_prio_android_vh_cpuidle_psci_exit(gic_s2idle_exit, NULL, INT_MAX);
	register_syscore_ops(&gic_syscore_ops);
	register_pm_notifier(&gic_notif_block);
	register_trace_android_vh_gic_v3_suspend(gic_suspend_ds, NULL);
	return 0;
}

static int msm_show_resume_remove(struct platform_device *pdev)
{
	unregister_trace_android_vh_cpuidle_psci_enter(gic_s2idle_enter, NULL);
	unregister_trace_android_vh_cpuidle_psci_exit(gic_s2idle_exit, NULL);
	unregister_syscore_ops(&gic_syscore_ops);
	iounmap(base);
	return 0;
}

static const struct of_device_id msm_show_resume_match_table[] = {
	{ .compatible = "qcom,show-resume-irqs" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_show_resume_match_table);

static struct platform_driver msm_show_resume_dev_driver = {
	.probe  = msm_show_resume_probe,
	.remove = msm_show_resume_remove,
	.driver = {
		.name = "show-resume-irqs",
		.of_match_table = msm_show_resume_match_table,
	},
};
module_platform_driver(msm_show_resume_dev_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. MSM Show resume IRQ");
MODULE_LICENSE("GPL");
