#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/kprobes.h>
#include <linux/namei.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/irqflags.h>
#include <linux/fs_struct.h>
#include <linux/fcntl.h>
#include <linux/file.h>

#define MODULE_NAME "io_monitor_v3"
#define LOG_FILE_PATH "/tmp/io_monitor.log"
#define MAX_DEVICE_NAME 64

// 模块参数
static char target_device[MAX_DEVICE_NAME] = "sda3";
module_param_string(device, target_device, MAX_DEVICE_NAME, 0644);
MODULE_PARM_DESC(device, "Target device name (e.g., sda3, sdb3)");

static dev_t target_dev;  // 目标设备号
static atomic64_t total_read, total_write; // 全局统计

// 进程级统计结构体
struct proc_io_stats {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    pid_t ppid;                    // 父进程PID
    char pcomm[TASK_COMM_LEN];     // 父进程名
    atomic64_t read_bytes;
    atomic64_t write_bytes;
    struct hlist_node hash_node;
    struct rcu_head rcu;
};

// 哈希表存储进程统计（键为pid）
#define HASHTABLE_SIZE 1024
static struct hlist_head proc_stats_table[HASHTABLE_SIZE];
static DEFINE_SPINLOCK(hashtable_lock);

// 设备过滤规则（RCU保护）
struct filter_rule {
    dev_t dev;
    bool track_read;
    bool track_write;
    struct rcu_head rcu;
};
static struct filter_rule __rcu *current_rule;

// kprobe 结构
static struct kprobe submit_bio_kp;

// 日志文件相关
static struct file *log_file;
static DEFINE_MUTEX(log_mutex);

// 在模块顶部声明
static struct work_struct write_log_work;

// 在文件开头添加日志缓冲区相关定义
#define MAX_LOG_ENTRIES 1024
#define MAX_LOG_ENTRY_SIZE 256

struct log_entry {
    char *buffer;
    struct list_head list;
};

static LIST_HEAD(log_entry_list);
static DEFINE_SPINLOCK(log_list_lock);
static atomic_t log_entry_count = ATOMIC_INIT(0);

// 获取进程统计结构（不存在则创建）
static struct proc_io_stats *get_proc_stats(pid_t pid) {
    struct hlist_head *head = &proc_stats_table[pid % HASHTABLE_SIZE];
    struct proc_io_stats *stats, *new_stats = NULL;
    struct task_struct *task;
    struct task_struct *parent;
    bool found = false;
    unsigned long flags;

    // 首先尝试查找现有条目
    rcu_read_lock();
    hlist_for_each_entry_rcu(stats, head, hash_node) {
        if (stats->pid == pid) {
            found = true;
            break;
        }
    }
    rcu_read_unlock();

    if (found) {
        return stats;
    }

    // 创建新条目（在自旋锁外分配内存）
    new_stats = kmalloc(sizeof(*new_stats), GFP_ATOMIC);
    if (!new_stats) return NULL;

    new_stats->pid = pid;
    
    // 安全地获取进程信息
    rcu_read_lock();
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (task) {
        get_task_comm(new_stats->comm, task);
        parent = rcu_dereference(task->real_parent);
        if (parent) {
            new_stats->ppid = parent->pid;
            get_task_comm(new_stats->pcomm, parent);
        } else {
            new_stats->ppid = 0;
            strcpy(new_stats->pcomm, "unknown");
        }
    } else {
        new_stats->ppid = 0;
        strcpy(new_stats->comm, "unknown");
        strcpy(new_stats->pcomm, "unknown");
    }
    rcu_read_unlock();
    
    atomic64_set(&new_stats->read_bytes, 0);
    atomic64_set(&new_stats->write_bytes, 0);

    // 使用自旋锁保护哈希表操作
    spin_lock_irqsave(&hashtable_lock, flags);
    
    // 再次检查是否在创建过程中有其他进程已经创建了相同的条目
    hlist_for_each_entry_rcu(stats, head, hash_node) {
        if (stats->pid == pid) {
            spin_unlock_irqrestore(&hashtable_lock, flags);
            kfree(new_stats);
            return stats;
        }
    }
    
    // 添加到哈希表
    hlist_add_head(&new_stats->hash_node, head);
    spin_unlock_irqrestore(&hashtable_lock, flags);

