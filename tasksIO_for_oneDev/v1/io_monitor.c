#include "io_monitor.h" 
#include "io_monitor_realtime.h" 
#include "io_monitor_history.h" 

#include <linux/fs.h> // 包含文件系统相关的函数和结构体
#include <linux/uaccess.h>  // 包含用户空间访问相关的函数和宏   
#include <linux/blkdev.h> // 包含块设备相关的函数和结构体
#include <linux/fdtable.h>    // 用于 files_fdtable
#include <linux/file.h>       // 用于 fget
#include <linux/blk_types.h>  // 用于块设备相关类型
#include <linux/buffer_head.h> 
#include <linux/init.h>       
#include <linux/part_stat.h>   
#include <linux/atomic.h>       // 添加这个头文件，用于 atomic64_t
#include <linux/ktime.h> // 添加这个头文件，用于时间相关函数
#include <linux/delay.h>  // 添加这行，用于 msleep 函数

// 模块参数
static char target_device[DISK_NAME_LEN]; // 受统计设备名
module_param_string(device, target_device, DISK_NAME_LEN, 0644);
MODULE_PARM_DESC(device_name, "Name of the device to monitor (e.g. /dev/sda3)");


// 定义全局设备信息结构体
struct device_info target_dev_info = {0};
dev_t target_dev = 0; // 定义全局设备号

// 模块初始化函数
static int __init io_monitor_init(void)
{
    int ret;

    // 增加设备名长度检查
    if (strlen(target_device) >= DISK_NAME_LEN) {
        printk(KERN_ERR "[io_monitor] : Device name too long\n");
        return -EINVAL;
    }
    // 检查设备名是否为空
    if (strlen(target_device) == 0) {
        printk(KERN_ERR "[io_monitor] : No target device specified\n");
        return -EINVAL;
    }

    ret = lookup_bdev(target_device, &target_dev);
    if (ret) {
        printk(KERN_ERR "[io_monitor] : Cannot find device %s\n", target_device);
        return ret;
    }

    // 直接使用找到的设备号，不尝试获取块设备
    target_dev_info.dev = target_dev;
    target_dev_info.valid = true;
        
    // 初始化mod1
    ret = init_to_mod1(target_device);
    if(ret != 0) {
        printk(KERN_ERR "[io_monitor] : Failed to initialize mod1\n");
        return ret;
    }
    // 初始化mod2
    ret = init_io_mod2();
    if(ret != 0) {
        printk(KERN_ERR "[io_monitor] : Failed to initialize mod2\n");
        return ret;
    }

    printk(KERN_INFO "[io_monitor] : module loaded for device %s\n", target_device);
    return 0;
}

static void __exit io_monitor_exit(void)
{
    printk(KERN_INFO "[io_monitor] : starting module cleanup\n");
    
    // 清理mod1
    cleanup_to_mod1();
    // 清理mod2
    cleanup_io_mod2();

    printk(KERN_INFO "[io_monitor] : module unloaded successfully\n");
}

module_init(io_monitor_init);
module_exit(io_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luyi Zhang");
MODULE_DESCRIPTION("[io_monitor] for specific device");