#include "walt.h"
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/cred.h>
#include <trace/hooks/sched.h>

#define RINGBUFFER_SIZE_MAX 10000
#define RINGITME_ADD_RATE_TIME 1000000000
#define PRIO_ADJUSTED_TID_ARRAY_SIZE 100 
#define FIRST_APPLICATION_UID 10000
#define MAX_USER_RT_PRIO 100
//1分钟
#define MAX_ACTIVE_TMS 60000
unsigned int sysctl_sched_nbia_dp;
unsigned int sysctl_sched_nbia_dp_array[NBIA_DP_ARRAY_LEN];

struct kobject *nbia_kobj;

/**
* sched_nbia_render_tid_array[0] 目标线程pid
* sched_nbia_render_tid_array[1] render thread tid
**/
int sched_nbia_render_tid_array[NUM_RENDER_TID_ARRAY_SIZE];
int sched_nbia_debug;
int sched_nbia_class_persisted_debug;
int sched_nbia_queuework_enable;
//sched_nbia_rate_tns RingBufer addItem的时间间隔；即采样周期
//默认值是0，最大值是1秒(RINGITME_ADD_RATE_TIME)
int sched_nbia_rate_tns;

//解决线程优先级继承问题
int sched_prio_adjusted_info[PRIO_ADJUSTED_TID_ARRAY_SIZE][5];
int ring_array_index;
bool prio_adj_info_data_changed = false;
//解决线程优先级继承问题

/*
* CMaskBundle 封装Affinity拦截机制的数据
* tid，usrspace通过节点告知的要对哪条tid进行拦截
* persist_caller，在usrspace发起读写节点的线程
* req，request_cpumask usrspace对tid要设置
*    的cpumask。任何线程对tid的修改都不会生效这个rq_value
*    始终对tid生效。
* req_dec，对应req的十进制值
* last_value，记录usrspace上层通过affinity接口对tid
*    线程设置的cpumask。
*/
typedef struct {
   int tid;
   u8 req_dec;
} CMaskBundle;
/*
* 每次set_cpus_allowed_comm函数回调的时候，在机制开启
* 的情况下都会执行一个kwork，AffinityWork_Args
* tid，此次被设置new mask的线程编号
* new_mask，此次该线程被设置的mask
*/
typedef struct {
     int tid;
     struct cpumask new_mask;
     struct kthread_work work;
} AffinityWork_Args;
CMaskBundle cmask_bundles[PRIO_ADJUSTED_TID_ARRAY_SIZE];
int bundles_array_index;
//Affinity拦截机制

//kthread
struct kthread_worker	worker;
struct task_struct	*thread;
bool kthread_running = false;

/*
*识别负重线程思路:跟目标进程的上帧线程q_t，有调用关系&&运行时间最长
*1，跟踪q_t调用关系思路 
*   _     _     _     _     _     _     _     _     _     
*  |c|   |f|   |c|   |f|   |c|   |f|   |c|   |c|   |c|
*  |b|   |e|   |b|   |e|   |b|   |e|   |b|   |b|   |b| 
*  |a|   |d|   |a|   |a|   |a|   |d|   |a|   |a|   |a|
*  ----------------------------------------------------> Time
*  |                       3s                        |
*
*  框框中的字幕代表待用关系，即c->b->a;每300ms采样一次。
*   
*  每次采用都会将这种调用关系用RB_item的结构记录 
*  rb_itme的声明见ringbuffer.h
* 
*  上面的例子中，明显可以看到c->b->a这种调用关系出现频次最高。
*  得出这种调用关系需要通过计数的方式得出。
*  为此设计了一个size为8的循环链表。(8 * 300ms = 2.4s，uspace预设
*   每隔3s来kernel获取一次负重线程的频次)
*     _________    _________     _________    _________    _________ 
* |->|_rb_item_|->|_rb_item_|->|_rb_item_|->|_rb_item_|->|_rb_item_|-|
* |                                                                  |
* |   _________ _________ ________                                   |
* |--|_rb_item_|_rb_item_|_rb_item_|<--------------------------------|
*
* 当uspace来获取负重线程的时候，就会对链表做一次统计；统计出出现频次最
* 高的调用组合。
* 然后对出现的调用组合的几个线程按照执行时间从大到小排列之后，将3个线程的
* tid反馈给usrspace。
*
*/
//根据游戏线程名字，将关键线程修改为rt-begin
/*
*机制名称:sched_class_persisted
*机制思路:
* 当目标游戏进程内的线程发生wakeup行为的时候，
* 如果被唤醒的线程是我们关心的关键线程。
*  并且它最近一次执行的时间在usrspace下发的间隔时间内 
*  如果它不是rt这一类sched_class，那么将它设置成为rt。
*
*         |           32ms                |
*  -------|-----------|-------------------|----------->
*                     |              发生wakeup行为
*                     |                  now
*             t1在这个时间点执行过
*
*如图例:
*   t1->sched_info.last_arrival > now-32ms & t1->se.sum_exec_runtime > 32ms
* 
*有几个可以辅助判断目标线程是否是我们关心的线程的位置:
* 目标进程fork子线程的时候，可以直接判断新产生的子线程是否是我们关注的线程。
* 在目标进程内有任何线程来操作affinity接口的时候。
* 这几个地方的辅助判断可以减轻wakeup的时候的工作量。
*
* uspace需要告诉我的信息:
* > 游戏进程号
* > 关键子线程名字
* > 线程活跃间隔时间(预设32ms,60帧-2帧时间)
* > 机制日志开关
*
* sched_class_persisted             格式:pid@tname-size@tname@tname@tname
* sched_class_persisted_active_tms  毫秒值
* sched_class_persisted_debug       0-关 or 1-开
*
*  struct scp_data tid_comms:
*   *tid_comms----> _______ *next  _______ *next  _______ *next  _______
*                  |UnityT0|----->|UnityT1|----->|UnityT2|----->|UnityT3|
*                  |_______|      |_______|      |_______|      |_______|
*                  |WorkT0 |*next  _______ *next  _______ 
*                  |_______|----->|WorkT1 |----->|WorkT2 |
*                  |GfxT   |      |_______|      |_______|
*                  |_______|
*/
//scp_udata:sched_class_persist_usrspace_data
/*enum scpt_state {
        DEFAULT_SCPT_S = 0,
	IS_SCPT,
	NOT_SCPT,
};*/
#define TNAME_MAX_SIZE 10
typedef struct 
{
  char comm[TASK_COMM_LEN];
  int sched_policy;
  int priority;
} scp_tcomm_item;
typedef struct {
     int game_pid;
     int tcomm_sz;
     scp_tcomm_item tid_comms[TNAME_MAX_SIZE];
     u64 active_tms;
     u64 token;
     bool debug;
} scp_data;
typedef struct {
   int tid;
   struct kthread_work work;
} scp_kwork_data;

scp_data scp_udata;
static DEFINE_MUTEX(scp_data_lock);
//over write walt early updown migration begin
unsigned int sched_early_up_migrate[WALT_NR_CPUS];
unsigned int sched_early_down_migrate[WALT_NR_CPUS];
static DEFINE_MUTEX(ud_value_data_lock);
//over write walt early updown migration end 
/*
* 如果上层修改了某个线程的sched_class跟prio，那么这个线程在
*进行fork子线程的时候，就会将这种变化继承过去。
*因此有了restore_ts_cfs，用于恢复子线程的sched_class跟prio跟未修改sched_class跟prio
*之前的parent一样。因此有了restore_task_to_cfs_work相关的代码
*!这个恢复子线程的动作不能在父线程的线程上下文环境下进行。
*/
typedef struct {
   int tid;
   int prio;
   struct kthread_work work;
} restore_ts_cfs_data;

