#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/workqueue.h> // 
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/irqflags.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/version.h>
#include "io_monitor_history.h"
#include "io_monitor_v3_main.h"

/* ================= 全局状态 ================= */
static struct hlist_head proc_stats_table[HASHTABLE_SIZE];
static DEFINE_SPINLOCK(hashtable_lock);

static struct filter_rule __rcu *current_rule;

static atomic64_t total_read_kb;
static atomic64_t total_write_kb;

static struct file *log_file; // FIXME 何时清空？
static DEFINE_MUTEX(log_mutex);

static struct work_struct write_log_work;

static LIST_HEAD(log_entry_list);
static DEFINE_SPINLOCK(log_list_lock);
static atomic_t log_entry_count = ATOMIC_INIT(0);

/* 字节转KB(向上取整) */
#define BYTES_TO_KB(b) (((b) + 1023ULL) / 1024ULL)

/* ================= 进程统计 ================= */
static struct proc_io_stats *get_proc_stats(pid_t pid)
{
    // struct hlist_head *head = &proc_stats_table[pid % HASHTABLE_SIZE];
    //struct proc_io_stats *stats;
    struct proc_io_stats *new_stats;
    // unsigned long flags;
    // bool found = false;
    struct task_struct *task, *parent;

    // rcu_read_lock();
    // hlist_for_each_entry_rcu(stats, head, hash_node) {
    //     if (stats->pid == pid) {
    //         found = true;
    //         break;
    //     }
    // }
    // rcu_read_unlock();
    // if (found)
    //     return stats;

    new_stats = kmalloc(sizeof(*new_stats), GFP_ATOMIC);
    if (!new_stats)
        return NULL;

    new_stats->pid = pid;
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
        strcpy(new_stats->comm, "unknown");
        strcpy(new_stats->pcomm, "unknown");
        new_stats->ppid = 0;
    }
    rcu_read_unlock();

    atomic64_set(&new_stats->read_kb, 0);
    atomic64_set(&new_stats->write_kb, 0);

    // spin_lock_irqsave(&hashtable_lock, flags);
    // hlist_for_each_entry_rcu(stats, head, hash_node) {
    //     if (stats->pid == pid) {
    //         spin_unlock_irqrestore(&hashtable_lock, flags);
    //         kfree(new_stats);
    //         return stats;
    //     }
    // }
    // hlist_add_head(&new_stats->hash_node, head);
    // spin_unlock_irqrestore(&hashtable_lock, flags);
    return new_stats;
}

/* ================= 日志缓冲与写入 ================= */
static void add_log_entry(char *buffer)
{
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
    spin_lock_irqsave(&log_list_lock, flags); // 获取日志自旋锁并禁用中断
    list_add_tail(&entry->list, &log_entry_list);
    spin_unlock_irqrestore(&log_list_lock, flags);
    atomic_inc(&log_entry_count);
}

static char *get_next_log_entry(void)
{
    struct log_entry *entry;
    char *buf = NULL;
    unsigned long flags;

    spin_lock_irqsave(&log_list_lock, flags);
    if (!list_empty(&log_entry_list)) {
        entry = list_first_entry(&log_entry_list, struct log_entry, list);
        list_del(&entry->list);
        buf = entry->buffer;
        kfree(entry);
        atomic_dec(&log_entry_count);
    }
    spin_unlock_irqrestore(&log_list_lock, flags);
    return buf;
}

static void write_log_worker(struct work_struct *work)
{
    char *log_buffer;
    int len;
    loff_t pos = 0;
    struct file *f;

    if (!log_file)
        return;

    mutex_lock(&log_mutex);
    f = log_file;

    /* 恢复原版本的 flock 保护(多数情况下可能为空或未实现，保持兼容) */
    if (f->f_op && f->f_op->flock && f->f_op->flock(f, F_SETLK, NULL) == 0) {
        while ((log_buffer = get_next_log_entry()) != NULL) {
            len = strlen(log_buffer);
            kernel_write(f, log_buffer, len, &pos);
            kfree(log_buffer);
        }
        f->f_op->flock(f, F_UNLCK, NULL);
    } else {
        while ((log_buffer = get_next_log_entry()) != NULL) {
            kernel_write(f, log_buffer, strlen(log_buffer), &pos);
            kfree(log_buffer);
        }
    }

    mutex_unlock(&log_mutex);
}

static void write_log_entry(struct bio *bio, struct proc_io_stats *stats, bool is_read)
{
    char *buf;
    unsigned long long bytes = bio->bi_iter.bi_size;
    unsigned long long delta_kb = BYTES_TO_KB(bytes);

    atomic64_add(delta_kb, is_read ? &total_read_kb : &total_write_kb);
    if (stats)
        atomic64_add(delta_kb,
                     is_read ? &stats->read_kb : &stats->write_kb);

    if (in_interrupt() || !log_file)
        return;

    buf = kmalloc(MAX_LOG_ENTRY_SIZE, GFP_ATOMIC);
    if (!buf)
        return;

    /* 时间戳 */
    {
        struct timespec64 ts;
        ktime_get_real_ts64(&ts);
        snprintf(buf, MAX_LOG_ENTRY_SIZE,
                 "%lld.%09lld %d %d %d %s %d %s %llu %llu\n",
                 (long long)ts.tv_sec, (long long)ts.tv_nsec,
                 MAJOR(bio->bi_bdev->bd_dev), MINOR(bio->bi_bdev->bd_dev),
                 stats ? stats->pid : 0, stats ? stats->comm : "unknown",
                 stats ? stats->ppid : 0, stats ? stats->pcomm : "unknown",
                 (unsigned long long)(is_read ? delta_kb : 0ULL),
                 (unsigned long long)(is_read ? 0ULL : delta_kb));
    }
    // FIXME 这里就可以释放stats了
    kfree(stats); // 释放 stats 内存
    add_log_entry(buf);
    schedule_work(&write_log_work);
}

