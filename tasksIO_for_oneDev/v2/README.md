# tasksIO_for_oneDev/v2

本项目用于监控单个块设备上的进程级读写活动，包含内核模块 (kernel_module_0818) 与用户态命令工具 (user_cmd)。

## 目录结构概览
- kernel_module_0818/  
  - io_monitor_v3_main.*: 模块入口、参数与 kprobe 注册
  - io_monitor_history.*: 异步日志写入到 /tmp/io_monitor.log
  - io_monitor_realtime.*: 暴露 /proc/io_monitor_v3 实时聚合视图
- user_cmd/  
  - (典型会包含查看 /proc、解析日志、切换设备的简单 CLI 工具)

## kernel_module_0818 功能要点
1. 通过 kprobe 挂载 submit_bio 捕获块层 I/O。
2. 仅监控指定块设备 (模块参数 device=，默认 /dev/sda3)。
3. 日志异步写入: /tmp/io_monitor.log  
   日志行格式:
   ```
   <sec>.<nsec> major minor pid comm ppid pcomm read_kb write_kb
   ```
   仅一行代表一次 bio（读或写），read_kb 与 write_kb 二选一非 0。
4. 实时聚合接口：`/proc/io_monitor_v3`  
   - 读取尾部最多 256KB / 最近 500 行
   - 聚合每进程读/写 KB 与估算速率 (KB/s)
5. Slab 缓存优化：进程统计结构 / 日志条目 / 行缓冲
6. 过滤规则: 仅当前设置的 dev_t + 读/写开关

## 编译与加载
```
cd kernel_module_0818
make
sudo insmod io_monitor_v3.ko device=/dev/sdXN   # 例如: /dev/sda3
dmesg | tail
```

## 卸载
```
sudo rmmod io_monitor_v3
dmesg | tail
```

## 查看
```
cat /proc/io_monitor_v3
tail -n 20 /tmp/io_monitor.log
```

## 重新指定设备
需重新加载:
```
sudo rmmod io_monitor_v3
sudo insmod io_monitor_v3.ko device=/dev/nvme0n1p3
```