static void restore_task_to_cfs_work(struct kthread_work *work)
{
  restore_ts_cfs_data *data_item = container_of(work, restore_ts_cfs_data, work);
  struct task_struct *p = NULL;
  if(!data_item){
    printk(KERN_INFO "[restore_task_to_cfs_work] restore_ts_cfs_data is null!\n");
    return;
  }
  //符合cfs调度策略的线程的优先级的值在101-140之间，默认是120
  if(data_item->prio > MAX_PRIO || data_item->prio <= MAX_USER_RT_PRIO)
  {
     printk(KERN_INFO "[restore_task_to_cfs_work] thread priority value invalid!\n");
     goto out;
  }
  p = get_pid_task(find_vpid(data_item->tid),PIDTYPE_PID);
  if(!p){
    if(scp_udata.debug) {
       printk(KERN_INFO "[restore_task_to_cfs_work] %d task_struct not found!\n", data_item->tid);
    }
    goto out;
  }
  if(scp_udata.debug) {
     printk(KERN_INFO "[restore_task_to_cfs_work] target_task:%d sched_class:%d \n", data_item->tid, p->policy);
  }
  sched_set_normal(p, PRIO_TO_NICE(data_item->prio));     
  if(scp_udata.debug) {
      printk(KERN_INFO "[restore_task_to_cfs_work] sched_set_normal set success target_task:%d sched_class:%d \n", data_item->tid, p->policy);
  }
  put_task_struct(p);
out:
  kfree(data_item);
}
void q_restore_cfs_task(int tid, int prio){
  restore_ts_cfs_data *data_item = NULL;
  int queue_w_result = 0;
  data_item = kzalloc(sizeof(restore_ts_cfs_data), GFP_KERNEL);
  if(!data_item){
    if(scp_udata.debug) {
      printk(KERN_INFO "[q_restore_cfs_task]----restore_ts_cfs_data kzalloc failed! task:%d\n", tid); 
    }
    return;
  }
  data_item->tid = tid; 
  data_item->prio = prio; 
  if(kthread_running){
     kthread_init_work(&data_item->work, restore_task_to_cfs_work);
     queue_w_result = kthread_queue_work(&worker, &data_item->work);
     if(!queue_w_result){
       if(scp_udata.debug) {
          printk(KERN_INFO "[q_restore_cfs_task]----queue work failed! task:%d\n", tid); 
       }
       kfree(data_item);
     }
     if(scp_udata.debug) printk(KERN_INFO "[q_restore_cfs_task]----queue work success! task:%d\n", tid); 
  }else{
     kfree(data_item);
  }
}
static void scp_work(struct kthread_work *work)
{
  scp_kwork_data *data_item = container_of(work, scp_kwork_data, work);
  struct sched_param sp;
  struct walt_task_struct *wts = NULL;
  struct task_struct *p = NULL;
  int tid_comms_index = 0;
  u64 now = sched_clock();
  bool is_scpt = false;
  if(!data_item){
    if(scp_udata.debug) {
        printk(KERN_INFO "[scp_work] scp_kwork_data is null!\n");
    }
    return;
  }
  mutex_lock(&scp_data_lock);
  p = get_pid_task(find_vpid(data_item->tid),PIDTYPE_PID);
  if(!p){
    if(scp_udata.debug) {
       printk(KERN_INFO "[scp_work] %d task_struct not found!\n", data_item->tid);
    }
    goto out;
  }
  wts = (struct walt_task_struct *) p->android_vendor_data1;
  if(wts){
      if(scp_udata.debug) {
        printk(KERN_INFO "[scp_work] target_task:%d game_pid:%d task-tgid:%d task-token:%llu scp_udata_token:%llu \n", data_item->tid, scp_udata.game_pid
         , p->tgid, wts->scpt_token, scp_udata.token);
      }
      if(scp_udata.game_pid && scp_udata.game_pid == p->tgid 
        && !(scp_udata.token && (scp_udata.token == wts->scpt_token))){
        if(wts->scp_s == IS_SCPT) wts->scp_s = NEED_RESTORE;
        if(wts->scp_s == NOT_SCPT) wts->scp_s = DEFAULT_SCPT_S;
      }  
      if(scp_udata.game_pid && scp_udata.game_pid != p->tgid){ 
        if(wts->scp_s == IS_SCPT || wts->scp_s == NEED_RESTORE) {
          wts->scp_s = NEED_RESTORE;
        }
      }
      if(!scp_udata.game_pid && (wts->scp_s == IS_SCPT || wts->scp_s == NEED_RESTORE)){
        wts->scp_s = NEED_RESTORE;
      }

      if(wts->scp_s == DEFAULT_SCPT_S || wts->scp_s == IS_SCPT)//修改sched_class
      {
         if(wts->scp_s == DEFAULT_SCPT_S){
              if(scp_udata.debug) printk(KERN_INFO "[scp_work]----uspspace add p-thread:%s!\n", p->comm); 
              for(tid_comms_index = 0; tid_comms_index < scp_udata.tcomm_sz; tid_comms_index++){
                 if(strnlen(p->comm, TASK_COMM_LEN) < strnlen(scp_udata.tid_comms[tid_comms_index].comm, TASK_COMM_LEN)){
                      wts->scp_s = NOT_SCPT;
                      wts->scpt_token = scp_udata.token;
                      continue;
                 }
                if(scp_udata.debug) printk(KERN_INFO "[scp_work]----scp_udata:index:%d thread:%s!\n", tid_comms_index, scp_udata.tid_comms[tid_comms_index].comm); 
                 is_scpt = strnstr(p->comm, scp_udata.tid_comms[tid_comms_index].comm, strnlen(p->comm, TASK_COMM_LEN)) 
                            && (strnlen(p->comm, TASK_COMM_LEN) == strnlen(scp_udata.tid_comms[tid_comms_index].comm, TASK_COMM_LEN));
                if(scp_udata.debug) printk(KERN_INFO "[scp_work]----uspspace add p-thread:%s! is_scpt:%d\n", p->comm, is_scpt); 
                 if(is_scpt){
                    wts->scp_s = IS_SCPT;
                    wts->scpt_token = scp_udata.token;
                    if(scp_udata.debug)printk(KERN_INFO "[scp_work]----scp_udata thread-%d is scpt!\n", data_item->tid); 
                    break;
                 }else{
                    wts->scp_s = NOT_SCPT;
                    wts->scpt_token = scp_udata.token;
                 }
              } 
         }
         if(wts->scp_s == NOT_SCPT){
             goto puttask;        
         }
         if(scp_udata.debug){
              printk(KERN_INFO "[scp_work]%d thread %s last_arrival:%llu now-scp_udata.active_tms:%llu p.sum_exec_runtime:%llu!\n", p->pid
                    , p->comm, p->sched_info.last_arrival, (now-scp_udata.active_tms), p->se.sum_exec_runtime);
         }
         if(!(p->sched_info.last_arrival > (now-scp_udata.active_tms) && p->se.sum_exec_runtime > scp_udata.active_tms)){
             if(scp_udata.debug)printk(KERN_INFO "[scp_work]%d thread %s exec time not fit!\n", p->pid, p->comm); 
             wts->scp_s = NOT_SCPT;
             wts->scpt_token = scp_udata.token;
             goto puttask;        
         }
         if(wts->scp_s == IS_SCPT && !rt_policy(p->policy)){
             wts->old_prio = p->static_prio;
             if(scp_udata.debug)printk(KERN_INFO "[scp_work]%d thread %s not rt sched class!\n", p->pid, p->comm); 
             for(tid_comms_index = 0; tid_comms_index < scp_udata.tcomm_sz; tid_comms_index++){
                if(strnlen(p->comm, TASK_COMM_LEN) < strnlen(scp_udata.tid_comms[tid_comms_index].comm, TASK_COMM_LEN)){
                     continue;
                }
                is_scpt = strnstr(p->comm, scp_udata.tid_comms[tid_comms_index].comm, strnlen(p->comm, TASK_COMM_LEN)) 
                           && (strnlen(p->comm, TASK_COMM_LEN) == strnlen(scp_udata.tid_comms[tid_comms_index].comm, TASK_COMM_LEN));
                if(is_scpt){
                    sp.sched_priority = scp_udata.tid_comms[tid_comms_index].priority;
                    WARN_ON_ONCE(sched_setscheduler_nocheck(p, scp_udata.tid_comms[tid_comms_index].sched_policy, &sp) != 0);
                    if(!rt_policy(p->policy)){
                       printk(KERN_INFO "[scp_work]%d thread %s set rt sched class failed!\n", p->pid, p->comm);
                    }else{
                       if(scp_udata.debug)printk(KERN_INFO "[scp_work]%d thread %s set rt sched class success!\n", p->pid, p->comm); 
                    }
                }else{
                   if(scp_udata.debug){
                      printk(KERN_INFO "[scp_work]%d thread %s not found in index:%d-(comm:%s)of scp_udata!\n", p->pid
                         , p->comm, tid_comms_index, scp_udata.tid_comms[tid_comms_index].comm);
                   }
                }
             }
         } 
      }

      if(wts->scp_s == NEED_RESTORE)//恢复sched_class
      {
        if(rt_policy(p->policy)){
          sched_set_normal(p, PRIO_TO_NICE(wts->old_prio));     
          if(scp_udata.debug)printk(KERN_INFO "[scp_work]--restore cfs sched_class--task:%d, %s\n", p->pid, p->comm); 
        }
        wts->scp_s = DEFAULT_SCPT_S;
        wts->scpt_token = 0;
      }
  }

puttask:
  put_task_struct(p);
out:
  mutex_unlock(&scp_data_lock);
  kfree(data_item);
}
void check_scpt_child_state(struct task_struct *new){
   struct walt_task_struct *curr_wts = (struct walt_task_struct *) current->android_vendor_data1;
   int creator_pid = current->pid; 
   if(!new)return;
   if(!curr_wts->is_app)return;
   if(curr_wts->scp_s != IS_SCPT
      || !rt_policy(new->policy)
      || !rt_policy(current->policy)){
      return;
   }
   q_restore_cfs_task(new->pid, curr_wts->old_prio);
   if(scp_udata.debug)printk(KERN_INFO "[check_scpt_child_state]----creator_pid:%d child task:%d, child comm:%s\n", creator_pid, new->pid, new->comm); 
}
//这个函数会执行在各个运用进程，函数内最好不要持锁
void q_scp_work(struct task_struct *prev, struct task_struct *task){
   struct walt_task_struct *wts = (struct walt_task_struct *) task->android_vendor_data1;
   struct walt_task_struct *prev_wts = (struct walt_task_struct *) prev->android_vendor_data1;
   bool q_work = false;
   int queue_w_result = 0;
   scp_kwork_data *data_item = NULL;
   //非应用的task，我们忽略不处理
   if(!wts->is_app || !prev_wts->is_app
     || task->tgid != prev->tgid){
      return;
   } 
   /*
   *game_pid 等于或者不等于 tgid；token以及  scp 4状态 
   *game_pid 关闭 scp 4状态
   *
   *      DEFAULT_SCPT_S = 0,
   *	 IS_SCPT,
   * 	 NOT_SCPT,
   *      NEED_RESTORE,
   *                  1                     2                   3
   * game_pid 开启   等于 tgid(a)      token   相等(a)   DEFAULT_SCP_S(a) 
   *               不等于 tgid(b)      token 不相等(b)         IS_SCPT(b)
   *                                                          NOT_SCPT(c)
   *                                                      NEED_RESTORE(d)
   * 1-a 2-a 3-a: queuework进行识别 
   * 1-a 2-a 3-b: 如果task不是rt，queuework
   * 1-a 2-a 3-c: 直接返回
   * 1-a 2-a 3-d: 修改状态为NEED_RESTORE queuework进行恢复操作
   *
   * //以下组合就是应对usrspace在同进程内多次改变tname的情况
   * 1-a 2-b 3-a: queuework进行识别
   * 1-a 2-b 3-b: 修改状态为NEED_RESTORE queuework进行恢复操作
   * 1-a 2-b 3-c: 设置状态为default，进行识别操作 
   * 1-a 2-b 3-d: 修改状态为NEED_RESTORE queuework进行恢复操作 
   *
   * //token是给game_pid 等于tgid的情况使用的;因此下面的情况就不用考虑了
   * //这种情况属于usrspace没有 echo 0@0进行数据清理，又再次要将其它进程写入
   * //或者正常的场景
   * 1-b 3-a: 不进行识别操作 
   * 1-b 3-b: 修改状态为NEED_RESTORE queuework进行恢复操作
   * 1-b 3-c: 设置状态为default，不进行识别操作
   * 1-b 3-d: queuework进行恢复操作
   *
   * game_pid 关闭 
   * DEFAULT_SCP_S(a)-不处理
   *       IS_SCPT(b)-修改状态为NEED_RESTORE queuework进行恢复操作
   *      NOT_SCPT(c)-设置状态为default，不进进行识别操作
   *  NEED_RESTORE(d)-queuework进行恢复操作
   */
   if(scp_udata.game_pid){//game_pid 开启
     if(scp_udata.game_pid == task->tgid){//1-a
       if(scp_udata.token && (scp_udata.token == wts->scpt_token)){//2-a
          if((wts->scp_s == NOT_SCPT)
            || (wts->scp_s == IS_SCPT && rt_policy(task->policy))){
            return;
          }
          q_work = true;
       }else{//2-b
          //线程安全考虑，此两处的状态修改挪到204行。
          //if(wts->scp_s == IS_SCPT) wts->scp_s = NEED_RESTORE;
          //if(wts->scp_s == NOT_SCPT) wts->scp_s = DEFAULT_SCPT_S;
          q_work = true;
       } 
     }else{//1-b
          /*if(wts->scp_s == NOT_SCPT || wts->scp_s == DEFAULT_SCPT_S){
              wts->scp_s = DEFAULT_SCPT_S;
              return;
          }这个地方不修改状态也是可以的，随着状态演进，这个操作会被367行逻辑覆盖*/
          if(wts->scp_s == IS_SCPT || wts->scp_s == NEED_RESTORE) {
            //状态值线程安全考虑，此次状态修改挪到209行
            //wts->scp_s = NEED_RESTORE;
            q_work = true;
          }
     }
   }else{//game_pid 关闭
     /*if(wts->scp_s == NOT_SCPT || wts->scp_s == DEFAULT_SCPT_S){
         wts->scp_s = DEFAULT_SCPT_S;
         return;
     }这个地方不修改状态也是可以的，随着状态演进，这个操作会被369行逻辑覆盖*/
     if(wts->scp_s == IS_SCPT || wts->scp_s == NEED_RESTORE) {
       //状态值线程安全考虑，此次状态修改挪到214行
       //wts->scp_s = NEED_RESTORE;
       q_work = true;
     }
   }
   if(q_work){
      data_item = kzalloc(sizeof(scp_kwork_data), GFP_KERNEL);
      if(!data_item){
         if(scp_udata.debug) {
           printk(KERN_INFO "[q_scp_work]----scp_kwork_data kzalloc failed! task:%s\n", task->comm); 
         }
        return;
      }
      data_item->tid = task->pid; 
      if(kthread_running){
         kthread_init_work(&data_item->work, scp_work);
         queue_w_result = kthread_queue_work(&worker, &data_item->work);
         if(!queue_w_result){
           if(scp_udata.debug) {
              printk(KERN_INFO "[q_scp_work]----queue work failed! task:%s\n", task->comm); 
           }
           kfree(data_item);
         }
         if(scp_udata.debug)printk(KERN_INFO "[q_scp_work]----queue work success! task:%s\n", task->comm); 
      }else{
         kfree(data_item);
      }
   }
}
//根据游戏线程名字，将关键线程修改为rt-end

