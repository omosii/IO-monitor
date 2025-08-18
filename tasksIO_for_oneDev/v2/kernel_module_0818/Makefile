MODULE_NAME := io_monitor_v3
obj-m := io_monitor_v3.o

# 组合模块的子对象
io_monitor_v3-objs := io_monitor_v3_main.o io_monitor_history.o io_monitor_realtime.o

KERNEL_VERSION := $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build

BUILD_DIR := $(PWD)/build
TMP_BUILD_DIR := $(PWD)/tmp_build

all: modules

$(BUILD_DIR) $(TMP_BUILD_DIR):
	mkdir -p $@

modules: | $(BUILD_DIR) $(TMP_BUILD_DIR)
	$(MAKE) -C $(KERNEL_DIR) M=$(TMP_BUILD_DIR) src=$(PWD) modules
	@cp -f $(TMP_BUILD_DIR)/$(MODULE_NAME).ko $(BUILD_DIR)/

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(TMP_BUILD_DIR) clean || true
	rm -f *.o *.ko *.mod.c *.mod.o modules.order Module.symvers .*.cmd
	rm -rf $(BUILD_DIR) $(TMP_BUILD_DIR)

install: modules
	@if [ -n "$(DEVICE)" ]; then \
		sudo insmod $(BUILD_DIR)/$(MODULE_NAME).ko device=$(DEVICE); \
	else \
		sudo insmod $(BUILD_DIR)/$(MODULE_NAME).ko device=/dev/sda3; \
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
	@echo "  make install [DEVICE=/dev/sda3] - Install module"
	@echo "  make uninstall- Uninstall module"
	@echo "  make reload   - Reload module"
	@echo "  make stats    - Show IO statistics"
	@echo "  make log      - Show real-time log"

.PHONY: all modules clean install uninstall reload stats log help
