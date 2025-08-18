#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/namei.h>
#include <linux/kprobes.h>

#include "io_monitor_v3_main.h"
#include "io_monitor_history.h"
#include "io_monitor_realtime.h"  // 新增: procfs 接口

// 模块参数
static char target_device[MAX_DEVICE_NAME] = "/dev/sda3";
module_param_string(device, target_device, MAX_DEVICE_NAME, 0644);
MODULE_PARM_DESC(device, "Target device name (e.g., /dev/sda3, /dev/sdb3)");

static dev_t target_dev;  // 目标设备号
static struct kprobe submit_bio_kp;

dev_t get_target_dev(void)
{
    return target_dev;
}

const char *get_target_device_name(void)
{
    return target_device;
}

// kprobe处理函数
static int submit_bio_entry_handler(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio = (struct bio *)regs->di;
    if (!bio || !bio->bi_bdev)
        return 0;
    
    if (bio->bi_bdev->bd_dev == get_target_dev()) {
        handle_bio_request(bio);
    }
    return 0;
}

// 根据设备名获取设备号
static int get_device_number(const char *device_name) {
    struct path path;
    struct kstat stat;
    char full_path[128];
    int ret;

    snprintf(full_path, sizeof(full_path), "%s", device_name);
    ret = kern_path(full_path, LOOKUP_FOLLOW, &path); // 
    if (ret)
        return ret;

    ret = vfs_getattr(&path, &stat, STATX_INO, AT_STATX_SYNC_AS_STAT);
    path_put(&path);
    
    if (ret == 0) {
        target_dev = stat.rdev;
        printk(KERN_INFO "%s: Target device %s resolved to %d:%d\n",
               MODULE_NAME, full_path, MAJOR(target_dev), MINOR(target_dev));
    }
    
    return ret;
}

// 新增: procfs 初始化/清理封装
static int init_procfs(void)
{
    int ret = create_realtime_proc_entry();
    if (ret)
        printk(KERN_ERR "%s: create /proc/%s failed (%d)\n",
               MODULE_NAME, MODULE_NAME, ret);
    return ret;
}

static void cleanup_procfs(void)
{
    remove_realtime_proc_entry();
}

// 模块初始化
static int __init io_monitor_init(void) {
    int ret;

    printk(KERN_INFO "%s: Initializing IO Monitor v3 for device %s\n",
           MODULE_NAME, target_device);

    ret = get_device_number(target_device);
    if (ret)
        return ret;

    ret = init_io_history();
    if (ret)
        return ret;

    ret = init_procfs();                 // 新增: 创建 /proc
    if (ret) {
        cleanup_io_history();
        return ret;
    }

    // 设置kprobe
    submit_bio_kp.symbol_name = "submit_bio";
    submit_bio_kp.pre_handler = submit_bio_entry_handler;
    ret = register_kprobe(&submit_bio_kp);
    if (ret < 0) {
        cleanup_procfs();                // 回滚 procfs
        cleanup_io_history();
        return ret;
    }

    printk(KERN_INFO "%s: Successfully loaded, kprobe registered at %p\n",
           MODULE_NAME, submit_bio_kp.addr);
    printk(KERN_INFO "%s: Monitoring device %s (%d:%d)\n",
           MODULE_NAME, target_device, MAJOR(target_dev), MINOR(target_dev));
    printk(KERN_INFO "%s: Log file: %s\n", MODULE_NAME, LOG_FILE_PATH);
    return 0;
}

// 模块退出
static void __exit io_monitor_exit(void) {
    printk(KERN_INFO "%s: Unloading module...\n", MODULE_NAME);
    unregister_kprobe(&submit_bio_kp);
    cleanup_procfs();          // 新增: 清理 /proc
    cleanup_io_history();
    printk(KERN_INFO "%s: Module unload initiated\n", MODULE_NAME);
}

module_init(io_monitor_init);
module_exit(io_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IO Monitor Team");
MODULE_DESCRIPTION("IO Monitor v3 - Monitor IO operations for specific devices");
MODULE_VERSION("3.0");