//将char转成数字
int atoi(char *pstr){
  int sum = 0;
  int sign = 1;
  if(pstr == NULL){
    return 0;
  }
  pstr = strim(pstr);
  if(*pstr == '-'){
    sign = -1;
  }
  if(*pstr == '-' || *pstr == '+'){
    pstr++;
  }
  while(*pstr >= '0' && *pstr <= '9'){
    sum = sum * 10 + (*pstr - '0');
    pstr++;
  }
  sum = sign * sum;
  return sum;
}
/*
* 此时是否合适对线程进行affinity的修改
* 合适返回True，否则返回false。
* 比如usrspace 临时关闭了7号核，此时在将p绑定只能在7号核上运行是不合适的。
*/
bool change_allowed(struct task_struct *p, struct cpumask *new_mask){
   struct cpumask cpus_allowed;
   int cpu = 0;
   if(!p || cpumask_empty(new_mask))return false;

   /*
   * If the node that the CPU is on has been offlined, cpu_to_node()
   * will return -1. There is no CPU on the node.
   */
   for_each_cpu(cpu, new_mask) {
     if(cpu_to_node(cpu) == -1){
        return false;
     }
   }
   if(cpumask_subset(new_mask, p->cpus_ptr)){
     return true;
   }
   cpumask_clear(&cpus_allowed);
   cpuset_cpus_allowed(p, &cpus_allowed);
   if(cpumask_subset(new_mask, &cpus_allowed)){
      return true;
   }
   return false;
}
u32 nbia_task_demand_boost(struct task_struct *p, u32 orig_pred_demand){
  int x = -1;
  if(unlikely(walt_disabled)) return orig_pred_demand;
  if(!sysctl_sched_nbia_dp)return orig_pred_demand;
  if(sched_nbia_debug)printk(KERN_INFO "nbia_task_demand_boost sysctl_sched_nbia_dp:%d \n", sysctl_sched_nbia_dp);
  for(x = 0; x < NBIA_DP_ARRAY_LEN; x+=3){
     if(sysctl_sched_nbia_dp_array[x] == 0) continue;
     if(sysctl_sched_nbia_dp_array[x] != p->pid)continue;
     if(sysctl_sched_nbia_dp_array[x+1] == 0)break;
     if(sysctl_sched_nbia_dp_array[x+2] == 0)break;
     if(sysctl_sched_nbia_dp_array[x+2] > DEMAD_INCREASE_INDEX_MAX)break;
     if(orig_pred_demand < sysctl_sched_nbia_dp_array[x+1])break;
     return orig_pred_demand 
           + (orig_pred_demand >> sysctl_sched_nbia_dp_array[x+2]);
  }
  return orig_pred_demand;
}

static void nbia_walt_work(struct kthread_work *work)
{
  RBItem *item = container_of(work, RBItem, work);
  if(!item){
    if(sched_nbia_debug) {
        printk(KERN_INFO "nbia_walt_work RBItem is null!\n");
    }
    return;
  }
  if(sched_nbia_debug) {
      printk(KERN_INFO "nbia_walt_work tids:[%d, %d, %d, %d] exetimes[%lu, %lu, %lu, %lu]\n", item->tid0, item->tid1, item->tid2, item->tid3
            , item->tid0_time, item->tid1_time, item->tid2_time, item->tid3_time);
  }
  mutex_lock(&r_buffer_lock);
  r_buffer->addItem(r_buffer, item);
  mutex_unlock(&r_buffer_lock);
  kfree(item); 
}
void nbia_wakeup_new_task(void *unused, struct task_struct *new)
{
    #define CPU_CORES 8
    int tgid = 0;
    int creator_pid = 0;
    int index = 0;
    int cpu_bit_dec[CPU_CORES] = {1, 2, 4, 8, 16, 32, 64, 128};
    int orig_allow_cpu = 0;
    struct cpumask *newmask = NULL;
    struct walt_task_struct *wts = (struct walt_task_struct *) new->android_vendor_data1;

    // if(sched_nbia_debug) {
    //   printk(KERN_INFO "[nbia_wake_up_new_task] prio_adj_info_data_changed:%d!\n", prio_adj_info_data_changed);
    // }
    check_scpt_child_state(new);
    //usrspace没有往下传递数据
    if(!prio_adj_info_data_changed)return;
    if(fair_policy(new->policy))return;
    if(!wts->is_app)return;

    tgid = new->tgid;
    creator_pid = current->pid; 
    for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
    {
        if(sched_nbia_debug) {
           printk(KERN_INFO "[nbia_wake_up_new_task]----orig-pid:%d new-task-pid:%d new-task-parent-tid:%d \n",sched_prio_adjusted_info[index][0], tgid, creator_pid); 
        }
        if(sched_prio_adjusted_info[index][0] == tgid
         && sched_prio_adjusted_info[index][1] == creator_pid)
        {
            break; 
        }
    }
    if(index == PRIO_ADJUSTED_TID_ARRAY_SIZE)return;
    q_restore_cfs_task(new->pid, sched_prio_adjusted_info[index][3]);
    orig_allow_cpu = sched_prio_adjusted_info[index][4];
    newmask = kzalloc(sizeof(struct cpumask), GFP_KERNEL);
    if(!newmask)return;
    for(index = 0; index < CPU_CORES; index++)
    {
       if(sched_nbia_debug) {
          printk(KERN_INFO "[nbia_wake_up_new_task]----orig-allow_cpu:%d cpu_bit_desc:%d result:%d \n", orig_allow_cpu
             , cpu_bit_dec[index], (orig_allow_cpu & cpu_bit_dec[index])); 
       }
       if((orig_allow_cpu & cpu_bit_dec[index]))
       {
           cpumask_set_cpu(index, newmask);
       }
    }
    //set_cpus_allowed_ptr(new, newmask);
    kfree(newmask); 
    if(sched_nbia_debug) {
       printk(KERN_INFO "[nbia_wake_up_new_task] exec finish!\n");
    }
}
void nbia_android_rvh_wakeup_success(struct task_struct *prev,
		struct task_struct *next)
{
  struct walt_task_struct *prev_wts = (struct walt_task_struct *) prev->android_vendor_data1;
  struct walt_task_struct *next_wts = (struct walt_task_struct *) next->android_vendor_data1;
  bool queue_w_result = false;
  RBItem *item;
  u64 now = sched_clock();
  if(unlikely(walt_disabled)) return;
  //if(sched_nbia_debug) {
  //   printk(KERN_INFO "[nbia-wakeup-success]----sched_nbia_render_tid_array[0]:%d, sched_nbia_render_tid_array[1]:%d\n", sched_nbia_render_tid_array[0]
  //            , sched_nbia_render_tid_array[1]);
  //}
  q_scp_work(prev, next);
  //usrSpace未曾告知关键r_tid，不做调用关系采样工作。
  if(sched_nbia_render_tid_array[0] <= 0
     || sched_nbia_render_tid_array[1] <= 0){
     return;
  }
  //忽略内核线程及系统应用或者系统进程，这些不需要参与usrspace的逻辑
  if(!prev_wts->is_app || !next_wts->is_app) return;

