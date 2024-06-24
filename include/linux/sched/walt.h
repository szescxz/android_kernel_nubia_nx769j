/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_SCHED_WALT_H
#define _LINUX_SCHED_WALT_H

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/cpumask.h>

enum pause_client {
	PAUSE_CORE_CTL	= 0x01,
	PAUSE_THERMAL	= 0x02,
	PAUSE_HYP	= 0x04,
	PAUSE_SBT	= 0x08,
};

#define NO_BOOST 0
#define FULL_THROTTLE_BOOST 1
#define CONSERVATIVE_BOOST 2
#define RESTRAINED_BOOST 3
#define STORAGE_BOOST 4
#define FULL_THROTTLE_BOOST_DISABLE -1
#define CONSERVATIVE_BOOST_DISABLE -2
#define RESTRAINED_BOOST_DISABLE -3
#define STORAGE_BOOST_DISABLE -4
#define MAX_NUM_BOOST_TYPE (STORAGE_BOOST+1)

#if IS_ENABLED(CONFIG_SCHED_WALT)

#define MAX_CPUS_PER_CLUSTER 6
#define MAX_CLUSTERS 4

struct core_ctl_notif_data {
	unsigned int nr_big;
	unsigned int coloc_load_pct;
	unsigned int ta_util_pct[MAX_CLUSTERS];
	unsigned int cur_cap_pct[MAX_CLUSTERS];
};

enum task_boost_type {
	TASK_BOOST_NONE = 0,
	TASK_BOOST_ON_MID,
	TASK_BOOST_ON_MAX,
	TASK_BOOST_STRICT_MAX,
	TASK_BOOST_END,
};

#define WALT_NR_CPUS 8
#define RAVG_HIST_SIZE 5
/* wts->bucket_bitmask needs to be updated if NUM_BUSY_BUCKETS > 16 */
#define NUM_BUSY_BUCKETS 16
#define NUM_BUSY_BUCKETS_SHIFT 4

struct walt_related_thread_group {
	int			id;
	raw_spinlock_t		lock;
	struct list_head	tasks;
	struct list_head	list;
	bool			skip_min;
	struct rcu_head		rcu;
	u64			last_update;
	u64			downmigrate_ts;
	u64			start_ktime_ts;
};
//sched_class_persisted begin
enum scpt_state {
        DEFAULT_SCPT_S = 0,
	IS_SCPT,
	NOT_SCPT,
        NEED_RESTORE,
};
//sched_class_persised end

struct walt_task_struct {
	/*
	 * 'mark_start' marks the beginning of an event (task waking up, task
	 * starting to execute, task being preempted) within a window
	 *
	 * 'sum' represents how runnable a task has been within current
	 * window. It incorporates both running time and wait time and is
	 * frequency scaled.
	 *
	 * 'sum_history' keeps track of history of 'sum' seen over previous
	 * RAVG_HIST_SIZE windows. Windows where task was entirely sleeping are
	 * ignored.
	 *
	 * 'demand' represents maximum sum seen over previous
	 * sysctl_sched_ravg_hist_size windows. 'demand' could drive frequency
	 * demand for tasks.
	 *
	 * 'curr_window_cpu' represents task's contribution to cpu busy time on
	 * various CPUs in the current window
	 *
	 * 'prev_window_cpu' represents task's contribution to cpu busy time on
	 * various CPUs in the previous window
	 *
	 * 'curr_window' represents the sum of all entries in curr_window_cpu
	 *
	 * 'prev_window' represents the sum of all entries in prev_window_cpu
	 *
	 * 'pred_demand_scaled' represents task's current predicted cpu busy time
	 * in terms of 1024 units
	 *
	 * 'busy_buckets' groups historical busy time into different buckets
	 * used for prediction
	 *
	 * 'demand_scaled' represents task's demand scaled to 1024
	 *
	 * 'prev_on_rq' tracks enqueue/dequeue of a task for error conditions
	 * 0 = nothing, 1 = enqueued, 2 = dequeued
	 */
	u32				flags;
	u64				mark_start;
	u64				window_start;
	u32				sum, demand;
	u32				coloc_demand;
	u32				sum_history[RAVG_HIST_SIZE];
	u16				sum_history_util[RAVG_HIST_SIZE];
	u32				curr_window_cpu[WALT_NR_CPUS];
	u32				prev_window_cpu[WALT_NR_CPUS];
	u32				curr_window, prev_window;
	u8				busy_buckets[NUM_BUSY_BUCKETS];
	u16				bucket_bitmask;
	u16				demand_scaled;
	u16				pred_demand_scaled;
	u64				active_time;
	u64				last_win_size;
	int				boost;
	bool				wake_up_idle;
	bool				misfit;
	bool				rtg_high_prio;
	u8				low_latency;
	u64				boost_period;
	u64				boost_expires;
	u64				last_sleep_ts;
	u32				init_load_pct;
	u32				unfilter;
	u64				last_wake_ts;
	u64				last_enqueued_ts;
	struct walt_related_thread_group __rcu	*grp;
	struct list_head		grp_list;
	u64				cpu_cycles;
	bool				iowaited;
	int				prev_on_rq;
	int				prev_on_rq_cpu;
	struct list_head		mvp_list;
	u64				sum_exec_snapshot_for_slice;
	u64				sum_exec_snapshot_for_total;
	u64				total_exec;
	int				mvp_prio;
	int				cidx;
	int				load_boost;
	int64_t				boosted_task_load;
	int				prev_cpu;
	int				new_cpu;
	u8				enqueue_after_migration;
	u8				hung_detect_status;
	int				pipeline_cpu;
	cpumask_t			reduce_mask;
	u64				mark_start_birth_ts;
	u8				high_util_history;
        //nubia add begin
        /**
        * 在每一次wakeup_sucess发生的时候,last_pid跟prev_last_pid
        * 都会进行记录。记录规则如下。
        * 变量使用的地方在nbia_walt.c的wakeup_sucess函数 
        * wakeup_sucess(*prev, *next)
        * 更新规则如下:
        *            ___________
        *         b:|__?__|__?__|
        *         c:|__b__|__?__|
        *         d:|__c__|__b__|
        *         e:|__d__|__c__|
        *         f:|__e__|__d__|
        *         g:|__f__|__e__|
        *        me:|__g__|__f__|
        *            l_pid pl_pid
        *
        * me指的是目标进程的render_tid
        * last_pid:唤醒当前rtid的waker的线程tid
        * prev_last_pid:唤醒[唤醒rtid的waker]的线程tid
        * l_sexec_ts: last sum exec time
        * pl_sexec_ts: prev last sum exec time
        * 这样就能实现往前追溯调用链。
        **/
	pid_t				l_pid; //last_pid
	pid_t				pl_pid;//prev_last_pid
        u64                             l_sexec_ts;
        u64                             pl_sexec_ts;
        /*
        * 采样过期时间
        * ringitem_build_expires = sched_clock() + RINGITME_BUILD_RATE
        * sched_clock指的是wakeup发生的时刻。
        *
        */
        u64 ringitem_build_expires;
        //affinity 拦截机制
        /*
        * is_app，是否是三方应用
        * persist_caller，记录是哪条usrspace线程发起的affinity持久化要求
        * affinity_persisted, 是否是affinity属性持久化的线程；如果有
        *     被usrspace强制要affinity持久化；该值为true。否则为false。
        * last，记录除了机制节点设置的affinity外，系统任意其它通道对
        *       task_struct的cpu_mask的值。包括cpuset
        * req，记录usrspace要设置的affinity值，理论上除了这个值，全系统包括
        *     kernel对这条线程的修改只能是这个值
        */
        bool is_app;
        bool mask_persisted;
        int persist_caller;
        struct cpumask req;
	struct cpumask last;
        //affinity 拦截机制
        //sched_class_persisted begin
        enum scpt_state scp_s;
        int old_prio;
        u64 scpt_token;
        //sched_class_persised end
        //nubia add end

};

