# 实验四：Linux 虚拟字符设备驱动

本目录实现课程设计实验四“Linux 驱动程序”。代码在 macOS 上编写，在 Linux 主机上编译、加载和测试。

## 文件说明

| 文件 | 作用 |
|---|---|
| `vchar_device.c` | 虚拟字符设备驱动源码 |
| `test_vchar.c` | 用户态测试程序 |
| `vchar_ctl.c` | 将 `open/read/write/close` 系统调用封装成命令的用户态工具 |
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
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

如果不是 Debian/Ubuntu 系统，请使用对应包管理器安装 GCC、make 和当前内核版本的 kernel headers。

## 从 GitHub 获取项目

```bash
git clone https://github.com/qwqsyyl/OS-Course-Design.git
cd OS-Course-Design
```

如果代码已经位于 Linux 主机，只需进入包含 `Makefile`、`vchar_device.c` 和 `run_demo.sh` 的目录。

## 状态字段说明

`cat /sys/class/vchar_device/vchar_device/status` 会显示：

| 字段 | 含义 |
|---|---|
| `device` | 设备名称 |
| `major` / `minor` | 内核自动分配的主、次设备号 |
| `buffer_size` | 当前有效数据长度 |
| `buffer_capacity` | 一页内核缓冲区容量，通常为 4096 字节 |
| `open_count` | 当前正在打开设备的文件实例数 |
| `total_reads` | 模块本次加载以来累计成功读取次数 |
| `total_writes` | 模块本次加载以来累计成功写入次数 |

手动执行 `exec N>&-` 或 `exec N<&-` 关闭全部设备文件描述符后，`open_count=0` 是正常结果。

## 可选检查与清理

```bash
# 查看设备号是否已经注册
cat /proc/devices | grep vchar_device

# 查看更聚焦的驱动日志
sudo dmesg | grep vchar_device | tail -n 20

# 清理编译生成文件
make clean
```

如果 `insmod` 提示模块已经存在，先执行：

```bash
sudo rmmod vchar_device
sudo insmod vchar_device.ko
```

如果 `/dev/vchar_device` 不存在，先检查模块和日志：

```bash
lsmod | grep vchar_device
sudo dmesg | tail -n 30
```

如果提示 `Permission denied`，执行：

```bash
sudo chmod 666 /dev/vchar_device
```

如果模块仍被占用，使用下面的指令检查哪个进程打开了设备，关闭对应程序后再卸载：

```bash
sudo lsof /dev/vchar_device
sudo rmmod vchar_device
```