    return new_stats;
}

// 添加日志条目管理函数
static void add_log_entry(char *buffer) {
    struct log_entry *entry;
    unsigned long flags;
    
    if (atomic_read(&log_entry_count) >= MAX_LOG_ENTRIES) {
        kfree(buffer);
        return;
    }
    
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
        kfree(buffer);
        return;
    }
    
    entry->buffer = buffer;
    
    spin_lock_irqsave(&log_list_lock, flags);
    list_add_tail(&entry->list, &log_entry_list);
    spin_unlock_irqrestore(&log_list_lock, flags);
    
    atomic_inc(&log_entry_count);
}

static char *get_next_log_entry(void) {
    struct log_entry *entry;
    char *buffer = NULL;
    unsigned long flags;
    
    spin_lock_irqsave(&log_list_lock, flags);
    if (!list_empty(&log_entry_list)) {
        entry = list_first_entry(&log_entry_list, struct log_entry, list);
        list_del(&entry->list);
        buffer = entry->buffer;
        kfree(entry);
        atomic_dec(&log_entry_count);
    }
    spin_unlock_irqrestore(&log_list_lock, flags);
    
    return buffer;
}

// 修改 write_log_entry 函数
static void write_log_entry(struct bio *bio, struct proc_io_stats *stats, bool is_read) {
    char *log_buffer;
    
    // 更新统计数据
    atomic64_add(bio->bi_iter.bi_size, is_read ? &total_read : &total_write);
    if (stats) {
        atomic64_add(bio->bi_iter.bi_size, 
            is_read ? &stats->read_bytes : &stats->write_bytes);
    }
    
    // 创建日志条目
    if (!in_interrupt() && log_file) {
        log_buffer = kmalloc(MAX_LOG_ENTRY_SIZE, GFP_ATOMIC);
        if (log_buffer) {
            struct timespec64 ts;
            ktime_get_real_ts64(&ts);
            
            snprintf(log_buffer, MAX_LOG_ENTRY_SIZE,
                "%lld.%09lld %d %d %d %s %d %s %lld %lld\n",
                (long long)ts.tv_sec, (long long)ts.tv_nsec,
                MAJOR(bio->bi_bdev->bd_dev), MINOR(bio->bi_bdev->bd_dev),
                stats ? stats->pid : 0, stats ? stats->comm : "unknown",
                stats ? stats->ppid : 0, stats ? stats->pcomm : "unknown",
                is_read ? (unsigned long long)bio->bi_iter.bi_size : 0,
                is_read ? 0 : (unsigned long long)bio->bi_iter.bi_size);
                
            add_log_entry(log_buffer);
            schedule_work(&write_log_work);
        }
    }
}

// 修改 submit_bio_entry_handler 函数
static int submit_bio_entry_handler(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio = (struct bio *)regs->di;
    struct filter_rule *rule;
    struct proc_io_stats *stats = NULL;
    dev_t bio_dev;
    
    if (!bio || !bio->bi_bdev)
        return 0;
        
    bio_dev = bio->bi_bdev->bd_dev;
    rule = rcu_dereference(current_rule);
    
    if (!rule || bio_dev != rule->dev)
        return 0;
        
    // 使用 GFP_ATOMIC 避免阻塞
    stats = get_proc_stats(task_pid_nr(current));
    
    if ((bio_data_dir(bio) == READ && rule->track_read) ||
        (bio_data_dir(bio) == WRITE && rule->track_write)) {
        write_log_entry(bio, stats, bio_data_dir(bio) == READ);
    }
    
    return 0;
}

// 根据设备名获取设备号
static int get_device_number(const char *device_name) {
    struct path path;
    struct kstat stat;
    int ret;
    char full_path[128];
    
    // 构造完整的设备路径
    snprintf(full_path, sizeof(full_path), "/dev/%s", device_name);
    
    ret = kern_path(full_path, LOOKUP_FOLLOW, &path);
    if (ret) {
        printk(KERN_ERR "%s: Failed to resolve device path %s, error %d\n", 
               MODULE_NAME, full_path, ret);
        return ret;
    }
    
    ret = vfs_getattr(&path, &stat, STATX_INO, AT_STATX_SYNC_AS_STAT);
    path_put(&path);
    
    if (ret) {
        printk(KERN_ERR "%s: Failed to get device stats for %s, error %d\n", 
               MODULE_NAME, full_path, ret);
        return ret;
    }
    
    target_dev = stat.rdev;
    printk(KERN_INFO "%s: Target device %s resolved to %d:%d\n", 
           MODULE_NAME, full_path, MAJOR(target_dev), MINOR(target_dev));
    
    return 0;
}

