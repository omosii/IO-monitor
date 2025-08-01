# 修改要编译的源文件名
obj-m += io_monitor_0731.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

insmod_demo:
	sudo insmod io_monitor_0731.ko device=/dev/sda3
rmmod:
	sudo rmmod io_monitor_0731

#显示信息
dmesg:
	sudo dmesg | tail -n 20

lsmod:
	sudo lsmod | grep io_monitor_0731