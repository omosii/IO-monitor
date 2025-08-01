#include "io_monitor.h" // 包含头文件 io_monitor.h
#include <linux/fs.h> // 包含文件系统相关的函数和结构体
#include <linux/uaccess.h>  // 包含用户空间访问相关的函数和宏   
#include <linux/blkdev.h> // 包含块设备相关的函数和结构体
#include <linux/fdtable.h>    // 用于 files_fdtable
#include <linux/file.h>       // 用于 fget
#include <linux/blk_types.h>  // 用于块设备相关类型
#include <linux/buffer_head.h>  // 添加这个头文件
#include <linux/init.h>         // 添加这个头文件
#include <linux/part_stat.h>    // 添加这个头文件
#include <linux/atomic.h>       // 添加这个头文件，用于 atomic64_t
#include <linux/ktime.h> // 添加这个头文件，用于时间相关函数
#include <linux/delay.h>  // 添加这行，用于 msleep 函数

// #if !defined(CONFIG_GENERIC_ATOMIC64)
//     typedef struct {
//         atomic_t counter;
//     } atomic64_t;
// #endif
static char target_device[DISK_NAME_LEN]; // 受统计设备名
module_param_string(device, target_device, DISK_NAME_LEN, 0644);

// 定义全局设备信息结构体
struct device_info target_dev_info = {0};

// 全局哈希表锁
static spinlock_t io_stats_hash_lock;
// 定义进程状态结构体 struct task_io_stats 的哈希桶数组
struct hlist_head task_io_stats_hash[IO_STATS_HASH_SIZE];


static struct task_io_stats *find_node(int target, struct hlist_head *task_io_stats_hash) {
    int key = target % IO_STATS_HASH_SIZE; // 简单哈希函数
    struct task_io_stats *entry;
    spin_lock(&io_stats_hash_lock);  // 获取锁
    hlist_for_each_entry(entry, &task_io_stats_hash[key], task_hnode) {
        if (entry->pid == target){
            spin_unlock(&io_stats_hash_lock);  // 释放锁
            return entry; // 返回找到的结构体
        }      
    }
    spin_unlock(&io_stats_hash_lock);  // 释放锁
    return NULL; // 未找到
}
static void init_my_hlist_head(void){
    // task_io_stats_hash初始化
    spin_lock(&io_stats_hash_lock);
    for (int i = 0; i < IO_STATS_HASH_SIZE; i++) {
        INIT_HLIST_HEAD(&task_io_stats_hash[i]);
    }
    spin_unlock(&io_stats_hash_lock);
}
static void clear_hlist_head(void){
    int i;
    struct task_io_stats *entry;
    struct hlist_node *tmp;

    // 清理哈希表中的所有条目
    spin_lock(&io_stats_hash_lock);
    for (i = 0; i < IO_STATS_HASH_SIZE; i++) {
        hlist_for_each_entry_safe(entry, tmp, &task_io_stats_hash[i], task_hnode) {
            if (entry) {  // 添加空指针检查
                hlist_del(&entry->task_hnode);
                kfree(entry);
            }
        }
    }
    spin_unlock(&io_stats_hash_lock);
    printk(KERN_INFO "IO Monitor: hash table cleared\n");
}

// 获取指定设备的 I/O 统计信息
static void get_dev_io_stats(struct task_struct *task, unsigned long *read_bytes, unsigned long *write_bytes, u64* start_ns)
{
    // 参数验证
    if (!task || !read_bytes || !write_bytes || !start_ns) {
        printk(KERN_ERR "IO Monitor: NULL pointer passed to get_dev_io_stats\n");
        return;
    }

    struct task_struct *thread;
    struct inode *inode;
    struct file *file;
    struct fdtable *fdt;
    int i;
    
    *read_bytes = 0;
    *write_bytes = 0;

    // 如果设备无效，直接返回
    if (!target_dev_info.valid)
        return;

    // 使用原子变量来累计 IO 统计，避免在多核系统上的竞态条件
    atomic64_t total_read = ATOMIC64_INIT(0);
    atomic64_t total_write = ATOMIC64_INIT(0);
    
    // 遍历进程的所有线程
    thread = task;
    do {
        // 优先检查线程是否有文件表
        if (!thread->files)
            continue;
            
        task_lock(thread);
        *start_ns = ktime_get_ns(); // 获取当前时间戳

        fdt = files_fdtable(thread->files);
        if (fdt) {
            // 遍历所有文件，不能提前退出
            for (i = 0; i < fdt->max_fds; i++) {
                file = fdt->fd[i];
                if (!file)
                    continue;
                    
                inode = file->f_inode;
                // 合并条件检查以减少分支预测失败
                if (!inode || !inode->i_sb)
                    continue;
                    
                // 检查是否是目标设备的文件
                if (inode->i_sb->s_dev == target_dev_info.dev) {
                    // 每个属于目标设备的文件都需要累计其 IO 统计
                    atomic64_add(thread->ioac.read_bytes, &total_read);
                    atomic64_add(thread->ioac.write_bytes, &total_write);
                }
            }
        }
        task_unlock(thread);
    } while_each_thread(task, thread);
    
    // 最后一次性获取累计结果
    *read_bytes = atomic64_read(&total_read);
    *write_bytes = atomic64_read(&total_write);
}