// 更新过滤规则（优化版本，避免阻塞）
static int update_rule(dev_t new_dev, bool track_r, bool track_w) {
    struct filter_rule *new_rule = kmalloc(sizeof(*new_rule), GFP_KERNEL);
    if (!new_rule) return -ENOMEM;

    new_rule->dev = new_dev;
    new_rule->track_read = track_r;
    new_rule->track_write = track_w;

    // 使用 RCU 更新指针，避免阻塞
    struct filter_rule *old_rule = rcu_dereference_protected(current_rule, 1);
    rcu_assign_pointer(current_rule, new_rule);
    
    // 使用 call_rcu 而不是 synchronize_rcu，避免阻塞
    if (old_rule) {
        call_rcu(&old_rule->rcu, (void (*)(struct rcu_head *))kfree);
    }
    
    return 0;
}

// 创建日志文件
static int create_log_file(void) {
    struct file *file;
    
    // 尝试打开日志文件
    file = filp_open(LOG_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    
    if (IS_ERR(file)) {
        int ret = PTR_ERR(file);
        printk(KERN_ERR "%s: Failed to create log file %s, error %d\n", 
               MODULE_NAME, LOG_FILE_PATH, ret);
        return ret;
    }
    
    log_file = file;
    printk(KERN_INFO "%s: Log file created at %s\n", MODULE_NAME, LOG_FILE_PATH);
    return 0;
}

// 修改 write_log_worker 函数
static void write_log_worker(struct work_struct *work) {
    char *log_buffer;
    int len;
    loff_t pos = 0;
    struct file *f = log_file;
    
    if (!f)
        return;
        
    mutex_lock(&log_mutex);
    
    // 使用内核的文件锁机制
    if (f->f_op->flock && f->f_op->flock(f, F_SETLK, NULL) == 0) {
        // 写入日志
        while ((log_buffer = get_next_log_entry()) != NULL) {
            len = strlen(log_buffer);
            kernel_write(f, log_buffer, len, &pos);
            kfree(log_buffer);
        }
        
        // 释放锁
        f->f_op->flock(f, F_UNLCK, NULL);
    }
    
    mutex_unlock(&log_mutex);
}

// /proc 接口：显示全局和进程级统计
static int proc_show(struct seq_file *m, void *v) {
    struct proc_io_stats *stats;
    int i;

    seq_printf(m, "IO Monitor v3 - Target Device: /dev/%s (%d:%d)\n", 
               target_device, MAJOR(target_dev), MINOR(target_dev));
    seq_printf(m, "Global Read: %lld bytes\n", atomic64_read(&total_read));
    seq_printf(m, "Global Write: %lld bytes\n\n", atomic64_read(&total_write));
    seq_puts(m, "Per-Process Statistics:\n");
    seq_puts(m, "PID\t\tCommand\t\t\tPPID\t\tPCommand\t\tRead(bytes)\tWrite(bytes)\n");
    seq_puts(m, "---\t\t-------\t\t\t----\t\t--------\t\t-----------\t------------\n");

    rcu_read_lock();
    for (i = 0; i < HASHTABLE_SIZE; i++) {
        hlist_for_each_entry_rcu(stats, &proc_stats_table[i], hash_node) {
            seq_printf(m, "%-8d\t%-16s\t%-8d\t%-16s\t%lld\t\t%lld\n",
                      stats->pid, stats->comm,
                      stats->ppid, stats->pcomm,
                      atomic64_read(&stats->read_bytes),
                      atomic64_read(&stats->write_bytes));
        }
    }
    rcu_read_unlock();
    return 0;
}

// 模块初始化
static int __init io_monitor_init(void) {
    int ret;
    
    printk(KERN_INFO "%s: Initializing IO Monitor v3 for device /dev/%s\n", 
           MODULE_NAME, target_device);
    
    // 初始化哈希表
    for (int i = 0; i < HASHTABLE_SIZE; i++)
        INIT_HLIST_HEAD(&proc_stats_table[i]);

    // 根据设备名获取设备号
    ret = get_device_number(target_device);
    if (ret) {
        printk(KERN_ERR "%s: Failed to resolve device name\n", MODULE_NAME);
        return ret;
    }

    // 设置过滤规则
    ret = update_rule(target_dev, true, true);
    if (ret) {
        printk(KERN_ERR "%s: Failed to set filter rule\n", MODULE_NAME);
        return ret;
    }

    // 创建日志文件
    ret = create_log_file();
    if (ret) {
        printk(KERN_ERR "%s: Failed to create log file\n", MODULE_NAME);
        return ret;
    }

    // 创建 /proc/io_monitor_v3
    proc_create_single(MODULE_NAME, 0, NULL, proc_show);
    
    // 设置 kprobe 跟踪 submit_bio 函数
    submit_bio_kp.symbol_name = "submit_bio";
    submit_bio_kp.pre_handler = submit_bio_entry_handler;
    
    ret = register_kprobe(&submit_bio_kp);
    if (ret < 0) {
        printk(KERN_ERR "%s: Failed to register kprobe, error %d\n", MODULE_NAME, ret);
        remove_proc_entry(MODULE_NAME, NULL);
        if (log_file) {
            filp_close(log_file, NULL);
            log_file = NULL;
        }
        return ret;
    }
    
    INIT_WORK(&write_log_work, write_log_worker);
    
    printk(KERN_INFO "%s: Successfully loaded, kprobe registered at %p\n", 
           MODULE_NAME, submit_bio_kp.addr);
    printk(KERN_INFO "%s: Monitoring device /dev/%s (%d:%d)\n", 
           MODULE_NAME, target_device, MAJOR(target_dev), MINOR(target_dev));
    printk(KERN_INFO "%s: Log file: %s\n", MODULE_NAME, LOG_FILE_PATH);
    
    return 0;
}

// 添加 RCU 回调函数
static void free_proc_stats_rcu(struct rcu_head *rcu)
{
    struct proc_io_stats *stats = container_of(rcu, struct proc_io_stats, rcu);
    kfree(stats);
}

// 修改模块退出函数
static void __exit io_monitor_exit(void)
{
    struct proc_io_stats *stats;
    struct hlist_node *tmp;
    struct log_entry *entry, *n;
    int i;
    
    printk(KERN_INFO "%s: Unloading module...\n", MODULE_NAME);
    
    // 取消注册 kprobe
    unregister_kprobe(&submit_bio_kp);
    
    // 取消所有待处理的工作
    cancel_work_sync(&write_log_work);
    
    // 清理所有未处理的日志条目
    spin_lock(&log_list_lock);
    list_for_each_entry_safe(entry, n, &log_entry_list, list) {
        list_del(&entry->list);
        kfree(entry->buffer);
        kfree(entry);
    }
    spin_unlock(&log_list_lock);

    // 关闭日志文件
    if (log_file) {
        mutex_lock(&log_mutex);
        filp_close(log_file, NULL);
        log_file = NULL;
        mutex_unlock(&log_mutex);
    }

    // 清理哈希表，使用 call_rcu
    for (i = 0; i < HASHTABLE_SIZE; i++) {
        spin_lock(&hashtable_lock);
        hlist_for_each_entry_safe(stats, tmp, &proc_stats_table[i], hash_node) {
            hlist_del(&stats->hash_node);
            call_rcu(&stats->rcu, free_proc_stats_rcu);
        }
        spin_unlock(&hashtable_lock);
    }

    // 清理过滤规则
    if (current_rule) {
        struct filter_rule *old_rule = rcu_dereference_protected(current_rule, 1);
        if (old_rule) {
            rcu_assign_pointer(current_rule, NULL);
            call_rcu(&old_rule->rcu, (void (*)(struct rcu_head *))kfree);
        }
    }

    // 移除 proc 条目
    remove_proc_entry(MODULE_NAME, NULL);

    printk(KERN_INFO "%s: Module unload initiated\n", MODULE_NAME);
}

module_init(io_monitor_init);
module_exit(io_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IO Monitor Team");
MODULE_DESCRIPTION("IO Monitor v3 - Monitor IO operations for specific devices using kprobes");
MODULE_VERSION("3.0");
