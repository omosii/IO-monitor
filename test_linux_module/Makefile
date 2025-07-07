obj-m += hello.o # 指定编译生成的模块文件名
all: # 默认目标，当运行 make 命令时会执行这个目标
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 
clean: # 清理目标，当运行 make clean 命令时会执行这个目标
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean 

# make -C：切换到指定目录执行 make
# /lib/modules/$(shell uname -r)/build：内核源码目录
    # $(shell uname -r) 获取当前运行的内核版本
# M=$(PWD)：指定当前目录为模块源码目录
# modules：编译目标，生成模块文件
# clean：清理目标，删除编译生成的模块文件

# 基本工作流
# 1、