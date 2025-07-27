obj-m += io_monitor.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

insmod_demo:
	sudo insmod io_monitor.ko device=/dev/sda
rmmod:
	sudo rmmod io_monitor

#显示信息
dmesg:
	sudo dmesg | tail -n 20

lsmod:
	sudo lsmod | grep io_monitor
