#ifndef MY_IOSTAT_H
#define MY_IOSTAT_H

#include <stdio.h>
#include <time.h>
#include <sys/time.h>  // 使用这个替代 linux/time.h

// 常量定义
#define PROC_FILE_MOD1 "/proc/io_monitor_mod1"
#define PROC_FILE_MOD2 "/proc/io_monitor_mod2"
#define BUF_SIZE 8192
#define TEMP_FILE "/tmp/io_monitor_snapshot_%d"
#define MAX_RETRIES 5      // 最大重试次数
#define RETRY_DELAY 200000 // 重试间隔(微秒)
#define PROC_NAME_MAX 256  // 进程名最大长度

// 进程IO信息结构体
struct proc_io_info {
    char comm[PROC_NAME_MAX];          // 进程名
    int pid;                           // 进程ID
    char parent_comm[PROC_NAME_MAX];   // 父进程名
    int ppid;                          // 父进程ID
    unsigned long first_read;          // 最早的读取量
    unsigned long first_write;         // 最早的写入量
    unsigned long last_read;           // 最新的读取量
    unsigned long last_write;          // 最新的写入量
    time_t first_seen;                 // 首次记录时间
    time_t last_seen;                  // 最后记录时间
    struct proc_io_info *next;         // 链表下一节点
};

// 函数声明
int kbhit(void);
void mode1_monitor(int interval);
void mode2_query(int period);
void print_usage(const char *program_name);

#endif /* MY_IOSTAT_H */