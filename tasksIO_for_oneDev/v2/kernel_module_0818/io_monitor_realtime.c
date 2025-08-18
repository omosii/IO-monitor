#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/string.h>
#include <linux/math64.h>
#include <linux/file.h>
#include "io_monitor_v3_main.h"
#include "io_monitor_realtime.h"
#include "io_monitor_history.h"

/* ===== 实时速率统计实现 ===== */
#define MAX_TAIL_READ      (256 * 1024)   /* 尾部最大读取字节 */
#define LAST_N_LINES       500            /* 取最后 N 条日志记录 */
#define INITIAL_PROC_CAP   64

struct rt_agg {
    pid_t pid;
    pid_t ppid;
    char  comm[TASK_COMM_LEN];
    char  pcomm[TASK_COMM_LEN];
    u64 read_kb;
    u64 write_kb;
};

struct log_line_parsed {
    u64 ts_ns;
    int major;
    int minor;
    pid_t pid;
    pid_t ppid;
    char comm[TASK_COMM_LEN];
    char pcomm[TASK_COMM_LEN];
    u64 read_kb;
    u64 write_kb;
};

// 查找或添加一个 rt_agg 记录，如果找不到，则创建一个新的记录并返回，如果找到了，则返回现有记录的指针
static struct rt_agg *find_or_add(struct rt_agg **arr, int *count, int *cap,
                                  pid_t pid, pid_t ppid,
                                  const char *comm, const char *pcomm)
{
    int i;
    for (i = 0; i < *count; i++) {
        if ((*arr)[i].pid == pid)
            return &(*arr)[i];
    }
    if (*count == *cap) {
        int new_cap = *cap * 2;
        struct rt_agg *n = krealloc(*arr, new_cap * sizeof(**arr), GFP_KERNEL);
        if (!n)
            return NULL;
        *arr = n;
        *cap = new_cap;
    }
    (*arr)[*count].pid = pid;
    (*arr)[*count].ppid = ppid;
    strscpy((*arr)[*count].comm,  comm ? comm  : "unknown", TASK_COMM_LEN);
    strscpy((*arr)[*count].pcomm, pcomm ? pcomm : "unknown", TASK_COMM_LEN);
    (*arr)[*count].read_kb = 0;
    (*arr)[*count].write_kb = 0;
    return &(*arr)[(*count)++];
}

// 解析一行日志，格式："timestamp pid ppid comm pcomm read_kb write_kb"
static int parse_log_line(char *line, struct log_line_parsed *out)
{
    long long sec, nsec;
    unsigned long long rtmp, wtmp;
    int matched = sscanf(line,
        "%lld.%09lld %d %d %d %15s %d %15s %llu %llu",
        &sec, &nsec,
        &out->major, &out->minor,
        &out->pid, out->comm,
        &out->ppid, out->pcomm,
        &rtmp, &wtmp);
    if (matched != 10)
        return -EINVAL;
    out->ts_ns   = (u64)sec * 1000000000ULL + (u64)nsec;
    out->read_kb = rtmp;
    out->write_kb= wtmp;
    return 0;
}

// 将一段连续的字符串缓冲区按换行符分割成多行
static char **collect_all_lines(char *buf, size_t len, int *out_cnt)
{
    size_t cap = 128;
    int cnt = 0;
    char **lines = kmalloc_array(cap, sizeof(char *), GFP_KERNEL);
    char *cur = buf, *nl;

    if (!lines)
        return NULL;

    while (cur && *cur) {
        if (cnt == (int)cap) {
            size_t new_cap = cap * 2;
            char **n = krealloc(lines, new_cap * sizeof(char *), GFP_KERNEL);
            if (!n) {
                kfree(lines);
                return NULL;
            }
            lines = n;
            cap = new_cap;
        }
        nl = strchr(cur, '\n');
        if (nl) *nl = '\0';
        if (*cur)
            lines[cnt++] = cur;
        if (!nl) break;
        cur = nl + 1;
    }
    *out_cnt = cnt;
    return lines;
}