  //是关注的目标进程，并且忽略外部进程唤醒上帧线程的情况
  if(prev->tgid == next->tgid
     && next->tgid == sched_nbia_render_tid_array[0]){
      //忽略自己唤醒自己的情况
      if (likely(prev == next)) {
         return; 
      }

      next_wts->l_pid = prev->pid;   
      next_wts->l_sexec_ts = prev->se.sum_exec_runtime;   
      next_wts->pl_pid = prev_wts->l_pid;   
      next_wts->pl_sexec_ts = prev_wts->l_sexec_ts;   

      //忽略不是唤醒上帧线程的情况
      if(next->pid != sched_nbia_render_tid_array[1]){
         return;
      }
      if(next_wts->ringitem_build_expires <= 0){
         next_wts->ringitem_build_expires = now;
      }

      //未满足采样周期间隔
      if(now < next_wts->ringitem_build_expires){
         return;
      } 
      if(sched_nbia_debug) {
         printk(KERN_INFO "[nbia-wakeup-success]----sched_nbia_rate_tns:%d sched_nbia_queuework_enable:%d\n"
              , sched_nbia_rate_tns, sched_nbia_queuework_enable); 
      }
      next_wts->ringitem_build_expires = now + sched_nbia_rate_tns;
      //满足采样周期间隔
      item = kzalloc(sizeof(RBItem), GFP_KERNEL);
      if(!item){
         if(sched_nbia_debug) {
            printk(KERN_INFO "[nbia-wakeup-success]----RBItem kzalloc failed!\n"); 
         }
        return;
      }
      item->tid0 = next_wts->l_pid;
      item->tid1 = next_wts->pl_pid;
      item->tid2 = prev_wts->pl_pid;
      item->tid3 = next->pid;
      item->tid0_time = next_wts->l_sexec_ts;
      item->tid1_time = next_wts->pl_sexec_ts;
      item->tid2_time = prev_wts->pl_sexec_ts;
      item->tid3_time = next->se.sum_exec_runtime;
      //往kwork队列塞任务 
      if(kthread_running && sched_nbia_queuework_enable){
         kthread_init_work(&item->work, nbia_walt_work);
         queue_w_result = kthread_queue_work(&worker, &item->work);
         if(!queue_w_result){
           kfree(item);
         }
      }else{
         kfree(item);
      }
      if(sched_nbia_debug) {
         printk(KERN_INFO "[nbia-wakeup-success]----queue_w_result:%d thread_running:%d\n", queue_w_result, kthread_running); 
      }
  }
}
static void nbia_affinity_ctrl_work(struct kthread_work *work)
{
  AffinityWork_Args *args = container_of(work, AffinityWork_Args, work);
  struct walt_task_struct *wts = NULL;
  struct task_struct *p = NULL;
  int err = 0;
  int cpu = 0;
  if(!args){
    if(sched_nbia_debug) {
        printk(KERN_INFO "nbia_walt_work AffinityWork_Args is null!\n");
    }
    return;
  }
  mutex_lock(&sysfs_store_lock);

  p = get_pid_task(find_vpid(args->tid),PIDTYPE_PID);
  if(!p){
    if(sched_nbia_debug) {
       printk(KERN_INFO "[nbia_affinity_ctrl_work] %d task_struct not found!\n", args->tid);
    }
    mutex_unlock(&sysfs_store_lock);
    kfree(args);
    return;
  }
  wts = (struct walt_task_struct *) p->android_vendor_data1;
  if(wts){
      if(sched_nbia_debug) {
        printk(KERN_INFO "[nbia_affinity_ctrl_work] target_task:%d \n", args->tid);
        for_each_cpu(cpu, &args->new_mask) {
           printk(KERN_INFO "[nbia_affinity_ctrl_work] args->new_mask:cpu:%d \n", cpu);
        }
        for_each_cpu(cpu, p->cpus_ptr) {
           printk(KERN_INFO "[nbia_affinity_ctrl_work] p->cpus_ptr cpu:%d!\n", cpu);
        }
      }
      if(!change_allowed(p, &(args->new_mask))){
        if(sched_nbia_debug) printk(KERN_INFO "[nbia_affinity_ctrl_work] thread:%d change affinity not allowed\n", args->tid);
      }else{
         if(p->user_cpus_ptr != NULL) {
         	cpumask_copy(p->user_cpus_ptr, &(args->new_mask));
         }
         err = set_cpus_allowed_ptr(p, &(args->new_mask));
         if(err){
            printk(KERN_INFO "[nbia_affinity_ctrl_work] thread:%d cpus_allowed set failed。\n", args->tid);
         }
         if(sched_nbia_debug) printk(KERN_INFO "[nbia_affinity_ctrl_work] thread:%d cpus_allowed result, err:%d \n", args->tid, err);
      }
  }
  put_task_struct(p);
  mutex_unlock(&sysfs_store_lock);
  kfree(args);
}
void q_affinity_work(int tid, struct cpumask *new_mask){
    bool queue_w_result = false;
    AffinityWork_Args *args = NULL;

    if(tid <= 0)return;
    if(cpumask_empty(new_mask)){
       printk(KERN_INFO "[nbia_q_affinity_work] ---- data err, mask is empty!\n");
       return;
    }
    args = kzalloc(sizeof(AffinityWork_Args), GFP_KERNEL);
    if(sched_nbia_debug) printk(KERN_INFO "[nbia_q_affinity_work] ---- target thread tid:%d\n", tid);
    if(!args){
       if(sched_nbia_debug) {
          printk(KERN_INFO "[nbia_q_affinity_work] ---- AffinityWork_Args kzalloc failed!\n");
       }
      return;
    }
    args->tid = tid;
    cpumask_copy(&(args->new_mask), new_mask);
    //往kwork队列塞任务
    if(kthread_running){
       kthread_init_work(&args->work, nbia_affinity_ctrl_work);
       queue_w_result = kthread_queue_work(&worker, &args->work);
       if(!queue_w_result){
          if(sched_nbia_debug) {
             printk(KERN_INFO "[nbia_q_affinity_work] ---- queuework failed!\n");
          }
          kfree(args);
       } 
    }else{
       if(sched_nbia_debug) {
          printk(KERN_INFO "[nbia_q_affinity_work] ---- kthread not started!\n");
       }
       kfree(args);
    }
}
/*
static void android_rvh_set_cpus_allowed_comm(void *unused, struct task_struct *p, const struct cpumask *new_mask)
{
    struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
    int cpu = 0;
    if(!wts->is_app)return;
    if(!wts->mask_persisted){
       cpumask_copy(&wts->last, new_mask);
       return;
    }
    //在实际调试中经常发现swap线程以及migration线程来修改目标线程的affinity
    if(current->mm == NULL
      && !(kthread_running && (current->pid == worker.task->pid))){
       cpumask_copy(&wts->last, new_mask);
       return;
    }
    if(sched_nbia_debug) {
       printk(KERN_INFO "[nbia_set_cpus_allowed_comm]find target thread:%d, function caller:%d caller_comm:%s! \n", p->pid, current->pid, current->comm);
       for_each_cpu(cpu, &(wts->req)) {
          printk(KERN_INFO "[nbia_set_cpus_allowed_comm] usr-space-req-mask:%d \n", cpu);
       }
       for_each_cpu(cpu, p->cpus_ptr) {
          printk(KERN_INFO "[nbia_set_cpus_allowed_comm] p->cpus_ptr cpu:%d!\n", cpu);
       }
    }
    if((wts->persist_caller == current->pid)
      || (kthread_running && (current->pid == worker.task->pid))){
       if(sched_nbia_debug) printk(KERN_INFO "[nbia_set_cpus_allowed_comm]change thread:%d affinity by ourself!\n", p->pid);
       return;
    }
    if(cpumask_equal(new_mask, &(wts->req))){
      cpumask_copy(&(wts->last), new_mask);
      return;
    }
    if(sched_nbia_debug) {
      if(sched_nbia_debug) printk(KERN_INFO "[nbia_set_cpus_allowed_comm] target thread:%d has different cpumask value!\n", p->pid);
    }
    cpumask_copy(&wts->last, new_mask);
    if(change_allowed(p, &(wts->req))){
       if(sched_nbia_debug) printk(KERN_INFO "[nbia_set_cpus_allowed_comm] target thread:%d change_mask_allowed!\n", p->pid);
       q_affinity_work(p->pid, &(wts->req));
    }
}
*/
/**
* 如果允许walt修改返回true，否则返回false, 同时要修改ret值。
**/
bool nbia_update_cpus_allowed(void *unused, struct task_struct *p,
						cpumask_var_t cpus_requested,
						const struct cpumask *new_mask, int *ret)
{
    struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
    if(!wts->is_app)return true;
    //如果usrspace设置的mask不是cpuset设置的一个子集，那么很有可能kernel在缩窄mask
    //这时候没有必要在进行校验跟拦截
    if(wts->mask_persisted){
       if(!cpumask_subset(&(wts->req), cpus_requested)){
          return true;
       }
       *ret = 0;
       if(sched_nbia_debug){
         printk(KERN_INFO "[nbia_update_cpus_allowed]find target thread:%d, function caller:%d caller_comm:%s! \n", p->pid, current->pid, current->comm);
         printk(KERN_INFO "[nbia_update_cpus_allowed] thread:%d is on ctrl array! ignore thread:%d' setaffinity! \n", p->pid, current->pid);
       }
       cpumask_copy(&wts->last, new_mask);
       if(!cpumask_equal(&(wts->req), p->cpus_ptr)){
          if(sched_nbia_debug) printk(KERN_INFO "[nbia_update_cpus_allowed] thread:%d' req cpumask not enable。change it! \n", p->pid);
          q_affinity_work(p->pid, &(wts->req));
       }
       return false;
    }
    return true;
}
static void nbia_sched_setaffinity_early(void *unused, struct task_struct *p, const struct cpumask *new_mask, bool *retval)
{
    struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
    if(!wts->is_app)return;
    if(wts->mask_persisted){
       *retval = true;
       if(sched_nbia_debug){
         printk(KERN_INFO "[nbia_sched_setaffinity_early]find target thread:%d, function caller:%d caller_comm:%s! \n", p->pid, current->pid, current->comm);
         printk(KERN_INFO "[nbia_sched_setaffinity_early] thread:%d is on ctrl array! ignore thread:%d' setaffinity! \n", p->pid, current->pid);
       }
       cpumask_copy(&wts->last, new_mask);
       if(!cpumask_equal(&(wts->req), p->cpus_ptr)){
          if(sched_nbia_debug)printk(KERN_INFO "[nbia_sched_setaffinity_early] thread:%d' req cpumask not enable。change it! \n", p->pid);
          q_affinity_work(p->pid, &(wts->req));
       }
    }
}
unsigned int get_early_up_migrate(int index){
   if(index >= WALT_NR_CPUS)return 0;
   if(scp_udata.debug) {
       printk(KERN_INFO "[get_early_up_migrate] index:%d up_migrate:%d\n", index, sched_early_up_migrate[index]);
   }
   return sched_early_up_migrate[index];
}
unsigned int get_early_down_migrate(int index){
   if(index >= WALT_NR_CPUS)return 0;
   if(scp_udata.debug) {
       printk(KERN_INFO "[get_early_down_migrate] index:%d down_migrate:%d\n", index, sched_early_down_migrate[index]);
   }
   return sched_early_down_migrate[index];
}
//以下部分都是创建/sys/nbia/目录下各种节点的代码
#define show_attr_simple(pt_name)                                                             \
static ssize_t pt_name##_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)   \
{                                                                                             \
   ssize_t retval = 0;                                                                        \
   char event_string[256] = { 0 };                                                            \
   snprintf(event_string, sizeof(event_string), "%d", pt_name);                               \
   retval = snprintf(buf, sizeof(event_string),"%s", event_string);                           \
   return retval;                                                                             \
}                                                                                             \

