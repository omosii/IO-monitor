# 编译器设置
CC = gcc
CFLAGS = -Wall -O2

# 目标文件
TARGET = my_iostat

# 源文件和头文件
SRC = my_iostat.c
HEADERS = my_iostat.h

# 默认目标
all: $(TARGET)

# 编译规则，添加头文件依赖
$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# 清理规则
clean:
	rm -f $(TARGET)

# 安装规则（可选）
install:
	cp $(TARGET) /usr/local/bin/

# 声明伪目标
.PHONY: all clean install