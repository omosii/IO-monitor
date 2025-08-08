#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/time.h>  // 使用这个替代 linux/time.h

#include "my_iostat.h"

// 非阻塞方式检测键盘输入
int kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

// 安全的字符串复制函数
static inline void safe_strncpy(char *dest, const char *src, size_t size) {
    size_t len = strlen(src);
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// 创建新的进程IO信息节点
static struct proc_io_info* create_proc_info(
    const char *comm, int pid, const char *parent_comm, int ppid,
    unsigned long read_bytes, unsigned long write_bytes, time_t timestamp) {
    
    struct proc_io_info *proc = malloc(sizeof(struct proc_io_info));
    if (proc == NULL) {
        return NULL;
    }
    
    safe_strncpy(proc->comm, comm, PROC_NAME_MAX);
    safe_strncpy(proc->parent_comm, parent_comm, PROC_NAME_MAX);
    
    proc->pid = pid;
    proc->ppid = ppid;
    proc->first_read = read_bytes;
    proc->first_write = write_bytes;
    proc->last_read = read_bytes;
    proc->last_write = write_bytes;
    proc->first_seen = timestamp;
    proc->last_seen = timestamp;
    proc->next = NULL;
    
    return proc;
}

// 复制文件内容
static int copy_file_with_retry(const char *src_path, const char *dst_path) {
    int retry_count = 0;
    char buffer[BUF_SIZE];
    FILE *src_fp = NULL, *dst_fp = NULL;

    while (retry_count < MAX_RETRIES) {
        src_fp = fopen(src_path, "r");
        if (!src_fp) {
            if (errno == EAGAIN) {
                retry_count++;
                printf("等待文件可用... (尝试 %d/%d)\n", retry_count, MAX_RETRIES);
                usleep(RETRY_DELAY);
                continue;
            }
            perror("无法打开源文件");
            return -1;
        }
        break;
    }

    if (retry_count >= MAX_RETRIES) {
        printf("无法访问文件，超过最大重试次数\n");
        return -1;
    }

    // 创建目标文件
    dst_fp = fopen(dst_path, "w");
    if (!dst_fp) {
        perror("无法创建目标文件");
        fclose(src_fp);
        return -1;
    }

    // 复制文件内容
    while (fgets(buffer, sizeof(buffer), src_fp) != NULL) {
        fputs(buffer, dst_fp);
    }

    fclose(src_fp);
    fclose(dst_fp);
    return 0;
}

// 打印统计结果
static void print_io_stats(struct proc_io_info *head, int period) {
    printf("\n时间范围：最近 %d 分钟的IO统计\n\n", period);
    printf("%-20s %-8s %-20s %-8s %-12s %-12s\n", 
           "COMM", "PID", "PARENT_COMM", "PPID", "Rd_Bytes", "Wt_Bytes");
    printf("%-20s %-8s %-20s %-8s %-12s %-12s\n",
           "--------------------", "--------", "--------------------", 
           "--------", "------------", "------------");

    struct proc_io_info *proc = head;
    // FIXME: 相等有可能是只有一条记录，修复这种情况
    while (proc != NULL) {
        unsigned long read_diff = proc->last_read - proc->first_read;
        unsigned long write_diff = proc->last_write - proc->first_write;
        
        if (read_diff > 0 || write_diff > 0) {
            printf("%-20s %-8d %-20s %-8d %-12lu %-12lu\n",
                   proc->comm, proc->pid, proc->parent_comm, proc->ppid,
                   read_diff, write_diff);
        }
        
        struct proc_io_info *to_free = proc;
        proc = proc->next;
        free(to_free);
    }
}

// 模式1：实时监控
void mode1_monitor(int interval) {
    FILE *fp;
    char buffer[BUF_SIZE];
    
    while(1) {
        
        // 清屏
        printf("\033[2J\033[H");
        
        // printf("unsigned long size: %zu bits\n", sizeof(unsigned long) * 8);
        // printf("unsigned long max: %lu\n", ULONG_MAX);

        // 在清屏后重新显示提示信息
        printf("按 'q' 键退出程序\n\n");
        
        // 打开并读取文件
        fp = fopen(PROC_FILE_MOD1, "r");
        if (fp == NULL) {
            perror("无法打开文件");
            exit(1);
        }
        
        // 读取并打印文件内容
        while (fgets(buffer, BUF_SIZE, fp) != NULL) {
            printf("%s", buffer);
        }
        fclose(fp);

        // 检查是否按下q键
        if (kbhit()) {
            char c = getchar();
            if (c == 'q' || c == 'Q') {
                printf("\n 欢迎下次使用 ^_^ \n");
                break;
            }
        }

        sleep(interval);
    }
}

// 模式2：查询历史记录
void mode2_query(int period) {
    char temp_file[PATH_MAX];
    FILE *fp;
    time_t current_time;
    struct proc_io_info *head = NULL;
    
    // 生成临时文件名
    snprintf(temp_file, sizeof(temp_file), TEMP_FILE, getpid());
    
    // 复制文件并加锁
    if (copy_file_with_retry(PROC_FILE_MOD2, temp_file) < 0) {
        exit(1);
    }
    
    // 打开临时文件处理
    fp = fopen(temp_file, "r");
    if (fp == NULL) {
        perror("无法打开临时文件");
        unlink(temp_file);
        exit(1);
    }
    
    time(&current_time);
    char buffer[BUF_SIZE];
    
    // 读取并处理文件内容
    while (fgets(buffer, BUF_SIZE, fp) != NULL) {
        char *token;
        long long timestamp_sec;
        unsigned long tasks_num;
        
        // 解析时间戳
        token = strtok(buffer, ".");
        if (!token) continue;
        timestamp_sec = atoll(token);
        
        // 检查时间范围
        if (current_time - timestamp_sec > period * 60) {
            continue;
        }

        // 解析纳秒和任务数
        token = strtok(NULL, " ");
        if (!token) continue;
        token = strtok(NULL, " ");
        if (!token) continue;
        tasks_num = strtoul(token, NULL, 10);
        
        // 处理每个任务的信息
        for (unsigned long i = 0; i < tasks_num; i++) {
            char comm[256];
            int pid;
            char parent_comm[256];
            int ppid;
            unsigned long read_bytes, write_bytes;
            
            // 解析进程信息
            token = strtok(NULL, " "); // COMM
            if (!token) break;
            strncpy(comm, token, sizeof(comm) - 1);
            
            token = strtok(NULL, " "); // PID
            if (!token) break;
            pid = atoi(token);
            
            token = strtok(NULL, " "); // PARENT_COMM
            if (!token) break;
            strncpy(parent_comm, token, sizeof(parent_comm) - 1);
            
            token = strtok(NULL, " "); // PPID
            if (!token) break;
            ppid = atoi(token);
            
            token = strtok(NULL, " "); // READ_BYTES
            if (!token) break;
            read_bytes = strtoul(token, NULL, 10);
            
            token = strtok(NULL, " "); // WRITE_BYTES
            if (!token) break;
            write_bytes = strtoul(token, NULL, 10);
            
            struct proc_io_info *proc = head;
            while (proc != NULL) {
                if (proc->pid == pid) {
                    proc->last_read = read_bytes;
                    proc->last_write = write_bytes;
                    proc->last_seen = timestamp_sec;
                    break;
                }
                proc = proc->next;
            }
            
            if (proc == NULL) {
                proc = create_proc_info(comm, pid, parent_comm, ppid, 
                                      read_bytes, write_bytes, timestamp_sec);
                if (proc == NULL) {
                    perror("内存分配失败");
                    fclose(fp);
                    unlink(temp_file);
                    exit(1);
                }
                proc->next = head;
                head = proc;
            }
        }
    }
    
    fclose(fp);
    unlink(temp_file);
    
    // 打印结果
    print_io_stats(head, period);
}

void print_usage(const char *program_name) {
    printf("使用方法:\n");
    printf("模式1 (实时监控): %s -1 <interval>\n", program_name);
    printf("模式2 (历史查询): %s -2 <period>\n", program_name);
    printf("参数说明:\n");
    printf("  interval: 刷新间隔(秒)\n");
    printf("  period: 查询时间范围(分钟)\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-1") == 0) {
        int interval = atoi(argv[2]);
        if (interval <= 0) {
            printf("刷新间隔必须大于0\n");
            return 1;
        }
        mode1_monitor(interval);
    }
    else if (strcmp(argv[1], "-2") == 0) {
        int period = atoi(argv[2]);
        if (period <= 0) {
            printf("查询时间范围必须大于0\n");
            return 1;
        }
        mode2_query(period);
    }
    else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}