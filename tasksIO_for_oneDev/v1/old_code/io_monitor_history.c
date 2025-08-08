#include "io_monitor_history.h"
#include "io_monitor.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/sched/signal.h>  // for task_struct and thread access
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/fs_struct.h>
#include <linux/filelock.h>  // 替换 file_lock_api.h

// 模块参数定义
// FIXME:可能要和其它参数定义在一起保证顺序
int interval_seconds = 30;
module_param(interval_seconds, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);  // 0644权限
MODULE_PARM_DESC(interval_seconds, "Monitoring interval in seconds (default: 30)"); // modinfo中查看

// 初始化全局变量
static LIST_HEAD(io_records);

// 添加自旋锁保护全局链表
static DEFINE_SPINLOCK(io_records_lock);

// 添加全局互斥锁
static DEFINE_MUTEX(io_monitor_mutex);

// 获取指定设备的 I/O 统计信息
static void get_dev_io_stats(struct task_struct *task, unsigned long *read_bytes, unsigned long *write_bytes)
{
    struct task_struct *thread;
    struct inode *inode;
    struct file *file;
    struct fdtable *fdt;
    int i;
    
    *read_bytes = 0;
    *write_bytes = 0;

    // 遍历进程的所有线程
    thread = task;
    do {
        if (!thread->files)
            continue;
            
        task_lock(thread);
        fdt = files_fdtable(thread->files);
        if (fdt) {
            for (i = 0; i < fdt->max_fds; i++) {
                file = fdt->fd[i];
                if (!file)
                    continue;
                    
                inode = file->f_inode;
                if (!inode || !inode->i_sb)
                    continue;
                    
                // 检查是否是目标设备的文件
                if (inode->i_sb->s_dev == target_dev_info.dev) {
                    // 从进程的IO统计中获取数据
                    *read_bytes += thread->ioac.read_bytes;
                    *write_bytes += thread->ioac.write_bytes;
                }
            }
        }
        task_unlock(thread);
    } while_each_thread(task, thread);
}

// 更新IO统计信息, 每个周期启动一次
static void update_io_stats(void)
{
    struct task_struct *task;
    struct io_record *record;
    struct list_head *task_io_stats_history_head;
    unsigned long tasks_num = 0;
    
    // 创建新记录
    record = kmalloc(sizeof(struct io_record), GFP_KERNEL);
    if (!record) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate io_record\n");
        return;
    }

    // 初始化task_stats链表头
    task_io_stats_history_head = kmalloc(sizeof(struct list_head), GFP_KERNEL);
    if (!task_io_stats_history_head) {
        kfree(record);
        printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate task_io_stats_history_head\n");
        return;
    }
    INIT_LIST_HEAD(task_io_stats_history_head);
    
    // 遍历所有进程
    rcu_read_lock();
    for_each_process(task) {
        unsigned long read_bytes = 0;
        unsigned long write_bytes = 0;
        
        get_dev_io_stats(task, &read_bytes, &write_bytes);
        
        // 如果进程有IO活动
        if (read_bytes > 0 || write_bytes > 0) {
            struct task_io_stats_history *task_stats;
            
            task_stats = kmalloc(sizeof(struct task_io_stats_history), GFP_KERNEL);
            if (!task_stats)
                continue;
                
            // 填充进程信息
            task_stats->pid = task->pid;
            task_stats->ppid = task->real_parent ? task->real_parent->pid : 0;
            strscpy(task_stats->comm, task->comm, TASK_COMM_LEN);
            strscpy(task_stats->parent_comm, 
                    task->real_parent ? task->real_parent->comm : "none",
                    TASK_COMM_LEN);
            task_stats->read_bytes = read_bytes;
            task_stats->write_bytes = write_bytes;
            
            // 添加到task_stats链表
            list_add_tail(&task_stats->list, task_io_stats_history_head);
            tasks_num++;
        }
    }
    rcu_read_unlock();
    
    // 填充record信息
    ktime_get_real_ts64(&record->timestamp);
    record->tasks_num = tasks_num;
    record->task_io_stats_history_head = task_io_stats_history_head;
    
    // 获取锁后添加到全局链表
    spin_lock(&io_records_lock);
    list_add_tail(&record->list, &io_records);
    spin_unlock(&io_records_lock);
    
    printk(KERN_INFO "[io_monitor | mod_2] %s: Updated IO stats: found %lu tasks with IO activity\n"
        "Record added at time %lld.%09ld\n", __func__ ,
        tasks_num, (long long)record->timestamp.tv_sec,
        record->timestamp.tv_nsec);
}

