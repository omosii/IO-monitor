#ifndef IO_MONITOR_REALTIME_H
#define IO_MONITOR_REALTIME_H

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/version.h>

#define IO_STATS_HASH_SIZE 1024 // 哈希表大小
#define SLEEP_MILLISECONDS 50 // 实时统计计算的时间间隔，单位为毫秒
#define ZOOM_FACTOR 1 // 用于速率计算的缩放因子

struct task_io_stats {
    pid_t pid;
    pid_t ppid;           // 父进程PID
    char comm[TASK_COMM_LEN];
    char parent_comm[TASK_COMM_LEN];  // 父进程名称

    unsigned long read_bytes;
    unsigned long write_bytes;
    unsigned long read_Bpms;
    unsigned long write_Bpms;

    u64 record_time_ns;
    struct hlist_node task_hnode;
};

int init_to_mod1(const char* target_device);
void cleanup_to_mod1(void);



#endif /* IO_MONITOR_REALTIME_H */