/*
 * enumeration to set the flags variable
 * each index below represents an offset into
 * wts->flags
 */
enum walt_flags {
	WALT_INIT,
	MAX_WALT_FLAGS
};

#define wts_to_ts(wts) ({ \
		void *__mptr = (void *)(wts); \
		((struct task_struct *)(__mptr - \
			offsetof(struct task_struct, android_vendor_data1))); })

static inline bool sched_get_wake_up_idle(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->wake_up_idle;
}

static inline int sched_set_wake_up_idle(struct task_struct *p, bool wake_up_idle)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	wts->wake_up_idle = wake_up_idle;
	return 0;
}

static inline void set_wake_up_idle(bool wake_up_idle)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) current->android_vendor_data1;

	wts->wake_up_idle = wake_up_idle;
}

extern int sched_lpm_disallowed_time(int cpu, u64 *timeout);
extern int set_task_boost(int boost, u64 period);

struct notifier_block;
extern void core_ctl_notifier_register(struct notifier_block *n);
extern void core_ctl_notifier_unregister(struct notifier_block *n);
extern int core_ctl_set_boost(bool boost);
extern void walt_set_cpus_taken(struct cpumask *set);
extern void walt_unset_cpus_taken(struct cpumask *unset);
extern cpumask_t walt_get_cpus_taken(void);
extern void walt_get_cpus_in_state1(struct cpumask *cpus);

extern int walt_pause_cpus(struct cpumask *cpus, enum pause_client client);
extern int walt_resume_cpus(struct cpumask *cpus, enum pause_client client);
extern int walt_partial_pause_cpus(struct cpumask *cpus, enum pause_client client);
extern int walt_partial_resume_cpus(struct cpumask *cpus, enum pause_client client);
extern int sched_set_boost(int type);
#else
static inline int sched_lpm_disallowed_time(int cpu, u64 *timeout)
{
	return INT_MAX;
}
static inline int set_task_boost(int boost, u64 period)
{
	return 0;
}

static inline bool sched_get_wake_up_idle(struct task_struct *p)
{
	return false;
}

static inline int sched_set_wake_up_idle(struct task_struct *p, bool wake_up_idle)
{
	return 0;
}

static inline void set_wake_up_idle(bool wake_up_idle)
{
}

static inline int core_ctl_set_boost(bool boost)
{
	return 0;
}

static inline void core_ctl_notifier_register(struct notifier_block *n)
{
}

static inline void core_ctl_notifier_unregister(struct notifier_block *n)
{
}

static inline int walt_pause_cpus(struct cpumask *cpus, enum pause_client client)
{
	return 0;
}
static inline int walt_resume_cpus(struct cpumask *cpus, enum pause_client client)
{
	return 0;
}

inline int walt_partial_pause_cpus(struct cpumask *cpus, enum pause_client client)
{
	return 0;
}
inline int walt_partial_resume_cpus(struct cpumask *cpus, enum pause_client client)
{
	return 0;
}

static inline void walt_set_cpus_taken(struct cpumask *set)
{
}

static inline void walt_unset_cpus_taken(struct cpumask *unset)
{
}

static inline cpumask_t walt_get_cpus_taken(void)
{
	cpumask_t t = { CPU_BITS_NONE };
	return t;
}

static inline int sched_set_boost(int type)
{
	return -EINVAL;
}
#endif

#endif /* _LINUX_SCHED_WALT_H */
