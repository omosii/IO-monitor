#ifndef IO_MONITOR_HISTORY_H
#define IO_MONITOR_HISTORY_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/blkdev.h>

#define LOG_FILE_PATH        "/tmp/io_monitor.log"
#define MAX_LOG_ENTRIES      2048 // FIXME 是否存在瓶颈？
#define MAX_LOG_ENTRY_SIZE   512
#define HASHTABLE_SIZE       1024

struct proc_io_stats {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    pid_t ppid;
    char pcomm[TASK_COMM_LEN];
    atomic64_t read_kb;   // 原 read_bytes
    atomic64_t write_kb;  // 原 write_bytes
    struct hlist_node hash_node;
    struct rcu_head rcu;
};

struct filter_rule {
    dev_t dev;
    bool track_read;
    bool track_write;
    struct rcu_head rcu;
};

struct log_entry {
    char *buffer;
    struct list_head list;
};

/* 供主模块调用的初始化/清理（实现于 io_monitor_history.c） */
int  init_io_history(void);
void cleanup_io_history(void);

/* 由 kprobe 触发的 bio 处理入口（主模块调用） */
void handle_bio_request(struct bio *bio);

/* 以下接口主模块未直接使用，可留作测试需要（不导出符号） */
int update_filter_rule(dev_t dev, bool track_r, bool track_w);

#endif /* IO_MONITOR_HISTORY_H */