static void update_timer_callback(struct timer_list *t);
DEFINE_TIMER(update_timer, update_timer_callback);
// 定时更新函数
static void update_timer_callback(struct timer_list *t)
{
    update_io_stats();
    
    // 重新设置定时器
    mod_timer(&update_timer, 
              jiffies + msecs_to_jiffies(interval_seconds * 1000));

    printk(KERN_INFO "[io_monitor | mod_2] Timer reset, next update in %d seconds\n", 
                interval_seconds);
}

// 显示历史记录
static int io_monitor_show(struct seq_file *m, void *v)
{
    struct io_record *record, *tmp_record;
    struct task_io_stats_history *task_stats, *tmp_stats;
    int write_nums = 0;

    // 获取互斥锁
    if (!mutex_trylock(&io_monitor_mutex)) {
        // 如果无法立即获取锁，返回 EAGAIN
        return -EAGAIN;
    }

    // 获取自旋锁保护内存数据
    spin_lock(&io_records_lock);
    
    list_for_each_entry_safe(record, tmp_record, &io_records, list) {
        // 打印时间戳和任务数
        seq_printf(m, "%lld.%09ld %lu",
                  (long long)record->timestamp.tv_sec,
                  record->timestamp.tv_nsec,
                  record->tasks_num);
        
        // 打印所有task的统计信息
        list_for_each_entry_safe(task_stats, tmp_stats, 
                                record->task_io_stats_history_head, list) {
            seq_printf(m, " %s %d %s %d %lu %lu",
                      task_stats->comm,
                      task_stats->pid,
                      task_stats->parent_comm,
                      task_stats->ppid,
                      task_stats->read_bytes,
                      task_stats->write_bytes);
                      
            // 释放task统计节点
            list_del(&task_stats->list);
            kfree(task_stats);
        }
        
        seq_putc(m, '\n');
        
        // 释放record相关内存
        list_del(&record->list);
        kfree(record->task_io_stats_history_head);
        kfree(record);
        write_nums++;  // 记录写入的记录条数
    }
    
    // 释放自旋锁
    spin_unlock(&io_records_lock);

    // 释放互斥锁
    mutex_unlock(&io_monitor_mutex);
    
    if (write_nums == 0) {
        printk(KERN_INFO "[io_monitor | mod_2] %s: no task was writen in this time\n", __func__);
    }else{
        printk(KERN_INFO "[io_monitor | mod_2] %s: %d records were writen into /proc/io_monitor_mod2\n", __func__, write_nums);
    }

    return 0;
}

static int io_monitor_mod2_open(struct inode *inode, struct file *file)
{
    return single_open(file, io_monitor_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops io_monitor_mod2_fops = {
    .proc_open = io_monitor_mod2_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct file_operations io_monitor_mod2_fops = {
    .owner = THIS_MODULE,
    .open = io_monitor_mod2_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

// 初始化函数
int init_io_mod2(void)
{
    // 创建proc入口
    if (!proc_create("io_monitor_mod2", 0, NULL, &io_monitor_mod2_fops)) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to create io_monitor_mod2 proc entry\n");
        return -ENOMEM;
    }
    
    // 立即执行一次统计
    update_io_stats();
    // 启动定时器
    mod_timer(&update_timer, 
              jiffies + msecs_to_jiffies(interval_seconds * 1000));
              
    printk(KERN_INFO "[io_monitor | mod_2] Module 2 initialized\n");

    return 0;
}

// 清理函数
void cleanup_io_mod2(void)
{
    struct io_record *record, *tmp_record;
    struct task_io_stats_history *task_stats, *tmp_stats;
    
    // 删除定时器
    del_timer_sync(&update_timer);
    
    // 清理所有记录
    list_for_each_entry_safe(record, tmp_record, &io_records, list) {
        list_for_each_entry_safe(task_stats, tmp_stats, 
                                record->task_io_stats_history_head, list) {
            list_del(&task_stats->list);
            kfree(task_stats);
        }
        kfree(record->task_io_stats_history_head);
        list_del(&record->list);
        kfree(record);
    }
    
    // 移除proc入口
    remove_proc_entry("io_monitor_mod2", NULL);
    
    printk(KERN_INFO "[io_monitor | mod_2] Module 2 cleaned up\n");
}

/**
 * jiffies是内核全局变量，记录系统启动以来的时钟滴答数
 * 
 * 优化方向：
 * 1、对于频繁分配/释放的小内存结构，考虑使用kmem_cache代替kmalloc
 * 2、限制io_records链表的长度，防止内存耗尽
 */