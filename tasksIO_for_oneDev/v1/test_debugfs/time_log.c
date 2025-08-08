#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define LOG_LINE_MAX 64
#define LOG_FILE_PATH "/tmp/time_log.txt"

static struct dentry *dir, *file;
static struct timer_list timer;
static DEFINE_MUTEX(log_mutex);

// 创建日志文件
static int create_log_file(void)
{
    struct file *log_file;
    char init_msg[] = "Time Logger Initialized\n";
    loff_t pos = 0;

    log_file = filp_open(LOG_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(log_file)) {
        pr_err("Failed to create log file: %ld\n", PTR_ERR(log_file));
        return PTR_ERR(log_file);
    }

    // 写入初始化消息
    kernel_write(log_file, init_msg, strlen(init_msg), &pos);
    filp_close(log_file, NULL);

    pr_info("Log file created at: %s\n", LOG_FILE_PATH);
    return 0;
}

// 读取日志文件内容
static ssize_t time_log_read(struct file *file, char __user *user_buf,
                            size_t count, loff_t *ppos)
{
    struct file *log_file;
    char *buffer;
    ssize_t ret = 0;
    loff_t file_size;
    loff_t read_pos = 0;

    mutex_lock(&log_mutex);

    // 打开日志文件
    log_file = filp_open(LOG_FILE_PATH, O_RDONLY, 0);
    if (IS_ERR(log_file)) {
        mutex_unlock(&log_mutex);
        pr_warn("Log file not found: %s\n", LOG_FILE_PATH);
        return 0; // 文件不存在，返回空
    }

    // 获取文件大小
    file_size = vfs_llseek(log_file, 0, SEEK_END);
    vfs_llseek(log_file, 0, SEEK_SET);

    if (file_size <= 0) {
        filp_close(log_file, NULL);
        mutex_unlock(&log_mutex);
        return 0;
    }

    // 分配缓冲区
    buffer = kmalloc(file_size + 1, GFP_KERNEL);
    if (!buffer) {
        filp_close(log_file, NULL);
        mutex_unlock(&log_mutex);
        return -ENOMEM;
    }

    // 读取文件内容
    ret = kernel_read(log_file, buffer, file_size, &read_pos);

    if (ret > 0) {
        buffer[ret] = '\0';
        ret = simple_read_from_buffer(user_buf, count, ppos, buffer, ret);
    }

    kfree(buffer);
    filp_close(log_file, NULL);
    mutex_unlock(&log_mutex);

    return ret;
}

static const struct file_operations time_log_fops = {
    .read = time_log_read,
};

// 追加写入日志文件
static void append_to_log_file(const char *log_entry)
{
    struct file *log_file;
    loff_t pos = 0;

    log_file = filp_open(LOG_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(log_file)) {
        pr_err("Failed to open log file: %ld\n", PTR_ERR(log_file));
        return;
    }

    // 写入日志条目
    kernel_write(log_file, log_entry, strlen(log_entry), &pos);

    filp_close(log_file, NULL);
}

static void timer_callback(struct timer_list *t)
{
    struct timespec64 ts;
    struct tm tm;
    char time_str[LOG_LINE_MAX];

    ktime_get_real_ts64(&ts);
    time64_to_tm(ts.tv_sec, 0, &tm);

    mutex_lock(&log_mutex);
    
    // 格式化时间字符串
    snprintf(time_str, LOG_LINE_MAX,
             "%ld-%02d-%02d %02d:%02d:%02d\n",
             tm.tm_year + 1900,
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec);

    // 实时追加写入文件
    append_to_log_file(time_str);

    mutex_unlock(&log_mutex);

    // 重新设置定时器，30秒后再次触发
    mod_timer(&timer, jiffies + msecs_to_jiffies(30000));
}

static int __init time_log_init(void)
{
    int ret;

    // 创建debugfs目录
    dir = debugfs_create_dir("time_logger", NULL);
    if (IS_ERR(dir)) {
        pr_err("Failed to create debugfs directory\n");
        return PTR_ERR(dir);
    }

    // 创建日志文件
    file = debugfs_create_file("time_log", 0444, dir, NULL, &time_log_fops);
    if (!file) {
        pr_err("Failed to create time_log file\n");
        debugfs_remove_recursive(dir);
        return -ENOMEM;
    }

    // 创建物理日志文件
    ret = create_log_file();
    if (ret < 0) {
        pr_err("Failed to create physical log file\n");
        debugfs_remove_recursive(dir);
        return ret;
    }

    // 初始化定时器
    timer_setup(&timer, timer_callback, 0);
    mod_timer(&timer, jiffies + msecs_to_jiffies(30000));

    pr_info("Time logger module initialized\n");
    pr_info("Log file location: %s\n", LOG_FILE_PATH);
    pr_info("DebugFS location: /sys/kernel/debug/time_logger/time_log\n");
    return 0;
}

static void __exit time_log_exit(void)
{
    // 删除定时器
    del_timer_sync(&timer);
    
    // 清理debugfs
    debugfs_remove_recursive(dir);
    
    pr_info("Time logger module unloaded\n");
    pr_info("Log file preserved at: %s\n", LOG_FILE_PATH);
}

module_init(time_log_init);
module_exit(time_log_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luyi Zhang");
MODULE_DESCRIPTION("A simple time logger with real-time file append");