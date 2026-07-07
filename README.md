# 实验四：Linux 虚拟字符设备驱动

本目录实现课程设计实验四“Linux 驱动程序”。代码在 macOS 上编写，在 Linux 主机上编译、加载和测试。

## 文件说明

| 文件 | 作用 |
|---|---|
| `vchar_device.c` | 虚拟字符设备驱动源码 |
| `test_vchar.c` | 用户态测试程序 |
| `Makefile` | 编译内核模块和测试程序 |
| `run_demo.sh` | 一键编译、加载、测试、查看状态 |

## 功能点

- 自动分配设备号。
- 注册字符设备并自动创建 `/dev/vchar_device`。
- 实现 `open`、`read`、`write`、`release` 回调函数。
- 额外实现 `llseek`，方便测试读写偏移。
- 内置一页大小内核缓冲区，容量为 `PAGE_SIZE`。
- 支持多进程或多次打开同一设备，每次打开都有独立读写偏移。
- 通过 `/sys/class/vchar_device/vchar_device/status` 输出设备状态。
- 支持 `insmod` 动态加载和 `rmmod` 动态卸载。

## Linux 主机环境

建议使用 Ubuntu、Debian、RHEL、KylinOS、openEuler 等 Linux 发行版，并安装：

```bash
sudo apt install build-essential linux-headers-$(uname -r)
```

如果不是 Debian/Ubuntu 系统，请使用对应包管理器安装 GCC、make 和当前内核版本的 kernel headers。

## 编译与运行

```bash
cd experiment4_vchar
make
sudo insmod vchar_device.ko
ls -l /dev/vchar_device
sudo chmod 666 /dev/vchar_device
./test_vchar
cat /sys/class/vchar_device/vchar_device/status
sudo rmmod vchar_device
```

也可以直接运行：

```bash
chmod +x run_demo.sh
./run_demo.sh
```

## 预期输出

测试程序会两次打开 `/dev/vchar_device`，先通过第一个文件描述符写入数据，再分别从两个文件描述符读取，验证每次打开具有独立读写偏移。

`status` 文件会显示设备号、缓冲区容量、当前打开计数、累计读写次数等内核态状态信息。
