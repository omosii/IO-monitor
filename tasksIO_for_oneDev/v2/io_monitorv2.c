#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/atomic.h>
#include <linux/kprobes.h>  // 添加 kprobes 支持

#define MODULE_NAME "io_monitorv2"

static dev_t target_dev;  // 目标设备号
static atomic64_t total_read, total_write; // 全局统计

// 进程级统计结构体
struct proc_io_stats {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    atomic64_t read_bytes;
    atomic64_t write_bytes;
    struct hlist_node hash_node;
    struct rcu_head rcu; // 添加 RCU 释放用字段
};

// 哈希表存储进程统计（键为pid）
#define HASHTABLE_SIZE 1024
static struct hlist_head proc_stats_table[HASHTABLE_SIZE];
static DEFINE_SPINLOCK(hashtable_lock); // 保护哈希表操作

// 设备过滤规则（RCU保护）
struct filter_rule {
    dev_t dev;
    bool track_read;
    bool track_write;
    struct rcu_head rcu;
};
static struct filter_rule __rcu *current_rule;

// 为 kprobe 声明
static struct kprobe submit_bio_kp;

// 获取进程统计结构（不存在则创建）
static struct proc_io_stats *get_proc_stats(pid_t pid) {
    struct hlist_head *head = &proc_stats_table[pid % HASHTABLE_SIZE];
    struct proc_io_stats *stats;

    // 查找现有条目
    rcu_read_lock();
    hlist_for_each_entry_rcu(stats, head, hash_node) {
        if (stats->pid == pid) {
            rcu_read_unlock();
            return stats;
        }
    }
    rcu_read_unlock();

    // 新建条目
    stats = kmalloc(sizeof(*stats), GFP_KERNEL);
    if (!stats) return NULL;

    stats->pid = pid;
    get_task_comm(stats->comm, current);
    atomic64_set(&stats->read_bytes, 0);
    atomic64_set(&stats->write_bytes, 0);

    spin_lock(&hashtable_lock);
    hlist_add_head_rcu(&stats->hash_node, head);
    spin_unlock(&hashtable_lock);

    return stats;
}

// kprobe 前置处理函数
static int submit_bio_entry_handler(struct kprobe *p, struct pt_regs *regs)
{
    // struct bio: 描述块设备 I/O 请求的核心结构，包含目标设备、读写方向、数据大小等信息
    struct bio *bio = (struct bio *)regs->di;  // 第一个参数通常在 di 寄存器
    struct filter_rule *rule;
    dev_t bio_dev;
    
    if (!bio || !bio->bi_bdev)
        return 0;
    //      IO请求-> IO设备 -> 设备号    
    bio_dev = bio->bi_bdev->bd_dev;
    rule = rcu_dereference(current_rule);
    
    if (rule && bio_dev == rule->dev) {
        struct proc_io_stats *stats = get_proc_stats(task_pid_nr(current)); // task_pid_nr(current)返回当前进程全局PID
        u64 bytes = bio->bi_iter.bi_size;

        if (bio_data_dir(bio) == READ && rule->track_read) {
            atomic64_add(bytes, &total_read);
            if (stats) atomic64_add(bytes, &stats->read_bytes);
        } else if (bio_data_dir(bio) == WRITE && rule->track_write) {
            atomic64_add(bytes, &total_write);
            if (stats) atomic64_add(bytes, &stats->write_bytes);
        }
    }
    
    return 0;
}

