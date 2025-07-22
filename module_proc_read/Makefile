# Makefile for task_IO_read kernel module
# This makefile is used to build the task_IO_read.ko kernel module

# 模块名称，对应生成的 .ko 文件
MODULE_NAME = task_IO_read

# 目标文件，内核构建系统会自动将 .c 文件编译为 .o 文件
obj-m += $(MODULE_NAME).o

# 获取当前内核版本和构建目录
KERNEL_VERSION = $(shell uname -r)
KERNEL_BUILD_DIR = /lib/modules/$(KERNEL_VERSION)/build

# 当前目录
PWD = $(shell pwd)

# 默认目标：编译模块
all:
	make -C $(KERNEL_BUILD_DIR) M=$(PWD) modules

# 清理生成的文件
clean:
	make -C $(KERNEL_BUILD_DIR) M=$(PWD) clean
	rm -f Module.markers modules.order

# 安装模块到系统
install:
	make -C $(KERNEL_BUILD_DIR) M=$(PWD) modules_install

# 加载模块
load:
	sudo insmod $(MODULE_NAME).ko

# 卸载模块
unload:
	sudo rmmod $(MODULE_NAME)

# 查看模块信息
info:
	modinfo $(MODULE_NAME).ko

# 查看内核日志
dmesg:
	dmesg | tail -20

# 查看proc文件内容
test:
	cat /proc/task_io_info

# 显示帮助信息
help:
	@echo "可用的make目标："
	@echo "  all      - 编译内核模块"
	@echo "  clean    - 清理编译生成的文件"
	@echo "  install  - 安装模块到系统"
	@echo "  load     - 加载模块到内核"
	@echo "  unload   - 从内核卸载模块"
	@echo "  info     - 显示模块信息"
	@echo "  dmesg    - 显示内核日志"
	@echo "  test     - 查看/proc/task_io_info文件内容"
	@echo "  help     - 显示此帮助信息"

# 声明伪目标
.PHONY: all clean install load unload info dmesg test help
