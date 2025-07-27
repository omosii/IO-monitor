#include <linux/module.h> // 包含模块相关的头文件
#include <linux/kernel.h> // 包含内核相关的头文件，提供printk等内核函数
#include <linux/init.h> // 包含初始化相关的头文件，提供__init和__exit宏
#include <linux/sched/signal.h> // 进程相关头文件
#include <linux/proc_fs.h> // 包含proc文件系统相关的头文件
#include <linux/seq_file.h> // 包含seq_file相关的头文件
#include <linux/mm.h> // 包含内存管理相关的头文件
#include <linux/fs.h> // 包含文件系统相关的头文件
#include <linux/uaccess.h> // 包含用户空间访问相关的头文件
#include <linux/cred.h> // 包含凭证相关的头文件
#include <linux/version.h> // 包含版本相关的头文件

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luyi Zhang");
MODULE_DESCRIPTION("A module to read task IO information");
MODULE_VERSION("0.2");

#define PROC_NAME "task_io_info" // 定义proc文件名

// 采集单个进程详细信息并输出到 seq_file
static void show_task_info(struct seq_file *m, struct task_struct *task)
{
    // 定义变量
    unsigned long vm_size = 0;  // 用于存储进程的虚拟内存大小（单位：字节）。
    unsigned long rss = 0;      // 用于存储进程实际占用的物理内存（常驻集，单位：字节）
    unsigned long read_bytes = 0; // 进程累计读取的字节数（I/O统计）
    unsigned long write_bytes = 0; // 进程累计写入的字节数（I/O统计）

    // 获取进程的虚拟内存大小和物理内存大小
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0) // 判断内核版本是否大于等于4.0.0
    if (task->mm) {
        // task->mm：指向进程的内存描述符（mm_struct），只有用户进程才有，内核线程为NULL。
        vm_size = task->mm->total_vm << PAGE_SHIFT; // 将虚拟内存大小转换为字节
        rss = get_mm_rss(task->mm) << PAGE_SHIFT; // 将物理内存大小转换为字节
    }
#endif

    // 获取进程的I/O统计信息
#ifdef CONFIG_TASK_IO_ACCOUNTING // 判断是否启用了任务I/O统计功能
    read_bytes = task->ioac.read_bytes; // 读取字节数
    write_bytes = task->ioac.write_bytes; // 写入字节数
#endif

    // 获取进程状态（兼容不同内核版本）
    long state;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,14,0)
    #ifdef READ_ONCE
        state = READ_ONCE(task->__state);
    #else
        state = task->__state;
    #endif
#else
    state = task->state;
#endif

    // 输出进程信息到seq_file
    seq_printf(m,
        "%-7d %-16s %-7ld %-7d %-7d %-9lu %-9lu %-10lu %-10lu\n",
        task->pid,  // 进程ID
        task->comm, // 进程名称
        state, // 进程状态
        task->real_parent ? task->real_parent->pid : 0, // 父进程ID
        __kuid_val(task->cred->uid), // 用户ID
        vm_size / 1024, // 虚拟内存大小（KB）
        rss / 1024, // 物理内存大小（KB）
        read_bytes, // 读取字节数
        write_bytes // 写入字节数
    );

    // __kuid_val() 是一个内核宏/函数，用于将 struct uid_t 转换为 unsigned int 类型。
    // 在 Linux 内核中，uid_t 通常是 32 位整数类型，用于表示用户 ID。
    // 而 __kuid_val() 宏的作用是将 uid_t 类型转换为 unsigned int 类型，以便在需要时进行整数运算。 
}

// seq_file 的 show 回调，遍历所有进程
// 这个函数是 /proc/task_io_info 文件的主显示回调函数。
// 当你用 cat /proc/task_io_info 时，内核会调用这个函数，把所有进程的信息输出到该文件。
static int task_io_seq_show(struct seq_file *m, void *v)
{
    // 定义变量
    struct task_struct *task; // 用于存储进程的 task_struct 结构体指针

    // 输出表头
    seq_printf(m, "PID     COMM             STATE   PPID    UID     VM(KB)    RSS(KB)   IO_R      IO_W\n");
    seq_printf(m, "-------------------------------------------------------------------------------\n");
    for_each_process(task) {
        show_task_info(m, task);
    }
    return 0;
}

// 这段代码定义了 /proc/task_io_info 文件的 open 回调函数。
// 当用户试图打开 /proc/task_io_info 文件时，内核会调用这个函数。
static int task_io_proc_open(struct inode *inode, struct file *file)
{
    // single_open 是 Linux 内核提供的一个辅助函数，专门用于 proc 文件的 seq_file 接口。
    // 它用于将 seq_file 接口与 proc 文件关联起来，使得 proc 文件可以像普通文件一样被读取。
    // 参数 file 是 proc 文件的 file 结构体指针，用于标识该文件。
    // 参数 task_io_seq_show 是 seq_file 的 show 回调函数，用于显示进程信息。
    // 参数 NULL 是 seq_file 的私有数据，这里传入 NULL 表示不需要额外的私有数据。
    return single_open(file, task_io_seq_show, NULL);
}

// 兼容不同内核版本的proc文件操作结构体
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
// 新版本内核使用 proc_ops 结构体
static const struct proc_ops task_io_proc_fops = {
    .proc_open = task_io_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
// 旧版本内核使用 file_operations 结构体
static const struct file_operations task_io_proc_fops = {
    .owner = THIS_MODULE,
    .open = task_io_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

static int __init task_io_read_init(void)
{
    if (!proc_create(PROC_NAME, 0, NULL, &task_io_proc_fops)) {
        printk(KERN_ERR "task_io_read: 无法创建 /proc/%s\n", PROC_NAME);
        return -ENOMEM; // 内存不足错误
    }
    printk(KERN_INFO "task_io_read: 模块加载，/proc/%s 已创建\n", PROC_NAME);
    return 0;
}

static void __exit task_io_read_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);  // 移除proc文件
    printk(KERN_INFO "task_io_read: 模块卸载，/proc/%s 已移除\n", PROC_NAME);
}

module_init(task_io_read_init);
module_exit(task_io_read_exit);