// 选择最后 N 条日志记录，若日志量不够，则返回全部日志
static int select_last_n(char **all, int total, char ***out_sel, int *out_sel_cnt)
{
    int start = (total > LAST_N_LINES) ? (total - LAST_N_LINES) : 0;
    int need = total - start;
    char **sel = kmalloc_array(need, sizeof(char *), GFP_KERNEL);
    int i;

    if (!sel)
        return -ENOMEM;
    for (i = 0; i < need; i++)
        sel[i] = all[start + i];
    *out_sel = sel;
    *out_sel_cnt = need;
    return 0;
}

// 将每行相同进程的IO聚合，存储在agg_arr中
static void aggregate_records(char **lines, int line_cnt,
                              struct rt_agg **agg_arr,
                              int *agg_cnt, int *agg_cap,
                              u64 *min_ts, u64 *max_ts,
                              int *dev_major, int *dev_minor)
{
    int i;
    for (i = 0; i < line_cnt; i++) {
        struct log_line_parsed rec;
        if (!parse_log_line(lines[i], &rec)) {
            struct rt_agg *entry = find_or_add(agg_arr, agg_cnt, agg_cap,
                                               rec.pid, rec.ppid,
                                               rec.comm, rec.pcomm);
            if (entry) {
                entry->read_kb  += rec.read_kb;
                entry->write_kb += rec.write_kb;
            }
            if (*min_ts == 0 || rec.ts_ns < *min_ts) *min_ts = rec.ts_ns;
            if (rec.ts_ns > *max_ts) *max_ts = rec.ts_ns;
            *dev_major = rec.major;
            *dev_minor = rec.minor;
        }
    }
}

// 打印聚合后的信息到 seq_file
static void print_aggregated_info(struct seq_file *m,
                                  const char *devname,
                                  int dev_major, int dev_minor,
                                  struct rt_agg *arr, int cnt,
                                  u64 min_ts, u64 max_ts,
                                  int total_lines)
{
    u64 span_ns = (max_ts > min_ts) ? (max_ts - min_ts) : 1000000ULL;
    u64 span_ms = span_ns / 1000000ULL;
    int i;
    if (span_ms == 0) span_ms = 1;

    seq_printf(m,
        "Device: %s (%d:%d)\n"
        "Last lines parsed: %d (limit %d)\n"
        "Time range: %llums\n\n",
        devname ? devname : "unknown",
        dev_major, dev_minor,
        total_lines, LAST_N_LINES,
        (unsigned long long)span_ms);

    /* 新表头（固定宽度，防止错列）
       字段含义:
       PID/PPID: 进程/父进程
       COMM/PARENT_COMM: 名称
       READ_KB/WRITE_KB: 累计KB
       R_KBps/W_KBps: 速率(KB/s) */
    seq_puts(m,
        "--------------------------------------------------------------------------------------------------------\n");
    seq_puts(m,
        "  PID   COMM              PPID  PARENT_COMM        READ_KB       WRITE_KB      R_KBps      W_KBps\n");
    seq_puts(m,
        "--------------------------------------------------------------------------------------------------------\n");

    for (i = 0; i < cnt; i++) {
        u64 r_rate = div64_u64(arr[i].read_kb  * 1000000000ULL, span_ns);
        u64 w_rate = div64_u64(arr[i].write_kb * 1000000000ULL, span_ns);
        seq_printf(m,
            "%6d %-16s %6d %-16s %12llu %12llu %10llu %10llu\n",
            arr[i].pid,
            arr[i].comm,
            arr[i].ppid,
            arr[i].pcomm,
            (unsigned long long)arr[i].read_kb,
            (unsigned long long)arr[i].write_kb,
            (unsigned long long)r_rate,
            (unsigned long long)w_rate);
    }
    seq_puts(m,
        "--------------------------------------------------------------------------------------------------------\n");
}

