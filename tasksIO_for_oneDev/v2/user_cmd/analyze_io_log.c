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
#include <sys/select.h>
#include <termios.h>   // 新增
#include "analyze_io_log.h"

struct proc_io_stat proc_stats[MAX_PROC];
int proc_count = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// 安全的字符串拷贝函数
void safe_strncpy(char *dest, const char *src, size_t size) {
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
int process_log(time_t cutoff_time) {
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
void print_stats(void) {
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

/* 新增: 周期读取 /proc/io_monitor_v3 */
int watch_proc(int interval) {
    if (interval <= 0) {
        fprintf(stderr, "Invalid interval\n");
        return -1;
    }
    printf("Periodic mode: every %d second(s). Press 'q' to quit.\n", interval);

    struct termios oldt, newt;
    int term_ok = (tcgetattr(STDIN_FILENO, &oldt) == 0);
    if (term_ok) {
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);   // 关闭规范模式和回显
        newt.c_cc[VMIN]  = 0;
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }

    while (1) {
        // 清屏并移动光标到左上
        printf("\033[2J\033[H");
        printf("Press 'q' to quit. Interval: %d s\n", interval);
        printf("Timestamp: %ld\n", (long)time(NULL));
        printf("------------------------------------------------------------\n");

        FILE *fp = fopen("/proc/io_monitor_v3", "r");
        if (!fp) {
            perror("open /proc/io_monitor_v3");
            if (term_ok) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return -1;
        }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            fwrite(buf, 1, n, stdout);
        fclose(fp);
        printf("------------------------------------------------------------\n");
        fflush(stdout);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = { .tv_sec = interval, .tv_usec = 0 };
        int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            int quit = 0;
            char ch;
            // 读取所有可用字符，检测是否存在 q/Q
            while (read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == 'q' || ch == 'Q') {
                    quit = 1;
                    break;
                }
            }
            if (quit) {
                printf("\nQuit.\n");
                break;
            }
        } else if (r < 0 && errno != EINTR) {
            perror("select");
            break;
        }
        // r == 0 超时 -> 继续下一轮
    }

    if (term_ok)
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}

// 修改 main 函数，删除调试输出
int main(int argc, char *argv[]) {
    // 新增参数模式: -p / --period <seconds>
    if (argc == 3 && (!strcmp(argv[1], "-p") || !strcmp(argv[1], "--period"))) {
        int sec = atoi(argv[2]);
        return watch_proc(sec) == 0 ? 0 : 1;
    }

    if (argc != 2) {
        fprintf(stderr,
                "Usage:\n"
                "  %s <minutes>              # 历史回看模式 (读取 %s)\n"
                "  %s -p <seconds>           # 周期模式：每 N 秒打印一次 /proc/io_monitor_v3，输入 q 退出\n",
                argv[0], LOG_FILE_PATH, argv[0]);
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