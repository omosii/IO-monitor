#ifndef ANALYZE_IO_LOG_H
#define ANALYZE_IO_LOG_H

#include <pthread.h>
#include <sys/types.h>
#include <time.h>

#define MAX_PROC      1024
#define MAX_COMM_LEN  16
#define LOG_FILE_PATH "/tmp/io_monitor.log"
#define MAX_LINE_LEN  256

struct proc_io_stat {
    pid_t pid;
    pid_t ppid;
    char comm[MAX_COMM_LEN];
    char pcomm[MAX_COMM_LEN];
    unsigned long long read_kb;
    unsigned long long write_kb;
};

/* 对外函数（仅在本文件内部实现，便于分离声明） */
void print_stats(void);
int  process_log(time_t cutoff_time);
int  watch_proc(int interval);

/* 工具函数（如需在单元测试等扩展使用可暴露） */
void safe_strncpy(char *dest, const char *src, size_t size);

extern pthread_mutex_t stats_mutex;
extern struct proc_io_stat proc_stats[MAX_PROC];
extern int proc_count;

#endif /* ANALYZE_IO_LOG_H */