#define store_attr_simple(pt_name)                                                             \
static ssize_t pt_name##_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) \
{\
    ssize_t retval = 0;\
    int debugValue = -1;\
    char chBuffer[256] = { 0 };\
    char *strTmp = NULL;\
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);\
    retval = count;\
    strTmp = chBuffer;\
    debugValue = atoi(strTmp);\
    if(debugValue <= 1){\
       pt_name = debugValue;\
    }\
    strTmp = NULL;\
    return retval;\
}\

#define kobj_attr(pt_name)                                                                           \
static struct kobj_attribute pt_name##_attr = __ATTR(pt_name, 0664, pt_name##_show, pt_name##_store) \

static ssize_t sched_nbia_rate_tns_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    int rate_time = -1;
    char chBuffer[256] = { 0 };
    char *strTmp = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_nbia_rate_tns_store-buf:%s \n", buf);
    retval = count;
    strTmp = chBuffer;
    rate_time = atoi(strTmp);
    if(0 <= rate_time && rate_time <= RINGITME_ADD_RATE_TIME){
       sched_nbia_rate_tns = rate_time;
    }
    strTmp = NULL;
    return retval;
}
show_attr_simple(sched_nbia_rate_tns);
static ssize_t sched_nbia_rbuffer_size_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    int rbuffer_size = -1;
    char chBuffer[256] = { 0 };
    char *strTmp = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_nbia_rbuffer_size_store-buf:%s \n", buf);
    retval = count;
    strTmp = chBuffer;
    rbuffer_size = atoi(strTmp);
    if(0 < rbuffer_size && rbuffer_size <= RINGBUFFER_SIZE_MAX){
       mutex_lock(&r_buffer_lock);
       if(NULL == r_buffer) {
          strTmp = NULL;
          mutex_unlock(&r_buffer_lock);
          return retval;
       }
       r_buffer->setSize(r_buffer, rbuffer_size);
       r_buffer->clear(r_buffer);
       mutex_unlock(&r_buffer_lock);
    }
    strTmp = NULL;
    return retval;
}
static ssize_t sched_nbia_rbuffer_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   int size = 0;
   mutex_lock(&r_buffer_lock);
   if(NULL == r_buffer) {
      mutex_unlock(&r_buffer_lock);
      return retval;
   }
   size = r_buffer->getSize(r_buffer);
   mutex_unlock(&r_buffer_lock);
   snprintf(event_string, sizeof(event_string), "%d", size);
   retval = snprintf(buf, sizeof(event_string),"%s", event_string);
   return retval;
}
show_attr_simple(sched_nbia_queuework_enable);
static ssize_t sched_nbia_queuework_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    int queuework_enable = -1;
    char chBuffer[256] = { 0 };
    char *strTmp = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_nbia_queuework_enable_store-buf:%s \n", buf);
    retval = count;
    strTmp = chBuffer;
    queuework_enable = atoi(strTmp);
    if(queuework_enable <= 1){
       sched_nbia_queuework_enable = queuework_enable;
       mutex_lock(&r_buffer_lock);
       if(NULL != r_buffer){
          r_buffer->clear(r_buffer);
       }
       mutex_unlock(&r_buffer_lock);
    }
    strTmp = NULL;
    return retval;
}

store_attr_simple(sched_nbia_debug);
show_attr_simple(sched_nbia_debug);
static ssize_t sched_nbia_render_tid_array_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   snprintf(event_string, sizeof(event_string), "%d:%d"
     , sched_nbia_render_tid_array[0]
     , sched_nbia_render_tid_array[1]);
   retval = snprintf(buf, sizeof(event_string),"%s", event_string);
   return retval;
}
static ssize_t sched_nbia_render_tid_array_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    int pid = -1;
    int render_tid = -1;
    const char *pchDilem = "@";
    char chBuffer[256] = { 0 };
    char *pchTmp = NULL;
    char *optStr = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_nbia_render_tid_array_store-buf:%s \n", buf);
    retval = count;
    if(strstr(chBuffer, pchDilem)){
       pchTmp = chBuffer;
       optStr = strsep(&pchTmp, pchDilem);
       if(strnlen(optStr, 8) && strnlen(pchTmp, 8)){
            pid = atoi(optStr);
            render_tid = atoi(pchTmp);
            printk(KERN_INFO "sched_nbia_render_tid_array_store pid:%d, render-tid:%d kthread_running:%d\n", pid, render_tid, kthread_running);
            mutex_lock(&sysfs_store_lock);
            if(pid > 0 && render_tid > 0){
               if(kthread_running){
                 sched_nbia_render_tid_array[0] = pid;
                 sched_nbia_render_tid_array[1] = render_tid;
               }
            }else{
               sched_nbia_render_tid_array[0] = pid;
               sched_nbia_render_tid_array[1] = render_tid;
            }
            mutex_unlock(&sysfs_store_lock);
            mutex_lock(&r_buffer_lock);
            if(NULL != r_buffer){
               r_buffer->clear(r_buffer);
            }
            mutex_unlock(&r_buffer_lock);
       }
       pchTmp = NULL;
       optStr = NULL;
    }
    return retval;
}
static ssize_t sched_nbia_loaded_tid_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
   return 0;
}
static ssize_t sched_nbia_loaded_tid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   int i;
   int j;
   int tids[4] = { 0 };
   u64 sexec_ts[4] = { 0 };
   RBItem item;

   mutex_lock(&r_buffer_lock);
   if(unlikely(walt_disabled) || NULL == r_buffer || sched_nbia_render_tid_array[0] <= 0
      || sched_nbia_render_tid_array[1] <= 0) {
      mutex_unlock(&r_buffer_lock);
      return retval;
   }
   r_buffer->getMostFrequent(r_buffer, &item);
   mutex_unlock(&r_buffer_lock);
   //将上帧线程拿掉，usrspace针对上帧线程已经有独立处理了。
   if(item.tid0 != sched_nbia_render_tid_array[1]){
      tids[0] = item.tid0;
      sexec_ts[0] = item.tid0_time;
   }
   if(item.tid1 != sched_nbia_render_tid_array[1]){
      tids[1] = item.tid1;
      sexec_ts[1] = item.tid1_time;
   }
   if(item.tid2 != sched_nbia_render_tid_array[1]){
      tids[2] = item.tid2;
      sexec_ts[2] = item.tid2_time;
   }
   //tid3以及tid3_time是render线程的tid以及时间。
   tids[2] = item.tid3;
   sexec_ts[2] = item.tid3_time;
   //按照线程执行时间，从大到小排列
   for(j = 0; j < 3; j++){
      for(i = j; i < 3; i++){
         if(sexec_ts[j] < sexec_ts[i]){
           sexec_ts[j] ^= sexec_ts[i];
           sexec_ts[i] ^= sexec_ts[j];
           sexec_ts[j] ^= sexec_ts[i];
           tids[j] ^= tids[i];
           tids[i] ^= tids[j];
           tids[j] ^= tids[i];
         }
      }
   }
   //数据去重
   if(tids[0] == tids[1]
     || tids[1] == tids[2]){
     tids[1] = tids[2];
     tids[2] = 0;
   }
   //将render_tid纳入排序，重新排一次
   for(j = 0; j < 4; j++){
      for(i = j; i < 4; i++){
         if(sexec_ts[j] < sexec_ts[i]){
           sexec_ts[j] ^= sexec_ts[i];
           sexec_ts[i] ^= sexec_ts[j];
           sexec_ts[j] ^= sexec_ts[i];
           tids[j] ^= tids[i];
           tids[i] ^= tids[j];
           tids[j] ^= tids[i];
         }
      }
   }
   if(sched_nbia_debug){
       printk(KERN_INFO "sched_nbia_loaded_tid_show after sort tids:[%d, %d, %d, %d] exetimes[%lu, %lu, %lu, %lu]\n", tids[0], tids[1], tids[2], tids[3]
               ,sexec_ts[0], sexec_ts[1], sexec_ts[2], sexec_ts[3]);
   }
   //排序之后赋值，从大到小排列。index=0的是运行时间最大的
   snprintf(event_string, sizeof(event_string), "%d:%d:%d:%d\n"
     , tids[0]
     , tids[1]
     , tids[2]
     , tids[3]);
   retval = snprintf(buf, sizeof(event_string),"%s\n", event_string);
   return retval;
}

