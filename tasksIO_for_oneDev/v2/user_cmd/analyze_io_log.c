#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#define MAX_PROC 1024
#define MAX_COMM_LEN 16
#define LOG_FILE_PATH "/tmp/io_monitor.log"
#define MAX_LINE_LEN 256

// 进程 IO 统计结构
struct proc_io_stat {
    pid_t pid;
    pid_t ppid;
    char comm[MAX_COMM_LEN];
    char pcomm[MAX_COMM_LEN];
    unsigned long long read_kb;   // 原 read_bytes
    unsigned long long write_kb;  // 原 write_bytes
};

// 全局变量
static struct proc_io_stat proc_stats[MAX_PROC];
static int proc_count = 0;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// 安全的字符串拷贝函数
static void safe_strncpy(char *dest, const char *src, size_t size) {
    if (!dest || !src || size == 0) return;
    size_t len = strlen(src);
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// 处理单行日志的函数
static void process_log_line(const char *line, time_t cutoff_time) {
    long long timestamp_sec, timestamp_nsec;
    int major, minor;
    pid_t pid, ppid;
    char comm[MAX_COMM_LEN], pcomm[MAX_COMM_LEN];
    unsigned long long read_kb, write_kb;
    int i;
    
    // 解析日志行
    if (sscanf(line, "%lld.%lld %d %d %d %s %d %s %llu %llu",
               &timestamp_sec, &timestamp_nsec,
               &major, &minor, &pid, comm, &ppid, pcomm,
               &read_kb, &write_kb) != 10) {
        return;
    }
    
    // 检查时间戳
    if (timestamp_sec < cutoff_time) {
        return;
    }
    
    // 查找或创建进程统计记录
    pthread_mutex_lock(&stats_mutex);
    
    // 查找现有记录
    for (i = 0; i < proc_count; i++) {
        if (proc_stats[i].pid == pid) {
            proc_stats[i].read_kb += read_kb;
            proc_stats[i].write_kb += write_kb;
            pthread_mutex_unlock(&stats_mutex);
            return;
        }
    }
    
    // 添加新记录
    if (proc_count < MAX_PROC) {
        struct proc_io_stat *stat = &proc_stats[proc_count++];
        stat->pid = pid;
        stat->ppid = ppid;
        safe_strncpy(stat->comm, comm, MAX_COMM_LEN);
        safe_strncpy(stat->pcomm, pcomm, MAX_COMM_LEN);
        stat->read_kb = read_kb;
        stat->write_kb = write_kb;
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

// 读取和处理日志文件
static int process_log(time_t cutoff_time) {
    FILE *fp;
    int fd;
    char line[MAX_LINE_LEN];
    
    // 使用底层文件描述符以支持文件锁
    fd = open(LOG_FILE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open log file");
        return -1;
    }
    
    // 尝试获取共享读锁
    if (flock(fd, LOCK_SH) < 0) {
        perror("Failed to acquire file lock");
        close(fd);
        return -1;
    }
    
    // 将文件描述符转换为 FILE* 以使用标准 IO
    fp = fdopen(fd, "r");
    if (!fp) {
        perror("Failed to create FILE* from fd");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    
    // 读取并处理日志内容
    while (fgets(line, sizeof(line), fp) != NULL) {
        process_log_line(line, cutoff_time);
    }
    
    // 清理资源
    fclose(fp); // 这会同时关闭 fd
    flock(fd, LOCK_UN); // 显式释放文件锁
    
    return 0;
}

// 打印统计结果
static void print_stats(void) {
    pthread_mutex_lock(&stats_mutex);
    
    // 打印表头
    printf("\n═══════════════════════════════════════ IO Statistics Summary ═══════════════════════════════════════\n");
    printf("  %-8s   %-20s   %-8s   %-20s   %-15s   %-15s \n",
           "PID", "Command", "PPID", "Parent", "Read(KB)", "Write(KB)");
    printf("═══════════════════════════════════════════════════════════════════════════════════════════════════════\n");
    
    // 打印数据行
    for (int i = 0; i < proc_count; i++) {
        printf("  %-8d   %-20.20s   %-8d   %-20.20s   %15llu   %15llu \n",
               proc_stats[i].pid,
               proc_stats[i].comm,
               proc_stats[i].ppid,
               proc_stats[i].pcomm,
               proc_stats[i].read_kb,
               proc_stats[i].write_kb);
    }
    
    // 打印底部边框
    printf("═══════════════════════════════════════════════════════════════════════════════════════════════════════\n");
    
    pthread_mutex_unlock(&stats_mutex);
}

// 修改 main 函数，删除调试输出
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <minutes>\n", argv[0]);
        return 1;
    }
    
    int minutes = atoi(argv[1]);
    if (minutes <= 0) {
        fprintf(stderr, "Invalid minutes value\n");
        return 1;
    }
    
    time_t current_time = time(NULL);
    time_t cutoff_time = current_time - (minutes * 60);
    
    if (process_log(cutoff_time) != 0) {
        fprintf(stderr, "Error processing log file\n");
        return 1;
    }
    
    print_stats();
    
    pthread_mutex_destroy(&stats_mutex);
    return 0;
}