#include "io_monitor.h" // 包含头文件 io_monitor.h
#include <linux/fs.h> // 包含文件系统相关的函数和结构体
#include <linux/uaccess.h>  // 包含用户空间访问相关的函数和宏   
#include <linux/blkdev.h> // 包含块设备相关的函数和结构体
// 添加缺失的头文件
#include <linux/fdtable.h>    // 用于 files_fdtable
#include <linux/file.h>       // 用于 fget
#include <linux/blk_types.h>  // 用于块设备相关类型
#include <linux/buffer_head.h>  // 添加这个头文件
#include <linux/init.h>         // 添加这个头文件
#include <linux/part_stat.h>    // 添加这个头文件

static char target_device[DISK_NAME_LEN]; // 受统计设备名
module_param_string(device, target_device, DISK_NAME_LEN, 0644);
/**
 * 当前代码没有对 target_device 是否为空进行检查
    虽然模块会正常加载，但会在 dmesg 中打印一个空设备名称
    模块可能无法正确过滤特定设备的 I/O 操作
 */

// 定义全局设备信息结构体
struct device_info target_dev_info = {0};

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

    // 如果设备无效，直接返回
    if (!target_dev_info.valid)
        return;

    // 遍历进程的所有线程
    thread = task;
    do {
        // 遍历进程打开的所有文件
        task_lock(thread);
        if (thread->files) {
            fdt = files_fdtable(thread->files);
            if (fdt) {
                for (i = 0; i < fdt->max_fds; i++) {
                    file = fdt->fd[i];
                    if (!file)
                        continue;
                        
                    inode = file->f_inode;
                    if (!inode)
                        continue;
                        
                    // 检查是否是目标设备的文件
                    if (inode->i_sb && inode->i_sb->s_dev == target_dev_info.dev) {
                        // 使用 u64 类型来存储 IO 统计
                        u64 rbytes = thread->ioac.read_bytes;
                        u64 wbytes = thread->ioac.write_bytes;
                        
                        *read_bytes += rbytes;
                        *write_bytes += wbytes;
                        // break;
                    }
                }
            }
        }
        task_unlock(thread);
    } while_each_thread(task, thread);
}

static int io_monitor_show(struct seq_file *m, void *v)
{
    struct task_struct *task;
    
    // 添加表头分隔线和格式化的表头
    seq_puts(m, "============================================================================\n");
    seq_printf(m, "%-16s %-8s %-16s %-8s %-12s %-12s\n",
              "COMM", "PID", "PARENT_COMM", "PPID", "READ(bytes)", "WRITE(bytes)");
    seq_puts(m, "----------------------------------------------------------------------------\n");
    
    // 在 io_monitor_show 函数中添加设备信息输出
    seq_printf(m, "Monitoring device: %s (dev_t: %u:%u)\n", 
               target_device,
               MAJOR(target_dev_info.dev),
               MINOR(target_dev_info.dev));
    
    rcu_read_lock();
    for_each_process(task) {
        struct task_io_stats stats = {
            .pid = task->pid,
            .ppid = task->real_parent ? task->real_parent->pid : 0
        };
        
        // 获取指定设备的 I/O 统计
        get_dev_io_stats(task, &stats.read_bytes, &stats.write_bytes);
        
        get_task_comm(stats.comm, task);
        
        if (task->real_parent) {
            get_task_comm(stats.parent_comm, task->real_parent);
        } else {
            strncpy(stats.parent_comm, "none", TASK_COMM_LEN);
        }
        
        // 使用固定宽度格式化输出
        seq_printf(m, "%-16s %-8d %-16s %-8d %-12lu %-12lu\n",
                  stats.comm,
                  stats.pid,
                  stats.parent_comm,
                  stats.ppid,
                  stats.read_bytes,
                  stats.write_bytes);
    }
    seq_puts(m, "============================================================================\n");
    rcu_read_unlock();
    
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
    // struct block_device *bdev;
    dev_t dev;
    int ret;

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

    if (!proc_create(PROC_ENTRY_NAME, 0, NULL, &io_monitor_fops))
        return -ENOMEM;
        
    printk(KERN_INFO "IO Monitor: module loaded for device %s\n", target_device);
    return 0;
}

static void __exit io_monitor_exit(void)
{
    remove_proc_entry(PROC_ENTRY_NAME, NULL);
    printk(KERN_INFO "IO Monitor: module unloaded\n");
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