static ssize_t sched_prio_adjusted_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   snprintf(event_string, sizeof(event_string), "[%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d]\n"
     , sched_prio_adjusted_info[0][0], sched_prio_adjusted_info[0][1], sched_prio_adjusted_info[0][2]
     , sched_prio_adjusted_info[0][3], sched_prio_adjusted_info[0][4]
     , sched_prio_adjusted_info[1][0], sched_prio_adjusted_info[1][1], sched_prio_adjusted_info[1][2]
     , sched_prio_adjusted_info[1][3], sched_prio_adjusted_info[1][4]
     , sched_prio_adjusted_info[2][0], sched_prio_adjusted_info[2][1], sched_prio_adjusted_info[2][2]
     , sched_prio_adjusted_info[2][3], sched_prio_adjusted_info[2][4]
     , sched_prio_adjusted_info[3][0], sched_prio_adjusted_info[3][1], sched_prio_adjusted_info[3][2]
     , sched_prio_adjusted_info[3][3], sched_prio_adjusted_info[3][4]);
   retval = snprintf(buf, sizeof(event_string),"%s\n", event_string);
   return retval;
}
static ssize_t sched_prio_adjusted_info_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    const char *pchDilem = "@";
    char chBuffer[256] = { 0 };
    char *pchTmp = NULL;
    char *optStr = NULL;
    int adj_info[5] = { -1 };
    int index = 0;
    bool find_duplicat_data = false;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_prio_adjusted_info_store-buf:%s \n", buf);
    retval = count;
    if(strstr(chBuffer, pchDilem)){
       pchTmp = chBuffer;
       while((optStr = strsep(&pchTmp, pchDilem)) != NULL && index < 5){
          if(strnlen(optStr, 8)){
            adj_info[index++] = atoi(optStr);
          }
       }
       //传入数据异常，没有严格按照pid@tid@s_class@prio@affinity的格式写入。
       if(index < 4 || adj_info[0] < 0){
          printk(KERN_INFO "data format is pid@tid@s_class@prio@affinity!\n");
          return -EINVAL;
       }
       //tid@s_class@prio这个格式如果是0@tid@0这种形式，意味着是usrspace为了
       if(adj_info[0] == 0
          && adj_info[1] == 0
          && adj_info[2] == 0
          && adj_info[3] == 0
          && adj_info[4] == 0)//usrspace要全清数据
       {
         mutex_lock(&sysfs_store_lock);
         memset(sched_prio_adjusted_info, 0, PRIO_ADJUSTED_TID_ARRAY_SIZE * 5);
         mutex_unlock(&sysfs_store_lock);
         return retval;
       }
       //将tid从监控数组内移除
       if(adj_info[0] <= 0){
          //上层没有传入tid值
          if(adj_info[1] <= 0){
             printk(KERN_INFO "data format is 0@tid@0@0@0!\n");
             return -EINVAL;
          }

          mutex_lock(&sysfs_store_lock);
          //在sched_prio_adjusted_info数组内寻找对应的数据，清除。
          for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
          {
              if(sched_prio_adjusted_info[index][1] == adj_info[1])
              {
                  sched_prio_adjusted_info[index][0] = 0;
                  sched_prio_adjusted_info[index][1] = 0;
                  sched_prio_adjusted_info[index][2] = 0;
                  sched_prio_adjusted_info[index][3] = 0;
                  sched_prio_adjusted_info[index][4] = 0;
                  break;
              }
          } 
          //检查下是否还有需要关注的tid
          for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
          {
              if(sched_prio_adjusted_info[index][0] != 0)
              {
                 prio_adj_info_data_changed = true;
                 break;
              }
              prio_adj_info_data_changed = false;
          } 
          mutex_unlock(&sysfs_store_lock);
       }else{
          //留意判断数据异常
          if(adj_info[1] <= 0){
             printk(KERN_INFO "tid value invalid!\n");
             return -EINVAL;
          }
          if(!fair_policy(adj_info[2]))
          {
             printk(KERN_INFO "policy value invalid!\n");
             return -EINVAL;
          }
          //符合cfs调度策略的线程的优先级的值在101-140之间，默认是120
          if(adj_info[3] > MAX_PRIO || adj_info[3] <= MAX_USER_RT_PRIO)
          {
             printk(KERN_INFO "thread priority value invalid!\n");
             return -EINVAL;
          }
          //affinity 值为0-255
          if(adj_info[3] > 255 || adj_info[3] <= 0)
          {
             printk(KERN_INFO "thread cpu affinity value invalid!\n");
             return -EINVAL;
          }
          //往sched_prio_adjusted_info数组丢数据
          mutex_lock(&sysfs_store_lock);
          //可能重复写入某个组合的数据，或者更新数组内的数据
          for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
          {
              if(sched_prio_adjusted_info[index][1] == adj_info[1])
              {
                  sched_prio_adjusted_info[index][0] = adj_info[0];
                  sched_prio_adjusted_info[index][1] = adj_info[1];
                  sched_prio_adjusted_info[index][2] = adj_info[2];
                  sched_prio_adjusted_info[index][3] = adj_info[3];
                  sched_prio_adjusted_info[index][4] = adj_info[4];
                  find_duplicat_data = true;
                  break;
              }
          }
          if(!find_duplicat_data){
              index = ring_array_index % PRIO_ADJUSTED_TID_ARRAY_SIZE;
              ring_array_index++;
              sched_prio_adjusted_info[index][0] = adj_info[0];
              sched_prio_adjusted_info[index][1] = adj_info[1];
              sched_prio_adjusted_info[index][2] = adj_info[2];
              sched_prio_adjusted_info[index][3] = adj_info[3];
              sched_prio_adjusted_info[index][4] = adj_info[4];
          }
          prio_adj_info_data_changed = true;
          mutex_unlock(&sysfs_store_lock);
       }
       pchTmp = NULL;
       optStr = NULL;
    }
    return retval;
}
static ssize_t sched_affinity_ctrl_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   snprintf(event_string, sizeof(event_string), "[%d, %d] [%d, %d] [%d, %d] [%d, %d]\n"
     , cmask_bundles[0].tid, cmask_bundles[0].req_dec
     , cmask_bundles[1].tid, cmask_bundles[1].req_dec
     , cmask_bundles[2].tid, cmask_bundles[2].req_dec
     , cmask_bundles[3].tid, cmask_bundles[3].req_dec);
   retval = snprintf(buf, sizeof(event_string),"%s\n", event_string);
   return retval;
}
void change_affinity(int tid, struct cpumask *new_mask, bool restore){
    struct task_struct *p = NULL;
    struct walt_task_struct *wts = NULL;
    int err = 0;
    int cpu = 0;
    if(tid <= 0) return;
    if(!restore && cpumask_empty(new_mask))
    {
       printk(KERN_INFO "[nbia_change_affinity] thread:%d, inter func_change_affinity, restore:%d, mask is empty\n", tid, restore);
       return;
    }

    p = get_pid_task(find_vpid(tid),PIDTYPE_PID);
    if(p){
       wts = (struct walt_task_struct *) p->android_vendor_data1;
       if(wts){
          if(!restore){
             //要修改线程affinity
             wts->mask_persisted = true;
             wts->persist_caller = current->pid;
             cpumask_copy(&wts->req, new_mask);
             if(!change_allowed(p, new_mask)){
                printk(KERN_INFO "[nbia_change_affinity] thread:%d change affinity not allowed!\n", tid);
                q_affinity_work(tid, new_mask);
             }else{
			 	if(p->user_cpus_ptr != NULL) {
			 		cpumask_copy(p->user_cpus_ptr, new_mask);
				}
                err = set_cpus_allowed_ptr(p, new_mask);
                if(err){
                   printk(KERN_INFO "[nbia_change_affinity] thread:%d cpus_allowed set failed. \n", tid);
                   q_affinity_work(tid, new_mask);
                }
                printk(KERN_INFO "[nbia_change_affinity] thread:%d cpus_allowed result err::%d \n", tid, err);
             }
          }else{
             //要恢复线程affinity
             wts->mask_persisted = false;
             wts->persist_caller = -1;
             if(sched_nbia_debug){
                for_each_cpu(cpu, &wts->last) {
                   printk(KERN_INFO "[nbia_change_affinity] restore task-cpu:cpu:%d \n", cpu);
                }
             }
             /*if(!change_allowed(p, &wts->last)){
                printk(KERN_INFO "[nbia_change_affinity] thread:%d restore affinity not allowed!\n", tid);
                q_affinity_work(tid, &wts->last);
             }else{*/
                err = set_cpus_allowed_ptr(p, &wts->last);
                if(err){
                   printk(KERN_INFO "[nbia_change_affinity] thread:%d restore affinity set failed. \n", tid);
                   q_affinity_work(tid, &wts->last);
                }
                printk(KERN_INFO "[nbia_change_affinity] thread:%d restore affinity err:%d \n", tid, err);
             //}
          }
       }
       put_task_struct(p);
    }else{
       printk(KERN_INFO "[nbia_change_affinity] thread:%d task_struct not found! \n", tid);
    }
}

