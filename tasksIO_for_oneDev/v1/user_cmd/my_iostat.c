#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>

#define PROC_FILE_MOD1 "/proc/io_monitor_mod1"
#define PROC_FILE_MOD2 "/proc/io_monitor_mod2"
#define BUF_SIZE 4096

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

// 模式1：实时监控
void mode1_monitor(int interval) {
    FILE *fp;
    char buffer[BUF_SIZE];
    
    while(1) {
        // 清屏
        printf("\033[2J\033[H");
        
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
    FILE *fp;
    char buffer[BUF_SIZE];
    
    fp = fopen(PROC_FILE_MOD2, "r");
    if (fp == NULL) {
        perror("无法打开文件");
        exit(1);
    }

    printf("显示最近 %d 分钟的IO记录：\n", period);
    
    // 读取并打印文件内容
    while (fgets(buffer, BUF_SIZE, fp) != NULL) {
        printf("%s", buffer);
    }
    
    fclose(fp);
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