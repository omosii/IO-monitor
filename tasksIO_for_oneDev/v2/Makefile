MODULE_NAME := io_monitor_v3
obj-m := io_monitor_v3.o

# 组合模块的子对象 (请把原 io_monitor_v3.c 重命名为 io_monitor_v3_main.c)
io_monitor_v3-objs := io_monitor_v3_main.o io_monitor_history.o

KERNEL_VERSION := $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build

all: modules

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o modules.order Module.symvers .*.cmd

install:
	@if [ -n "$(DEVICE)" ]; then \
		sudo insmod $(MODULE_NAME).ko device=$(DEVICE); \
	else \
		sudo insmod $(MODULE_NAME).ko device=sda3; \
	fi

uninstall:
	sudo rmmod $(MODULE_NAME) || true

reload: uninstall install

stats:
	@cat /proc/$(MODULE_NAME) 2>/dev/null || echo "Module not loaded"

log:
	@tail -f /tmp/io_monitor.log 2>/dev/null || echo "Log file not found"

help:
	@echo "Usage:"
	@echo "  make          - Build module"
	@echo "  make clean    - Clean build files"
	@echo "  make install [DEVICE=sdb1] - Install module"
	@echo "  make uninstall- Uninstall module"
	@echo "  make reload   - Reload module"
	@echo "  make stats    - Show IO statistics"
	@echo "  make log      - Show real-time log"

.PHONY: all modules clean install uninstall reload stats log help