static int io_monitor_show(struct seq_file *m, void *v)
{
    // 添加错误检查
    if (!m) {
        printk(KERN_ERR "IO Monitor: NULL seq_file pointer\n");
        return -EINVAL;
    }

    struct task_struct *task; // 定义任务结构体指针
    u64 first_ns = 0; // 用于记录每个进程第一次统计的时间戳
    u64 second_ns = 0; // 用于记录每个进程第二次统计的时间戳

    init_my_hlist_head(); // 初始化哈希表

    // 添加表头分隔线和格式化的表头
    seq_puts(m, "============================================================================\n");
    seq_printf(m, "%-16s %-8s %-16s %-8s %-12s %-12s\n",
              "COMM", "PID", "PARENT_COMM", "PPID", "Rd_Bpms", "Wt_Bpms");
    seq_puts(m, "----------------------------------------------------------------------------\n");
    
    // 在 io_monitor_show 函数中添加设备信息输出
    seq_printf(m, "Monitoring device: %s (dev_t: %u:%u)\n", 
               target_device,
               MAJOR(target_dev_info.dev),
               MINOR(target_dev_info.dev));
    seq_puts(m, "----------------------------------------------------------------------------\n");

    rcu_read_lock();
    for_each_process(task) {
        struct task_io_stats first_stats;
        memset(&first_stats, 0, sizeof(first_stats)); // 清零结构体
        first_stats.pid = task->pid;
        

        // 获取指定设备的 I/O 统计
        get_dev_io_stats(task, &first_stats.read_bytes, &first_stats.write_bytes, &first_ns);
        // 如果没有 I/O 操作，则跳过该任务
        if(!first_stats.read_bytes && !first_stats.write_bytes) continue; 
        first_stats.record_time_ns = first_ns; // 记录开始时间戳
        INIT_HLIST_NODE(&first_stats.task_hnode); // 初始化哈希节点
        
        // 将 stats 添加到哈希表中
        // 加锁保护哈希表插入
        spin_lock(&io_stats_hash_lock);  // 获取锁
        int index = first_stats.pid % IO_STATS_HASH_SIZE; // 计算哈希索引
        hlist_add_head(&first_stats.task_hnode, &task_io_stats_hash[index]);
        spin_unlock(&io_stats_hash_lock);  // 释放锁
    }
    rcu_read_unlock();
    
    msleep(SLEEP_milliseconds); // 休眠50毫秒;

    rcu_read_lock();
    for_each_process(task) {
        struct task_io_stats second_stats;
        memset(&second_stats, 0, sizeof(second_stats)); // 清零结构体
        second_stats.pid = task->pid,
        second_stats.ppid = task->real_parent ? task->real_parent->pid : 0;

        // 查找历史节点
        struct task_io_stats *stats_last = find_node(second_stats.pid, task_io_stats_hash); 
        if(stats_last == NULL) continue;

        // 获取新的IO统计
        get_dev_io_stats(task, &second_stats.read_bytes, &second_stats.write_bytes, &second_ns);
        if(!second_stats.read_bytes && !second_stats.write_bytes) continue;
        
        second_stats.record_time_ns = second_ns;

        // 安全的时间差计算
        u64 read_delta_ms = (second_stats.record_time_ns - stats_last->record_time_ns);
        if (read_delta_ms == 0) continue;  // 避免除零
        read_delta_ms = div_u64(read_delta_ms, 1000000); // 安全的除法

        // 安全的速率计算
        if (read_delta_ms > 0) {
            second_stats.read_bytes_pms = div_u64((second_stats.read_bytes - stats_last->read_bytes), read_delta_ms);
            second_stats.write_bytes_pms = div_u64((second_stats.write_bytes - stats_last->write_bytes), read_delta_ms);
        } else {
            second_stats.read_bytes_pms = 0;
            second_stats.write_bytes_pms = 0;
        }

        // stats.comm 与 stats.parent_comm获取
        strncpy(second_stats.comm, task->comm, TASK_COMM_LEN);
        second_stats.comm[TASK_COMM_LEN - 1] = '\0'; 
        if (task->real_parent) {    
            strncpy(second_stats.parent_comm, task->real_parent->comm, TASK_COMM_LEN);
            second_stats.parent_comm[TASK_COMM_LEN - 1] = '\0'; 
        } else {
            second_stats.parent_comm[0] = '\0'; // 如果没有父进程
        }

        // 使用固定宽度格式化输出
        seq_printf(m, "%-16s %-8d %-16s %-8d %-12lu %-12lu\n",
            second_stats.comm,
            second_stats.pid,
            second_stats.parent_comm,
            second_stats.ppid,
            second_stats.read_bytes_pms,
            second_stats.write_bytes_pms);
    }
    rcu_read_unlock();
    
    seq_puts(m, "============================================================================\n");
    clear_hlist_head(); // 清理哈希表中的所有条目
    return 0;
}

