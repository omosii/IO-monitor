#include <linux/module.h> // 包含模块相关的头文件
#include <linux/kernel.h> // 包含内核相关的头文件，提供printk等内核函数
#include <linux/init.h> // 包含初始化相关的头文件，提供__init和__exit宏

MODULE_LICENSE("GPL"); // 声明模块的许可证
MODULE_AUTHOR("Your Name"); // 声明模块的作者
MODULE_DESCRIPTION("A simple hello world module"); // 声明模块的描述
MODULE_VERSION("0.1"); // 声明模块的版本

static int __init init_module(void)
{
    printk(KERN_INFO "Hello, World!\n");
    return 0;
}

static void __exit cleanup_module(void)
{
    printk(KERN_INFO "Goodbye, World!\n");
}

module_init(init_module); // 注册模块的初始化函数
module_exit(cleanup_module); // 注册模块的清理函数