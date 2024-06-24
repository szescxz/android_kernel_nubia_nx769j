#ifndef _NBIA_W
#define _NBIA_W
#include "ringbuffer.h"
#include "../../../fs/proc/internal.h"
#include <linux/sched/walt.h>
#include <linux/sched/prio.h>
#include <linux/jump_label.h>
#include "../sched.h"

#include <linux/cgroup.h>

#define TASK_DEMAND_BOOST_COUNT 4 
#define DEMAD_INCREASE_INDEX_MAX 6 
#define NBIA_DP_VALUE_LEN 512
#define NBIA_DP_ARRAY_LEN 12
#define NUM_WAKER_BUCKETS 3 
#define NUM_RENDER_TID_ARRAY_SIZE 2

extern unsigned int sysctl_sched_nbia_dp;
extern unsigned int sysctl_sched_nbia_dp_array[NBIA_DP_ARRAY_LEN];
extern struct ctl_table nbia_table[];
extern struct ctl_table nbia_base_table[];

static DEFINE_MUTEX(sysfs_store_lock);
static DEFINE_MUTEX(r_buffer_lock);
static RingBuffer *r_buffer = NULL;

void nbia_init(void);
u32 nbia_task_demand_boost(struct task_struct *p, u32 orig_pred_demand);
void nbia_android_rvh_wakeup_success(struct task_struct *prev,
		struct task_struct *next);
void nbia_wakeup_new_task(void *unused, struct task_struct *new);
bool nbia_update_cpus_allowed(void *unused, struct task_struct *p,
						cpumask_var_t cpus_requested,
						const struct cpumask *new_mask, int *ret);
void nbia_fork_init(struct task_struct *p);
void q_affinity_work(int tid, struct cpumask *new_mask);
unsigned int get_early_up_migrate(int index);
unsigned int get_early_down_migrate(int index);
#endif /* _NBIA_W */

