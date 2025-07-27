#ifndef IO_MONITOR_H
#define IO_MONITOR_H

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/version.h>

#define PROC_ENTRY_NAME "io_monitor"

struct task_io_stats {
    pid_t pid;
    pid_t ppid;           // 父进程PID
    char comm[TASK_COMM_LEN];
    char parent_comm[TASK_COMM_LEN];  // 父进程名称
    unsigned long read_bytes;
    unsigned long write_bytes;
};

// 版本兼容性宏定义
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
#define HAVE_PROC_OPS 
// 支持proc_ops结构
/**
 * 这个宏用于处理 Linux 内核 5.6.0 版本前后的 procfs 接口变化
    在 Linux 5.6.0 之前，procfs 操作使用 file_operations 结构体
    在 Linux 5.6.0 及之后，引入了专门的 proc_ops 结构体
 */
#endif

#endif /* IO_MONITOR_H */