static int proc_show(struct seq_file *m, void *v)
{
    struct file *f = io_log_file_get();
    loff_t fsize, start, pos;
    ssize_t to_read, read_len;
    char *buf = NULL;
    char **all_lines = NULL, **last_lines = NULL;
    int all_cnt = 0, last_cnt = 0;
    struct rt_agg *agg = NULL;
    int agg_cnt = 0, agg_cap = INITIAL_PROC_CAP;
    u64 min_ts = 0, max_ts = 0;
    int dev_major = 0, dev_minor = 0;
    const char *devname = get_target_device_name();

    if (!f) {
        seq_puts(m, "log file not ready\n");
        return 0;
    }

    agg = kmalloc_array(agg_cap, sizeof(*agg), GFP_KERNEL);
    if (!agg) {
        fput(f);
        seq_puts(m, "no memory\n");
        return 0;
    }

    fsize = i_size_read(file_inode(f));
    if (fsize < 0) fsize = 0;
    start = (fsize > MAX_TAIL_READ) ? (fsize - MAX_TAIL_READ) : 0;
    to_read = fsize - start;

    if (to_read <= 0) {  // 新增: 空文件或无可读数据
        fput(f);
        seq_printf(m, "Device: %s\nNo log data yet.\n",
                   devname ? devname : "unknown");
        return 0;
    }

    buf = kmalloc(to_read + 1, GFP_KERNEL);
    if (!buf) {
        kfree(agg);
        fput(f);
        seq_puts(m, "no memory\n");
        return 0;
    }

    pos = start;
    read_len = kernel_read(f, buf, to_read, &pos);

    if (read_len == -EINVAL && start != 0) {
        /* 回退策略:
         * 可能原因:
         * 1) 文件以只写方式打开 (无读权限)
         * 2) 底层不支持带独立偏移的 read
         * 3) 偏移非法
         * 尝试改为从头全量读取一次
         */
        kfree(buf);
        start = 0;
        to_read = fsize;
        buf = kmalloc(to_read + 1, GFP_KERNEL);
        if (!buf) {
            fput(f);
            kfree(agg);
            seq_puts(m, "no memory (fallback)\n");
            return 0;
        }
        pos = 0;
        read_len = kernel_read(f, buf, to_read, &pos);
    }

    fput(f);

    if (read_len < 0) {
        kfree(buf);
        kfree(agg);
        if (read_len == -EINVAL)
            seq_printf(m, "read log failed: %zd (EINVAL: file not readable or offset unsupported)\n", read_len);
        else
            seq_printf(m, "read log failed: %zd\n", read_len);
        return 0;
    }
    buf[read_len] = '\0';

    all_lines = collect_all_lines(buf, read_len, &all_cnt);
    if (!all_lines) {
        kfree(buf);
        kfree(agg);
        seq_puts(m, "parse lines failed\n");
        return 0;
    }

    if (select_last_n(all_lines, all_cnt, &last_lines, &last_cnt)) {
        kfree(all_lines);
        kfree(buf);
        kfree(agg);
        seq_puts(m, "select last lines failed\n");
        return 0;
    }

    if (last_cnt == 0) {
        seq_printf(m, "Device: %s\nNo log lines.\n",
                   devname ? devname : "unknown");
        goto out;
    }

    aggregate_records(last_lines, last_cnt, &agg, &agg_cnt, &agg_cap,
                      &min_ts, &max_ts, &dev_major, &dev_minor);
    if (agg_cnt == 0) {
        seq_printf(m, "Device: %s (%d:%d)\nNo valid parsed lines.\n",
                   devname ? devname : "unknown", dev_major, dev_minor);
        goto out;
    }

    print_aggregated_info(m, devname, dev_major, dev_minor,
                          agg, agg_cnt, min_ts, max_ts, last_cnt);

out:
    kfree(last_lines);
    kfree(all_lines);
    kfree(buf);
    kfree(agg);
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
static const struct proc_ops proc_fops = {
    .proc_open    = proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct file_operations proc_fops = {
    .owner   = THIS_MODULE,
    .open    = proc_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};
#endif

int create_realtime_proc_entry(void)
{
    if (!proc_create(MODULE_NAME, 0444, NULL, &proc_fops))
        return -ENOMEM;
    return 0;
}

void remove_realtime_proc_entry(void)
{
    remove_proc_entry(MODULE_NAME, NULL);
}