static ssize_t sched_affinity_ctrl_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    //userspace通过/sys/nbia/sched_affinity_ctl告知要将哪个线程设置成为哪个cpu_allows值
    //tid@affinity_value 这种方式组合写这个节点是新增要拦截的tid
    //0@tid              这种方式组合是要将tid从拦截内剔除，剔除也意味着要将这个tid的cpu_allows恢复成
    //                       它最近一次被设置的affinity值
    //0@0                这种方式组合式要全部清除cmask_bundles数组内的线程全部恢复成最近一次设置的affinity的值
    ssize_t retval = 0;
    const char *pchDilem = "@";
    char chBuffer[256] = { 0 };
    char *pchTmp = NULL;
    char *optStr = NULL;
    int cmask_info[2] = { -1 };
    int cpu_bit_dec[CPU_CORES] = {1, 2, 4, 8, 16, 32, 64, 128};
    int index = 0;
    int index_tmp = 0;
    struct cpumask req_tmp;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_affinity_ctrl_store-buf:%s \n", buf);
    retval = count;
    if(strstr(chBuffer, pchDilem)){
       pchTmp = chBuffer;
       while((optStr = strsep(&pchTmp, pchDilem)) != NULL && index < 3){
          if(strnlen(optStr, 8)){
            cmask_info[index++] = atoi(optStr);
          }
       }
       pchTmp = NULL;
       optStr = NULL;

       //全部清除cmask_bundles
       //从cmask_bundles清除某个tid
       if(cmask_info[0] == 0 && cmask_info[1] >= 0)
       {
          mutex_lock(&sysfs_store_lock);
          for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
          {
             if((cmask_info[1] == 0 && cmask_bundles[index].tid > 0)//全部清除
                || (cmask_info[1] > 0 && cmask_bundles[index].tid == cmask_info[1]))//清除某一个tid
             {
                change_affinity(cmask_bundles[index].tid, &req_tmp, true);
                cmask_bundles[index].tid = 0;
                cmask_bundles[index].req_dec = 0;
             }
          }
          mutex_unlock(&sysfs_store_lock);
          return retval;
       }
       //往cmask_bundles新增某个tid
       if(cmask_info[0] > 0 && cmask_info[1] > 0)
       {
         if(cmask_info[1] > U8_MAX)
         {
             printk(KERN_INFO "sched_affinity_ctrl_store:affinity value invalid! max-value:255 \n");
             return -EINVAL;
         }
         for(index = 0; index < PRIO_ADJUSTED_TID_ARRAY_SIZE; index++)
         {
              if(cmask_bundles[index].tid == cmask_info[0])break;
         }
         if(index < PRIO_ADJUSTED_TID_ARRAY_SIZE)
         {//新增的操作只是改变cmask_bundles中某个tid的affinity
           printk(KERN_INFO "sched_affinity_ctrl_store-buf:%d in the array! \n", cmask_info[0]);
           index_tmp = index;
           mutex_lock(&sysfs_store_lock);
           cpumask_clear(&req_tmp);
           for(index = 0; index < CPU_CORES; index++)
           {
              if((cmask_info[1] & cpu_bit_dec[index]))
              {
                  cpumask_set_cpu(index, &req_tmp);
              }
           }
           if(cpumask_empty(&req_tmp)){
             printk(KERN_INFO "sched_affinity_ctrl_store req_tmp cpumask is empty---1! \n");
             mutex_unlock(&sysfs_store_lock);
             return retval;
           }
           cmask_bundles[index_tmp].req_dec = cmask_info[1];
           change_affinity(cmask_info[0], &req_tmp, false);
           mutex_unlock(&sysfs_store_lock);
           return retval;
         }
         printk(KERN_INFO "sched_affinity_ctrl_store-buf:%d not in the array! \n", cmask_info[0]);
         mutex_lock(&sysfs_store_lock);
         index_tmp = index = bundles_array_index % PRIO_ADJUSTED_TID_ARRAY_SIZE;
         bundles_array_index++;
         //如果cmask_bundles里面有值，也就是这是一次覆盖添加。那么需要先将之前的restore了
         if(cmask_bundles[index].tid > 0){ 
             change_affinity(cmask_bundles[index].tid, &req_tmp, true);
             cmask_bundles[index].tid = 0;
             cmask_bundles[index].req_dec = 0;
         }
         cpumask_clear(&req_tmp);
         for(index = 0; index < CPU_CORES; index++)
         {
            if((cmask_info[1] & cpu_bit_dec[index]))
            {
                cpumask_set_cpu(index, &req_tmp);
            }
         }
         if(cpumask_empty(&req_tmp)){
           printk(KERN_INFO "sched_affinity_ctrl_store req_tmp cpumask is empty---2! \n");
           mutex_unlock(&sysfs_store_lock);
           return retval;
         }
         change_affinity(cmask_info[0], &req_tmp, false);
         cmask_bundles[index_tmp].tid = cmask_info[0];
         cmask_bundles[index_tmp].req_dec = cmask_info[1];
         mutex_unlock(&sysfs_store_lock);
         printk(KERN_INFO "sched_affinity_ctrl_store-buf:%d add item finish! \n", cmask_info[0]);
         return retval;
       }
    }
    return retval;
}
#define PID_IN_BUF_INDEX 0 
#define TNAME_SZ_IN_BUF_INDEX 1 
#define SUBSTR_TNAME_INDEX 0
#define SUBSTR_SCHED_POLICY_INDEX 1
#define SUBSTR_PRIORITY_INDEX 2 
static ssize_t sched_class_persisted_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    //sched_class_persisted  格式:pid@tname-size@tname@tname@tname
    //int splitChars(const char *buf, char *result, int result_array_sz)
    ssize_t retval = 0;
    const char *pchDilem = "@";
    const char *subStrPchDilem = ":";
    char chBuffer[1024] = { 0 };
    char *pchTmp = NULL;
    char *optStr = NULL;
    char *optSubStr = NULL;
    int index = 0;
    int substr_index = 0;
    int substr_len = 0;
    int tid_comms_index = 0;
    int game_pid_tmp = 0;
    u64 active_tms_bp = 32000000;//32ms

    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "sched_class_persisted-buf:%s\n", buf);
    retval = count;
    if(strstr(chBuffer, pchDilem)){
       pchTmp = chBuffer;
       mutex_lock(&scp_data_lock);
       active_tms_bp = scp_udata.active_tms;
       while((optStr = strsep(&pchTmp, pchDilem)) != NULL){
	  optStr = skip_spaces(optStr);
          if(index == PID_IN_BUF_INDEX){//处理进程号
            substr_len = strnlen(optStr, 8);
            if(substr_len > 8 || substr_len <= 0) goto out;
            game_pid_tmp = atoi(optStr);
            if(scp_udata.game_pid && game_pid_tmp){
              //uspace重复在未清除数据的情况下，重复写入
              printk(KERN_INFO "sched_class_persisted----scpdata pid exists;clear first!\n"); 
              memset(&scp_udata, 0, sizeof(scp_data));
              scp_udata.debug = sched_nbia_class_persisted_debug;
              scp_udata.active_tms = active_tms_bp;
            }
            scp_udata.game_pid = game_pid_tmp;
            if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----uspspace set pid:%d!\n", scp_udata.game_pid); 
          }else{
            if(scp_udata.game_pid){//上层要开启机制
                if(index == TNAME_SZ_IN_BUF_INDEX){//处理tcomm_size
                  substr_len = strnlen(optStr, 6);
                  if(substr_len > 6 || substr_len <= 0){
                     memset(&scp_udata, 0, sizeof(scp_data));
                     scp_udata.active_tms = active_tms_bp;
                     scp_udata.debug  = sched_nbia_class_persisted_debug;
                     goto out;
                  }
                  scp_udata.tcomm_sz = atoi(optStr);
                  scp_udata.token = sched_clock();
                  if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----uspspace set tcomm_sz:%d!\n", scp_udata.tcomm_sz); 
                  if(scp_udata.tcomm_sz > TNAME_MAX_SIZE){
                    memset(&scp_udata, 0, sizeof(scp_data));
                    scp_udata.active_tms = active_tms_bp;
                    scp_udata.debug  = sched_nbia_class_persisted_debug;
                    printk(KERN_INFO "sched_class_persisted----tname_size value invalid;max value is:%d!\n", TNAME_MAX_SIZE); 
                    goto out;
                  }
                }else{//将uspace传递下来的线程名字传递给tid_comms数组的comm字段
                  substr_len = strnlen(optStr, 6);
                  if(!substr_len){
                     memset(&scp_udata, 0, sizeof(scp_data));
                     scp_udata.active_tms = active_tms_bp;
                     scp_udata.debug  = sched_nbia_class_persisted_debug;
                     goto out;
                  } 
                  if(tid_comms_index < scp_udata.tcomm_sz && strstr(optStr, subStrPchDilem)){
                     substr_index = 0;
                     if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----uspspace add thread:%s\n", optStr); 
                     while((optSubStr= strsep(&optStr, subStrPchDilem)) != NULL){
                         substr_len = strnlen(optSubStr, 6);
                         if(!substr_len){
                             memset(&scp_udata, 0, sizeof(scp_data));
                             scp_udata.active_tms = active_tms_bp;
                             goto out;
                         }
                         if(SUBSTR_TNAME_INDEX == substr_index)
                         {
                             strlcpy(scp_udata.tid_comms[tid_comms_index].comm, optSubStr, TASK_COMM_LEN); 
                             if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----index:%d,comm:%s\n", tid_comms_index, scp_udata.tid_comms[tid_comms_index].comm);
                         }
                         if(SUBSTR_SCHED_POLICY_INDEX == substr_index)
                         {
                            scp_udata.tid_comms[tid_comms_index].sched_policy = atoi(optSubStr); 
                            if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----index:%d, sched_policy:%d\n", tid_comms_index, scp_udata.tid_comms[tid_comms_index].sched_policy);
                            if(scp_udata.tid_comms[tid_comms_index].sched_policy < 0 
                               || scp_udata.tid_comms[tid_comms_index].sched_policy > SCHED_RR){
                              if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----index:%d, sched_policy-invalid\n", tid_comms_index);
                               memset(&scp_udata, 0, sizeof(scp_data));
                               scp_udata.active_tms = active_tms_bp;
                               scp_udata.debug  = sched_nbia_class_persisted_debug;
                               goto out;
                            }
                         }
                         if(SUBSTR_PRIORITY_INDEX == substr_index)
                         {
                            scp_udata.tid_comms[tid_comms_index].priority = atoi(optSubStr); 
                            if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----index:%d,priority:%d\n", tid_comms_index, scp_udata.tid_comms[tid_comms_index].priority);
                            if(scp_udata.tid_comms[tid_comms_index].priority < 0 
                               || scp_udata.tid_comms[tid_comms_index].priority > MAX_PRIO
                               ||(scp_udata.tid_comms[tid_comms_index].sched_policy != SCHED_NORMAL
                                  && scp_udata.tid_comms[tid_comms_index].priority >= MAX_USER_RT_PRIO)){
                              if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----index:%d,priority-invalid\n", tid_comms_index);
                              memset(&scp_udata, 0, sizeof(scp_data));
                              scp_udata.active_tms = active_tms_bp;
                              scp_udata.debug  = sched_nbia_class_persisted_debug;
                              goto out;
                            }
                         }
                         substr_index++;
                     }
                     if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----scp_udata.tid_comms thread name:%s \n", scp_udata.tid_comms[tid_comms_index].comm); 
                     tid_comms_index++;
                  } 
                }
            }else{//上层要关闭机制
              if(scp_udata.debug)printk(KERN_INFO "sched_class_persisted----uspspace want reset scpdata!\n"); 
              memset(&scp_udata, 0, sizeof(scp_data));
              scp_udata.active_tms = active_tms_bp;
              scp_udata.debug  = sched_nbia_class_persisted_debug;
              goto out;
            } 
          }
          index++; 
       }
       pchTmp = NULL;
       optStr = NULL;
       optSubStr = NULL;
    }
