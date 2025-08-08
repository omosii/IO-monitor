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
#include <linux/uaccess.h>

#define HISTORY_LOG_PATH "/tmp/io_monitor_history.txt"
#define LOG_BUFFER_SIZE 4096

// 模块参数定义
int interval_seconds = 30;
module_param(interval_seconds, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);  // 0644权限
MODULE_PARM_DESC(interval_seconds, "Monitoring interval in seconds (default: 30)"); // modinfo中查看

// 添加全局互斥锁保护文件操作
static DEFINE_MUTEX(io_history_file_mutex);

// 创建历史日志文件
static int create_history_file(void)
{
    struct file *history_file;
    int retry_count = 0;
    const int max_retries = 3;

    while (retry_count < max_retries) {
        history_file = filp_open(HISTORY_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (IS_ERR(history_file)) {
            long err = PTR_ERR(history_file);
            printk(KERN_WARNING "[io_monitor | mod_2] Failed to create history file (attempt %d/%d): %ld\n", 
                   retry_count + 1, max_retries, err);
            
            retry_count++;
            if (retry_count >= max_retries) {
                printk(KERN_ERR "[io_monitor | mod_2] Failed to create history file after %d attempts\n", max_retries);
                return err;
            }
            continue;
        }
        break;
    }
    
    filp_close(history_file, NULL);

    printk(KERN_INFO "[io_monitor | mod_2] History file created at: %s\n", HISTORY_LOG_PATH);
    return 0;
}

// 追加写入历史文件
static void append_to_history_file(const char *log_entry)
{
    struct file *history_file;
    loff_t pos = 0;
    int retry_count = 0;
    const int max_retries = 3;

    while (retry_count < max_retries) {
        history_file = filp_open(HISTORY_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (IS_ERR(history_file)) {
            long err = PTR_ERR(history_file);
            printk(KERN_WARNING "[io_monitor | mod_2] Failed to open history file (attempt %d/%d): %ld\n", 
                   retry_count + 1, max_retries, err);
            
            // 如果是文件不存在，尝试重新创建
            if (err == -ENOENT) {
                printk(KERN_INFO "[io_monitor | mod_2] File not found, attempting to recreate...\n");
                create_history_file();
                retry_count++;
                continue;
            }
            
            retry_count++;
            if (retry_count >= max_retries) {
                printk(KERN_ERR "[io_monitor | mod_2] Failed to open history file after %d attempts\n", max_retries);
                return;
            }
            continue;
        }
        break;
    }

    // 写入日志条目
    kernel_write(history_file, log_entry, strlen(log_entry), &pos);
    filp_close(history_file, NULL);
}

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

// 创建新的任务IO统计节点
static struct task_io_stats_history *create_task_io_stats(struct task_struct *task, 
                                                         unsigned long read_bytes, 
                                                         unsigned long write_bytes)
{
    struct task_io_stats_history *task_stats;
    
    task_stats = kmalloc(sizeof(struct task_io_stats_history), GFP_KERNEL);
    if (!task_stats)
        return NULL;
    
    task_stats->pid = task->pid;
    task_stats->ppid = task->real_parent ? task->real_parent->pid : 0;
    
    strncpy(task_stats->comm, task->comm, TASK_COMM_LEN);
    task_stats->comm[TASK_COMM_LEN - 1] = '\0';
    
    if (task->real_parent) {
        strncpy(task_stats->parent_comm, task->real_parent->comm, TASK_COMM_LEN);
        task_stats->parent_comm[TASK_COMM_LEN - 1] = '\0';
    } else {
        strncpy(task_stats->parent_comm, "none", TASK_COMM_LEN);
    }
    
    task_stats->read_bytes = read_bytes;
    task_stats->write_bytes = write_bytes;
    INIT_LIST_HEAD(&task_stats->list);
    
    return task_stats;
}

// 释放任务IO统计链表
static void free_task_stats_list(struct list_head *task_head)
{
    struct task_io_stats_history *task_stats, *tmp;
    
    if (!task_head)
        return;
        
    list_for_each_entry_safe(task_stats, tmp, task_head, list) {
        list_del(&task_stats->list);
        kfree(task_stats);
    }
    kfree(task_head);
}

// 将IO记录写入文件 - 兼容my_iostat.c的格式
static void write_io_record_to_file(struct io_record *record)
{
    struct task_io_stats_history *task_stats;
    char *log_buffer;
    char line_buffer[256];
    int pos = 0;
    int line_len;
    int required_size;
    
    // 首先计算所需的总缓冲区大小
    required_size = snprintf(NULL, 0, "%lld.%09ld %lu",
                            (long long)record->timestamp.tv_sec, 
                            record->timestamp.tv_nsec,
                            record->tasks_num);
    
    list_for_each_entry(task_stats, record->task_io_stats_history_head, list) {
        required_size += snprintf(NULL, 0, " %s %d %s %d %lu %lu",
                                 task_stats->comm,
                                 task_stats->pid,
                                 task_stats->parent_comm,
                                 task_stats->ppid,
                                 task_stats->read_bytes,
                                 task_stats->write_bytes);
    }
    required_size += 2; // 为换行符和终止符预留空间
    
    // 动态分配足够大的缓冲区
    if (required_size > LOG_BUFFER_SIZE) {
        log_buffer = kmalloc(required_size, GFP_KERNEL);
        if (!log_buffer) {
            printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate large log buffer (%d bytes)\n", required_size);
            return;
        }
    } else {
        log_buffer = kmalloc(LOG_BUFFER_SIZE, GFP_KERNEL);
        if (!log_buffer) {
            printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate log buffer\n");
            return;
        }
    }
    
    // 写入时间戳和任务数
    pos = snprintf(log_buffer, required_size, "%lld.%09ld %lu",
                   (long long)record->timestamp.tv_sec, 
                   record->timestamp.tv_nsec,
                   record->tasks_num);
    
    // 遍历任务链表，写入任务信息到同一行
    list_for_each_entry(task_stats, record->task_io_stats_history_head, list) {
        line_len = snprintf(line_buffer, sizeof(line_buffer), 
                           " %s %d %s %d %lu %lu",
                           task_stats->comm,
                           task_stats->pid,
                           task_stats->parent_comm,
                           task_stats->ppid,
                           task_stats->read_bytes,
                           task_stats->write_bytes);
        
        // 直接追加到缓冲区（已经确保空间足够）
        strcpy(log_buffer + pos, line_buffer);
        pos += line_len;
    }
    
    // 添加换行符结束这一行
    strcat(log_buffer, "\n");
    
    // 一次性写入整行内容
    mutex_lock(&io_history_file_mutex);
    append_to_history_file(log_buffer);
    mutex_unlock(&io_history_file_mutex);
    
    kfree(log_buffer);
}


// 更新IO统计信息, 每interval_seconds个周期启动一次
static void update_io_stats(void)
{
    struct task_struct *task;
    struct io_record *record;
    struct task_io_stats_history *task_stats;
    unsigned long read_bytes, write_bytes;
    
    // 创建新的IO记录
    record = kmalloc(sizeof(struct io_record), GFP_KERNEL);
    if (!record) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate io_record\n");
        return;
    }
    
    // 创建任务统计链表头
    record->task_io_stats_history_head = kmalloc(sizeof(struct list_head), GFP_KERNEL);
    if (!record->task_io_stats_history_head) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to allocate task_io_stats_history_head\n");
        kfree(record);
        return;
    }
    
    // 初始化记录
    ktime_get_real_ts64(&record->timestamp);
    record->tasks_num = 0;
    INIT_LIST_HEAD(record->task_io_stats_history_head);
    INIT_LIST_HEAD(&record->list);
    
    // 遍历所有进程，收集有IO活动的任务信息
    rcu_read_lock();
    for_each_process(task) {
        get_dev_io_stats(task, &read_bytes, &write_bytes);
        
        // 如果任务有IO活动，添加到链表
        if (read_bytes > 0 || write_bytes > 0) {
            task_stats = create_task_io_stats(task, read_bytes, write_bytes);
            if (task_stats) {
                list_add_tail(&task_stats->list, record->task_io_stats_history_head);
                record->tasks_num++;
            }
        }
    }
    rcu_read_unlock();
    
    // 如果有IO活动的任务，写入文件
    if (record->tasks_num > 0) {
        write_io_record_to_file(record);
        
        printk(KERN_INFO "[io_monitor | mod_2] %s: Updated IO stats: found %lu tasks with IO activity\n"
               "Record added at time %lld.%09ld\n", __func__,
               record->tasks_num, (long long)record->timestamp.tv_sec, record->timestamp.tv_nsec);
    } else {
        printk(KERN_INFO "[io_monitor | mod_2] %s: No IO activity detected at time %lld.%09ld\n", 
               __func__, (long long)record->timestamp.tv_sec, record->timestamp.tv_nsec);
    }
    
    // 清理内存
    free_task_stats_list(record->task_io_stats_history_head);
    kfree(record);
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

// 从历史文件读取内容
static ssize_t read_history_file_content(struct seq_file *m)
{
    struct file *history_file;
    char *buffer;
    ssize_t ret = 0;
    loff_t file_size;
    loff_t read_pos = 0;

    mutex_lock(&io_history_file_mutex);

    // 打开历史文件
    history_file = filp_open(HISTORY_LOG_PATH, O_RDONLY, 0);
    if (IS_ERR(history_file)) {
        mutex_unlock(&io_history_file_mutex);
        printk(KERN_INFO "[io_monitor | mod_2] History file not found: %s\n", HISTORY_LOG_PATH);
        return 0; // 文件不存在，返回空
    }

    // 获取文件大小
    file_size = vfs_llseek(history_file, 0, SEEK_END);
    vfs_llseek(history_file, 0, SEEK_SET);

    if (file_size <= 0) {
        filp_close(history_file, NULL);
        mutex_unlock(&io_history_file_mutex);
        return 0;
    }

    // 分配缓冲区
    buffer = kmalloc(file_size + 1, GFP_KERNEL);
    if (!buffer) {
        filp_close(history_file, NULL);
        mutex_unlock(&io_history_file_mutex);
        return -ENOMEM;
    }

    // 读取文件内容
    ret = kernel_read(history_file, buffer, file_size, &read_pos);

    if (ret > 0) {
        buffer[ret] = '\0';
        seq_printf(m, "%s", buffer);
    }

    kfree(buffer);
    filp_close(history_file, NULL);
    mutex_unlock(&io_history_file_mutex);

    return ret;
}

// 显示历史记录（从文件读取）
static int io_monitor_show(struct seq_file *m, void *v)
{
    ssize_t read_size;
    
    read_size = read_history_file_content(m);
    
    if (read_size > 0) {
        printk(KERN_INFO "[io_monitor | mod_2] %s: History content displayed (%zd bytes)\n", 
               __func__, read_size);
    } else {
        printk(KERN_INFO "[io_monitor | mod_2] %s: No history content available\n", __func__);
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
    int ret;
    
    // 创建历史文件
    ret = create_history_file();
    if (ret < 0) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to create history file\n");
        return ret;
    }
    
    // 创建proc入口
    if (!proc_create("io_monitor_mod2", 0444, NULL, &io_monitor_mod2_fops)) {
        printk(KERN_ERR "[io_monitor | mod_2] Failed to create io_monitor_mod2 proc entry\n");
        return -ENOMEM;
    }
    
    // 立即执行一次统计
    update_io_stats();
    // 启动定时器
    mod_timer(&update_timer, 
              jiffies + msecs_to_jiffies(interval_seconds * 1000));
              
    printk(KERN_INFO "[io_monitor | mod_2] Module 2 initialized\n");
    printk(KERN_INFO "[io_monitor | mod_2] History file location: %s\n", HISTORY_LOG_PATH);

    return 0;
}

// 清理函数
void cleanup_io_mod2(void)
{
    // 删除定时器
    del_timer_sync(&update_timer);
    
    // 移除proc入口
    remove_proc_entry("io_monitor_mod2", NULL);
    
    printk(KERN_INFO "[io_monitor | mod_2] Module 2 cleaned up\n");
    printk(KERN_INFO "[io_monitor | mod_2] History file preserved at: %s\n", HISTORY_LOG_PATH);
}