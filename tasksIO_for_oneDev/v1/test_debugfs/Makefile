# 定义模块名称
MODULE_NAME := my_time_log

# 指定要生成的模块
obj-m := $(MODULE_NAME).o

# 定义源文件列表
$(MODULE_NAME)-y := \
	time_log.o

# 指定内核构建目录
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# 编译目标
all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# 清理目标
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.* Module.* modules.*

# 模块操作目标
insmod:
	sudo insmod $(MODULE_NAME).ko

rmmod:
	sudo rmmod $(MODULE_NAME)

# 显示信息
dmesg:
	sudo dmesg | grep time_log

dmesg_err:
	sudo dmesg -l err

lsmod:
	sudo lsmod | grep $(MODULE_NAME)

modinfo:
	sudo modinfo ./$(MODULE_NAME).ko

# 测试目标
test:
	sudo cat /sys/kernel/debug/time_logger/time_log

test_file:
	cat /tmp/time_log.txt

# 挂载debugfs (如果没有挂载)
mount_debugfs:
	sudo mount -t debugfs none /sys/kernel/debug

.PHONY: all modules clean insmod rmmod dmesg dmesg_err lsmod modinfo test test_file mount_debugfs