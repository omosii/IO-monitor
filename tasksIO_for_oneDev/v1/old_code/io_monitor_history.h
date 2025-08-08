#ifndef IO_MONITOR_HISTORY_H
#define IO_MONITOR_HISTORY_H

#include <linux/list.h>
#include <linux/time64.h>
#include <linux/sched.h>

/*
    链表结构如下：
    io_record_head -> io_record_1 -> io_record_2 -> io_record_3 -> ... -> io_record_head
                            |             |              |
                            v             v              v
                           head          head           head
                            |             |              |
                            v             v              v
                    task_io_stats_1  task_io_stats_1  task_io_stats_1
                            |             |              |
                            v             v              v
                    task_io_stats_2  task_io_stats_2  task_io_stats_2
                            |             |              |
                            v             v              v
                    task_io_stats_3  task_io_stats_3  task_io_stats_3
                            |             |              |
                            v             v              v
                           ...           ...            ...
                            |             |              |
                            v             v              v
                           head          head           head
*/ 
// 存储单个进程IO信息的结构体
struct task_io_stats_history {
    pid_t pid;
    pid_t ppid;           // 父进程PID
    char comm[TASK_COMM_LEN];
    char parent_comm[TASK_COMM_LEN];  // 父进程名称

    unsigned long read_bytes;
    unsigned long write_bytes;

    struct list_head list;
};

// 存储某一时刻，所有进程IO信息的结构体
struct io_record {

    struct timespec64 timestamp;

    unsigned long tasks_num;  

    struct list_head* task_io_stats_history_head; // 指向一个链表，存储所有进程的IO信息
    
    struct list_head list;
};


// 历史记录管理函数
int init_io_mod2(void);
void cleanup_io_mod2(void);


// 模块参数声明
extern int interval_minutes;

#endif /* IO_MONITOR_HISTORY_H */