static int io_monitor_open(struct inode *inode, struct file *file)
{
    return single_open(file, io_monitor_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops io_monitor_fops = {
    .proc_open = io_monitor_open,
    .proc_read = seq_read, // 使用 seq_read 处理读取操作
    .proc_lseek = seq_lseek, // 使用 seq_lseek 处理 lseek 操作
    .proc_release = single_release, // 使用 single_release 处理释放操作
};
#else
static const struct file_operations io_monitor_fops = {
    .owner = THIS_MODULE,
    .open = io_monitor_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

static int __init io_monitor_init(void)
{
    dev_t dev;
    int ret;
    struct proc_dir_entry *proc_entry;

    // 增加设备名长度检查
    if (strlen(target_device) >= DISK_NAME_LEN) {
        printk(KERN_ERR "IO Monitor: Device name too long\n");
        return -EINVAL;
    }
    // 检查设备名是否为空
    if (strlen(target_device) == 0) {
        printk(KERN_ERR "IO Monitor: No target device specified\n");
        return -EINVAL;
    }

    ret = lookup_bdev(target_device, &dev);
    if (ret) {
        printk(KERN_ERR "IO Monitor: Cannot find device %s\n", target_device);
        return ret;
    }

    // 直接使用找到的设备号，不尝试获取块设备
    target_dev_info.dev = dev;
    target_dev_info.valid = true;

    spin_lock_init(&io_stats_hash_lock);  // 初始化自旋锁
    init_my_hlist_head(); // 初始化哈希表

    proc_entry = proc_create(PROC_ENTRY_NAME_REALTIME, 0, NULL, &io_monitor_fops);
    if (!proc_entry) {
        printk(KERN_ERR "IO Monitor: Cannot create proc entry\n");
        return -ENOMEM;
    }
        
    printk(KERN_INFO "IO Monitor: module loaded for device %s\n", target_device);
    return 0;
}

static void __exit io_monitor_exit(void)
{
    printk(KERN_INFO "IO Monitor: starting module cleanup\n");
    
    // 清理哈希表前先打印状态
    printk(KERN_INFO "IO Monitor: clearing hash table\n");
    clear_hlist_head();
    
    printk(KERN_INFO "IO Monitor: removing proc entry\n");
    remove_proc_entry(PROC_ENTRY_NAME_REALTIME, NULL);
    
    printk(KERN_INFO "IO Monitor: module unloaded successfully\n");
}

module_init(io_monitor_init);
module_exit(io_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luyi Zhang");
MODULE_DESCRIPTION("IO Monitor for specific device");

/**
 * readme
 * 
 * seq_read
    功能：用于处理 procfs 文件的读取操作
    用途：
        自动处理大数据的分页读取
        处理用户空间的读取请求
        管理读取缓冲区
    特点：
        自动处理部分读取和继续读取
        避免内存溢出问题
seq_lseek
    功能：处理文件的定位操作
    用途：
        允许用户空间程序在 procfs 文件中移动读取位置
        支持标准的文件定位操作（如 SEEK_SET、SEEK_CUR、SEEK_END）
    特点：
        与 seq_read 配合使用
        确保正确的文件位置跟踪
single_release
    功能：释放与 procfs 文件相关的资源
    用途：
        当文件被关闭时清理资源
        释放 seq_file 结构体
        防止内存泄漏
    特点：
        与 single_open 配对使用
    自动处理清理工作



graph TD
    A[用户打开/proc/io_monitor] --> B[创建file对象]
    B --> C[single_open关联seq_file]
    C --> D[将show函数与seq_file绑定]
    D --> E[建立file与seq_file的关联]
    E --> F[用户读取数据]
    F --> G[seq_read处理读取请求]
    G --> H[seq_lseek处理定位请求]
    H --> I[single_release释放资源]
    I --> J[清理seq_file结构体]
    J --> K[模块卸载时清理procfs条目]
    K --> L[打印模块卸载信息]
    L --> M[模块完全卸载]
    M --> N[用户空间可以再次打开/proc/io_monitor]
    N --> A[循环开始]
 */