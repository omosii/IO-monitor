#ifndef IO_MONITOR_H
#define IO_MONITOR_H

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/version.h>

#define PROC_ENTRY_NAME_REALTIME "io_monitor_mod1"
#define PROC_ENTRY_NAME_HISTORY "io_monitor_mod2"
#define IO_STATS_HASH_SIZE 1024
#define SLEEP_milliseconds 50
struct task_io_stats {
    pid_t pid;
    pid_t ppid;           // 父进程PID
    char comm[TASK_COMM_LEN];
    char parent_comm[TASK_COMM_LEN];  // 父进程名称

    unsigned long read_bytes;
    unsigned long write_bytes;
    unsigned long read_bytes_pms;
    unsigned long write_bytes_pms;

    u64 record_time_ns;
    struct hlist_node task_hnode;
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

// 添加设备相关结构体
struct device_info {
    dev_t dev;           // 设备号
    bool valid;          // 设备是否有效
};

extern struct device_info target_dev_info;

#endif /* IO_MONITOR_H */
