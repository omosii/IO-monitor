obj-m += io_monitorv2.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

insmod_demo:
	sudo insmod io_monitorv2.ko
rmmod:
	sudo rmmod io_monitorv2

#显示信息
dmesg:
	sudo dmesg | tail -n 20

lsmod:
	sudo lsmod | grep io_monitorv2

cat:
	sudo cat /proc/io_monitorv2