// 更新过滤规则（用户空间通过sysfs触发）
// 通过RCU（Read-Copy-Update）机制更新全局的过滤规则（current_rule），确保在更新过程中不会阻塞读操作，且避免数据竞争。
static int update_rule(dev_t new_dev, bool track_r, bool track_w) {
    struct filter_rule *new_rule = kmalloc(sizeof(*new_rule), GFP_KERNEL);
    if (!new_rule) return -ENOMEM;

    new_rule->dev = new_dev;
    new_rule->track_read = track_r;
    new_rule->track_write = track_w;

    // 保存旧指针，用于后续释放
    struct filter_rule *old_rule = rcu_dereference_protected(current_rule, 1);

    rcu_assign_pointer(current_rule, new_rule);
    synchronize_rcu(); // 等待所有正在使用旧规则（current_rule）的读临界区（rcu_read_lock保护的区域）退出
    if (old_rule)
        kfree_rcu(old_rule, rcu); // 释放旧规则
    // kfree_rcu(current_rule, rcu);
    return 0;
}

// /proc 接口：显示全局和进程级统计
static int proc_show(struct seq_file *m, void *v) {
    struct proc_io_stats *stats;
    int i;

    seq_printf(m, "Target Device: %d:%d\n", MAJOR(target_dev), MINOR(target_dev));
    seq_printf(m, "Global Read: %lld bytes\n", atomic64_read(&total_read));
    seq_printf(m, "Global Write: %lld bytes\n\n", atomic64_read(&total_write));
    seq_puts(m, "Per-Process Statistics:\n");

    for (i = 0; i < HASHTABLE_SIZE; i++) {
        hlist_for_each_entry_rcu(stats, &proc_stats_table[i], hash_node) {
            seq_printf(m, "PID: %d, Comm: %s, Read: %lld bytes, Write: %lld bytes\n",
                      stats->pid, stats->comm,
                      atomic64_read(&stats->read_bytes),
                      atomic64_read(&stats->write_bytes));
        }
    }
    return 0;
}

// 模块初始化
static int __init io_stat_init(void) {
    int ret;
    
    // 初始化哈希表
    for (int i = 0; i < HASHTABLE_SIZE; i++)
        INIT_HLIST_HEAD(&proc_stats_table[i]);

    // 默认规则：跟踪 /dev/sda 的读写
    target_dev = MKDEV(8, 3); // 根据实际设备号修改
    update_rule(target_dev, true, true);

    // 创建 /proc/io_monitorv2
    proc_create_single(MODULE_NAME, 0, NULL, proc_show);
    
    // 设置 kprobe 跟踪 submit_bio 函数
    submit_bio_kp.symbol_name = "submit_bio";
    submit_bio_kp.pre_handler = submit_bio_entry_handler;
    /*
     * 当注册 kprobe 时，内核会替换目标函数（submit_bio）的第一条指令为断点指令（如 int3），触发断点后执行回调函数，再恢复原指令继续执行
    */
    ret = register_kprobe(&submit_bio_kp);
    if (ret < 0) {
        printk(KERN_ERR "%s: register_kprobe failed, returned %d\n", MODULE_NAME, ret);
        remove_proc_entry(MODULE_NAME, NULL);
        return ret;
    }
    
    printk(KERN_INFO "%s: Loaded, kprobe at %p\n", MODULE_NAME, submit_bio_kp.addr);
    return 0;
}

// 模块卸载
static void __exit io_stat_exit(void) {
    struct proc_io_stats *stats;
    struct hlist_node *tmp;
    int i;

    // 取消注册 kprobe
    unregister_kprobe(&submit_bio_kp);
    remove_proc_entry(MODULE_NAME, NULL);

    // 清理哈希表
    for (i = 0; i < HASHTABLE_SIZE; i++) {
        hlist_for_each_entry_safe(stats, tmp, &proc_stats_table[i], hash_node) {
            hlist_del_rcu(&stats->hash_node);
            kfree_rcu(stats, rcu);  // 现在 stats 有 rcu 成员
        }
    }

    kfree_rcu(current_rule, rcu);
    printk(KERN_INFO "%s: Unloaded\n", MODULE_NAME);
}

module_init(io_stat_init);
module_exit(io_stat_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luyi Zhang");
MODULE_DESCRIPTION("Per-Process I/O Statistics for Block Devices");