/* ================= 过滤规则 ================= */
int update_filter_rule(dev_t dev, bool track_r, bool track_w)
{
    struct filter_rule *new_rule, *old_rule;

    new_rule = kmalloc(sizeof(*new_rule), GFP_KERNEL);
    if (!new_rule)
        return -ENOMEM;
    new_rule->dev = dev;
    new_rule->track_read = track_r;
    new_rule->track_write = track_w;

    old_rule = rcu_dereference_protected(current_rule, 1);
    rcu_assign_pointer(current_rule, new_rule);
    if (old_rule)
        call_rcu(&old_rule->rcu, (void (*)(struct rcu_head *))kfree);
    return 0;
}

/* ================= 日志文件系统 ================= */
static int create_log_file(void)
{
    struct file *f;

    f = filp_open(LOG_FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0644);
    if (IS_ERR(f))
        return PTR_ERR(f);
    log_file = f;
    printk(KERN_INFO "%s: Log file created at %s\n", MODULE_NAME, LOG_FILE_PATH);
    return 0;
}

static int init_log_system(void)
{
    return create_log_file();
}

static void free_pending_log_entries(void)
{
    struct log_entry *e, *n;
    unsigned long flags;

    spin_lock_irqsave(&log_list_lock, flags);
    list_for_each_entry_safe(e, n, &log_entry_list, list) {
        list_del(&e->list);
        kfree(e->buffer);
        kfree(e);
    }
    spin_unlock_irqrestore(&log_list_lock, flags);
}

static void cleanup_log_system(void)
{
    cancel_work_sync(&write_log_work);
    free_pending_log_entries();
    if (log_file) {
        mutex_lock(&log_mutex);
        filp_close(log_file, NULL);
        log_file = NULL;
        mutex_unlock(&log_mutex);
    }
}

/* ========= 新增: 供 realtime (/proc) 使用的日志访问接口 ========= */
struct file *io_log_file_get(void)
{
    struct file *f = NULL;
    mutex_lock(&log_mutex);
    if (log_file) {
        get_file(log_file);
        f = log_file;
    }
    mutex_unlock(&log_mutex);
    return f;
}

void io_log_read_lock(void)
{
    mutex_lock(&log_mutex);
}

void io_log_read_unlock(void)
{
    mutex_unlock(&log_mutex);
}

/* ================= RCU 回调 ================= */
static void free_proc_stats_rcu(struct rcu_head *rcu)
{
    struct proc_io_stats *s = container_of(rcu, struct proc_io_stats, rcu);
    kfree(s);
}

/* ================= 对外 bio 处理接口 ================= */
void handle_bio_request(struct bio *bio)
{
    struct filter_rule *rule;
    struct proc_io_stats *stats;
    bool is_read;

    if (unlikely(!bio || !bio->bi_bdev))
        return;

    rule = rcu_dereference(current_rule);
    if (!rule || rule->dev != bio->bi_bdev->bd_dev)
        return;

    is_read = (bio_data_dir(bio) == READ);
    if ((is_read && !rule->track_read) ||
        (!is_read && !rule->track_write))
        return;

    stats = get_proc_stats(task_pid_nr(current)); // FIXME 给日志的应该是新的结构体，而不是以前的，新生成的stats要防止内存泄漏
    write_log_entry(bio, stats, is_read);
}

/* ================= 初始化与清理 ================= */
int init_io_history(void)
{
    int i, ret;

    for (i = 0; i < HASHTABLE_SIZE; i++)
        INIT_HLIST_HEAD(&proc_stats_table[i]);

    INIT_WORK(&write_log_work, write_log_worker); // 初始化工作队列

    ret = init_log_system();
    if (ret) {
        return ret;
    }

    ret = update_filter_rule(get_target_dev(), true, true);
    if (ret) {
        cleanup_log_system();
        return ret;
    }

    return 0;
}

void cleanup_io_history(void)
{
    int i;
    struct proc_io_stats *s;
    struct hlist_node *tmp;

    cleanup_log_system();

    for (i = 0; i < HASHTABLE_SIZE; i++) {
        unsigned long flags;
        spin_lock_irqsave(&hashtable_lock, flags);
        hlist_for_each_entry_safe(s, tmp, &proc_stats_table[i], hash_node) {
            hlist_del(&s->hash_node);
            call_rcu(&s->rcu, free_proc_stats_rcu);
        }
        spin_unlock_irqrestore(&hashtable_lock, flags);
    }

    if (current_rule) {
        struct filter_rule *old = rcu_dereference_protected(current_rule, 1);
        if (old) {
            rcu_assign_pointer(current_rule, NULL);
            call_rcu(&old->rcu, (void (*)(struct rcu_head *))kfree);
        }
    }
}
