# IO监控系统 (IO Monitor System)

## 项目概述

这是一个基于Linux内核模块的IO监控系统，用于实时监控特定设备的IO操作。系统包含两个主要模块：实时监控模块和历史记录模块，以及配套的用户空间工具。

## 项目结构

```
v1/
├── 📁 核心模块文件
│   ├── io_monitor.c              # 主模块入口，负责初始化和清理
│   ├── io_monitor.h              # 主模块头文件，定义通用结构和宏
│   ├── io_monitor_realtime.c     # 实时监控模块实现
│   ├── io_monitor_realtime.h     # 实时监控模块头文件
│   ├── io_monitor_history.c      # 历史记录模块实现
│   ├── io_monitor_history.h      # 历史记录模块头文件
│   └── Makefile                  # 内核模块编译配置
│
├── 📁 user_cmd/                  # 用户空间工具
│   ├── my_iostat.c              # 用户空间监控程序源码
│   ├── my_iostat.h              # 用户空间程序头文件
│   ├── my_iostat                # 编译后的可执行文件
│   └── Makefile                 # 用户空间程序编译配置
│
├── 📁 test_debugfs/             # 调试和测试工具
│   ├── time_log.c               # 时间日志内核模块
│   └── Makefile                 # 调试模块编译配置
│
├── 📁 old_code/                 # 历史版本代码
│   ├── io_monitor_history.c     # 旧版本历史记录模块
│   ├── io_monitor.h             # 旧版本头文件
│   ├── io_monitor_history.h     # 旧版本历史记录头文件
│   └── io_monitor.c             # 旧版本主模块
│
└── 📁 .vscode/                  # VS Code配置
    ├── settings.json            # VS Code设置
    └── c_cpp_properties.json    # C/C++配置
```

## 核心模块详解

### 1. 主模块 (io_monitor.c/h)

**功能**: 模块的入口点，负责整体初始化和清理工作

**主要特性**:
- 设备参数解析和验证
- 模块生命周期管理
- 子模块协调

**关键结构**:
```c
struct device_info {
    dev_t dev;           // 设备号
    bool valid;          // 设备是否有效
};
```

### 2. 实时监控模块 (io_monitor_realtime.c/h)

**功能**: 实时监控指定设备的IO操作，提供进程级别的IO统计

**主要特性**:
- 实时IO速率计算
- 进程和父进程信息跟踪
- 哈希表存储优化
- 50ms间隔的统计更新

**关键结构**:
```c
struct task_io_stats {
    pid_t pid;                    // 进程ID
    pid_t ppid;                   // 父进程ID
    char comm[TASK_COMM_LEN];     // 进程名
    char parent_comm[TASK_COMM_LEN]; // 父进程名
    unsigned long read_bytes;     // 读取字节数
    unsigned long write_bytes;    // 写入字节数
    unsigned long read_Bpms;      // 读取速率(B/s)
    unsigned long write_Bpms;     // 写入速率(B/s)
    u64 record_time_ns;           // 记录时间戳
    struct hlist_node task_hnode; // 哈希表节点
};
```

**接口**: `/proc/io_monitor_mod1`

### 3. 历史记录模块 (io_monitor_history.c/h)

**功能**: 定期记录IO统计信息，支持历史数据查询

**主要特性**:
- 链表结构存储历史记录
- 可配置的记录间隔
- 时间戳记录
- 内存管理优化

**数据结构**:
```c
// 单个进程IO信息
struct task_io_stats_history {
    pid_t pid;                    // 进程ID
    pid_t ppid;                   // 父进程ID
    char comm[TASK_COMM_LEN];     // 进程名
    char parent_comm[TASK_COMM_LEN]; // 父进程名
    unsigned long read_bytes;     // 读取字节数
    unsigned long write_bytes;    // 写入字节数
    struct list_head list;        // 链表节点
};

// 某一时刻的所有进程IO信息
struct io_record {
    struct timespec64 timestamp;  // 时间戳
    unsigned long tasks_num;      // 进程数量
    struct list_head* task_io_stats_history_head; // 进程IO信息链表
    struct list_head list;        // 记录链表节点
};
```

**接口**: `/proc/io_monitor_mod2`

## 用户空间工具

### my_iostat 程序

**位置**: `user_cmd/my_iostat`

**功能**: 用户空间监控工具，提供友好的IO统计信息显示

**主要特性**:
- 实时监控模式 (Mode 1)
- 历史查询模式 (Mode 2)
- 进程IO信息聚合
- 速率计算和显示

**使用方法**:
```bash
# 实时监控模式
./my_iostat 1 [interval_ms]

# 历史查询模式
./my_iostat 2 [period_seconds]
```

## 编译和使用

### 编译内核模块

```bash
# 编译主模块
make

# 编译用户空间工具
cd user_cmd && make
```

### 安装和使用

```bash
# 加载内核模块
sudo insmod myIOMonitor.ko device=/dev/sda3 interval_seconds=10

# 查看模块信息
sudo modinfo ./myIOMonitor.ko

# 查看内核日志
sudo dmesg | grep io_monitor

# 卸载模块
sudo rmmod myIOMonitor
```

### 模块参数

- `device`: 要监控的设备路径 (如 `/dev/sda3`)
- `interval_seconds`: 历史记录间隔时间 (秒)

## 技术特性

### 内核兼容性
- 支持Linux内核5.6.0前后的procfs接口变化
- 使用条件编译处理版本差异

### 性能优化
- 哈希表存储实时数据
- 链表管理历史记录
- 内存分配优化

### 安全性
- 用户空间数据验证
- 内核空间错误处理
- 资源清理机制

## 开发环境

- **操作系统**: Linux
- **内核版本**: 支持5.6.0前后版本
- **编译器**: GCC
- **开发工具**: VS Code (配置已包含)

## 注意事项

1. 需要root权限加载内核模块
2. 确保目标设备存在且可访问
3. 监控大量进程时注意内存使用
4. 建议在生产环境使用前充分测试

## 作者

Luyi Zhang

## 许可证

GPL
