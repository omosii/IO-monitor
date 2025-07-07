
# 编写简单内核模块

尝试从零开始创建一个可用的Linux内核模块，涵盖环境搭建、模块编写、编译加载、调试优化等完整流程。

## 一、开发环境准备

### ​​安装编译工具链
```bash
sudo apt-get update
sudo apt-get install build-essential libncurses5-dev libssl-dev
```
- build-essential：基础编译工具集，它包含了在Linux系统上进行软件开发所需的基本工具链，是编译和构建软件的基础依赖，用于 编译内核模块(需配合其他工具)。
- libncurses5-dev：终端界面开发库，libncurses5-dev是​​ncurses库的开发版本​​，提供了创建文本用户界面(TUI)所需的头文件和库，用于 开发命令行交互工具(如vim、htop等)。
    - 在内核编译过程中，make menuconfig命令依赖libncurses5-dev来提供基于文本的图形配置界面。如果没有安装这个库，就无法使用菜单方式配置内核选项。
- libssl-dev：安全通信开发库，libssl-dev是​​OpenSSL的开发版本​​，提供了SSL/TLS协议和加密功能的开发支持，可用于某些内核模块的编译(如加密文件系统)。

### ​获取内核头文件
内核头文件包含了编译模块所需的内核API定义：
```bash
sudo apt-get install linux-headers-$(uname -r)
```

### 验证环境
检查gcc和make是否安装成功：
```bash
gcc --version
make --version
```

## 二、编写简单内核模块

### 创建模块源代码文件​​（hello.c）：

### 创建Makefile​​：

## 三、编译与加载模块

## 编译模块​​：
## 加载模块​​：
## 查看模块信息​​：
## 卸载模块​​：