out:
   mutex_unlock(&scp_data_lock);
   printk(KERN_INFO "sched_class_persisted_store finish\n"); 
   return retval;
}
static ssize_t sched_class_persisted_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   ssize_t retval = 0;
   char event_string[256] = { 0 };
   snprintf(event_string, sizeof(event_string), "scp_data:%d.%d\n", scp_udata.game_pid, scp_udata.tcomm_sz);
   retval = snprintf(buf, sizeof(event_string),"%s\n", event_string);
   return retval;
}
static ssize_t sched_class_persisted_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{                               
   ssize_t retval = 0;                                             
   char event_string[256] = { 0 };                                 
   mutex_lock(&scp_data_lock);
   snprintf(event_string, sizeof(event_string), "%d", scp_udata.debug);    
   mutex_unlock(&scp_data_lock);
   retval = snprintf(buf, sizeof(event_string),"%s", event_string);
   return retval;                                                  
}                                                                  

static ssize_t sched_class_persisted_debug_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) 
{
    ssize_t retval = 0;
    int debugValue = -1;
    char chBuffer[256] = { 0 };
    char *strTmp = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    retval = count;
    strTmp = chBuffer;
    debugValue = atoi(strTmp);
    sched_nbia_class_persisted_debug = debugValue;
    mutex_lock(&scp_data_lock);
    if(debugValue <= 1){
       scp_udata.debug = debugValue;
    }
    mutex_unlock(&scp_data_lock);
    strTmp = NULL;
    return retval;
}

static ssize_t sched_class_persisted_active_tms_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    ssize_t retval = 0;
    u64 active_tms = -1;
    char chBuffer[256] = { 0 };
    char *strTmp = NULL;
    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    retval = count;
    strTmp = chBuffer;
    active_tms = atoi(strTmp);
    mutex_lock(&scp_data_lock);
    if(active_tms <= MAX_ACTIVE_TMS){
      scp_udata.active_tms = active_tms * 1000000;
    }
    mutex_unlock(&scp_data_lock);
    strTmp = NULL;
    return retval;
}
ssize_t store_updown_migrate_handle(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count, bool is_u){
    ssize_t retval = 0;
    const char *pchDilem = "@";
    char chBuffer[256] = { 0 };
    char *pchTmp = NULL;
    char *optStr = NULL;
    int migrate_value[MAX_MARGIN_LEVELS] = { -1 };
    int index = 0;
	int i = 0, cpu;
	struct walt_sched_cluster *cluster;

    strncpy(chBuffer, buf, sizeof(chBuffer)-1);
    printk(KERN_INFO "store_updown_migrate_handle-buf:%s is_u:%d \n", buf, is_u);
    retval = count;
    if(strstr(chBuffer, pchDilem)){
       pchTmp = chBuffer;
       while((optStr = strsep(&pchTmp, pchDilem)) != NULL && index < MAX_MARGIN_LEVELS){
          if(strnlen(optStr, 8)){
            migrate_value[index++] = atoi(optStr);
          }
       }
       pchTmp = NULL;
       optStr = NULL;
       for (index = 0; index < MAX_MARGIN_LEVELS; index++) {
           if (migrate_value[index] < 0) return -EINVAL; 
       }
       mutex_lock(&ud_value_data_lock);
	   for_each_sched_cluster(cluster) {
	   	  /*
	   	   * No need to worry about CPUs in last cluster
	   	   * if there are more than 2 clusters in the system
	   	   */
	   	  for_each_cpu(cpu, &cluster->cpus) {
	   	  	  if (is_u) {
                  sched_early_up_migrate[cpu] = migrate_value[i];
                  printk(KERN_INFO "store_updown_migrate_handle update cpu:%d up-migrate-value:%d is_u:%d \n", cpu, sched_early_up_migrate[cpu], is_u);
              }	else {
                  sched_early_down_migrate[cpu] = migrate_value[i];
                  printk(KERN_INFO "store_updown_migrate_handle update cpu:%d down-migrate-value:%d is_u:%d \n", cpu, sched_early_down_migrate[cpu], is_u);
	   	      }
          }

	   	  if (++i >= num_sched_clusters - 1)
	   	  	break;
	   }
       mutex_unlock(&ud_value_data_lock);
   }
   return retval;
}
ssize_t show_updown_migrate_handle(struct kobject *kobj, struct kobj_attribute *attr, char *buf, bool is_u){
   int index = 0;
   int count = 0;
   mutex_lock(&ud_value_data_lock);
   if(is_u){
       for (index = 0; index < WALT_NR_CPUS; index++) {
           count += scnprintf(buf + count, PAGE_SIZE - count, " %d", sched_early_up_migrate[index]);
       }
   }else{
       for (index = 0; index < WALT_NR_CPUS; index++) {
           count += scnprintf(buf + count, PAGE_SIZE - count, " %d", sched_early_down_migrate[index]);
       }
   }    
   count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
   mutex_unlock(&ud_value_data_lock);
   return count;
}
static ssize_t sched_early_downmigrate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{                       
   return show_updown_migrate_handle(kobj, attr, buf, false);                                                  
}                                                             
static ssize_t sched_early_downmigrate_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
   return store_updown_migrate_handle(kobj, attr, buf, count, false);
}
static ssize_t sched_early_upmigrate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{                  
   return show_updown_migrate_handle(kobj, attr, buf, true);                                                  
}                                                             
static ssize_t sched_early_upmigrate_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
   return store_updown_migrate_handle(kobj, attr, buf, count, true);
}
static ssize_t sched_class_persisted_active_tms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{                      
   ssize_t retval = 0;                                             
   char event_string[256] = { 0 };                                 
   mutex_lock(&scp_data_lock);
   snprintf(event_string, sizeof(event_string), "%d", scp_udata.active_tms);    
   mutex_unlock(&scp_data_lock);
   retval = snprintf(buf, sizeof(event_string),"%s", event_string);
   return retval;                                                  
}                                   
kobj_attr(sched_nbia_loaded_tid);
kobj_attr(sched_nbia_render_tid_array);
kobj_attr(sched_nbia_rate_tns);
kobj_attr(sched_nbia_rbuffer_size);
kobj_attr(sched_nbia_queuework_enable);
kobj_attr(sched_prio_adjusted_info);
kobj_attr(sched_affinity_ctrl);
kobj_attr(sched_nbia_debug);
kobj_attr(sched_class_persisted);
kobj_attr(sched_class_persisted_active_tms);
kobj_attr(sched_class_persisted_debug);
kobj_attr(sched_early_upmigrate);
kobj_attr(sched_early_downmigrate);
static struct attribute * nbia_attrs[] = {
    &sched_nbia_loaded_tid_attr.attr,
    &sched_nbia_render_tid_array_attr.attr,
    &sched_nbia_rate_tns_attr.attr,
    &sched_nbia_rbuffer_size_attr.attr,
    &sched_nbia_queuework_enable_attr.attr,
    &sched_prio_adjusted_info_attr.attr,
    &sched_affinity_ctrl_attr.attr,
    &sched_nbia_debug_attr.attr,
    &sched_class_persisted_attr.attr,
    &sched_class_persisted_active_tms_attr.attr,
    &sched_class_persisted_debug_attr.attr,
    &sched_early_upmigrate_attr.attr,
    &sched_early_downmigrate_attr.attr,
    NULL
};
static const struct attribute_group nbia_attr_group = {
	.attrs = nbia_attrs,
};
void nbia_init(void){
    int error;
    memset(sched_nbia_render_tid_array, 0, NUM_RENDER_TID_ARRAY_SIZE);
    memset(sched_early_up_migrate, 0, WALT_NR_CPUS);
    memset(sched_early_down_migrate, 0, WALT_NR_CPUS);
    memset(sched_prio_adjusted_info, 0, PRIO_ADJUSTED_TID_ARRAY_SIZE * 5);
    memset(cmask_bundles, 0, sizeof(CMaskBundle) * PRIO_ADJUSTED_TID_ARRAY_SIZE);
    memset(&scp_udata, 0, sizeof(scp_data));
    scp_udata.active_tms = 32000000;//32毫秒
    ring_array_index = 0;
    bundles_array_index = 0;
    sched_nbia_debug = 0;
    sched_nbia_class_persisted_debug = 0;
    sched_nbia_queuework_enable = 1;
    sched_nbia_rate_tns = 0;
    kthread_init_worker(&worker);
    mutex_lock(&r_buffer_lock);
    if(NULL == r_buffer){
       r_buffer = createRingBuffer(RINGBUFFER_SIZE_MAX);
    }
    mutex_unlock(&r_buffer_lock);
    nbia_kobj = kobject_create_and_add("nbia", NULL);
    if (!nbia_kobj) {
       printk(KERN_INFO "nbia_init kobject_create_and_add failed!\n");
       return;
    }
    error = sysfs_create_group(nbia_kobj, &nbia_attr_group);
    if (error){
       printk(KERN_INFO "nbia_init sysfs_create_group failed! error:%d\n", error);
       return;
    }
    // register_trace_android_rvh_set_cpus_allowed_comm(android_rvh_set_cpus_allowed_comm, NULL);
    register_trace_android_vh_sched_setaffinity_early(nbia_sched_setaffinity_early, NULL);
    //创建kthread及worker，如果kthread及worker创建失败，直接退出
    thread = kthread_create(kthread_worker_fn, &worker, "kthread_nbia_walt");
    if (IS_ERR(thread)) {
        kthread_running = false;
        printk(KERN_INFO "create failed!");
    }else{
        wake_up_process(thread);
        kthread_running = true;
        printk(KERN_INFO "create success! kthread_running:%d\n", kthread_running);
    }
}
void nbia_fork_init(struct task_struct *p)
{
    struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
    if(p && p->real_cred)
        wts->is_app = (p->real_cred->uid.val >= FIRST_APPLICATION_UID);
    wts->persist_caller = -1;
    wts->mask_persisted = false;
    cpumask_copy(&wts->last, &p->cpus_mask);
    cpumask_copy(&wts->req, &p->cpus_mask);
    wts->scp_s = DEFAULT_SCPT_S;
    wts->old_prio = p->static_prio;
    wts->scpt_token = 0;
}
