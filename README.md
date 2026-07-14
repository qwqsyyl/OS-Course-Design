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

## 答辩推荐：完全手动演示

答辩时不运行 `run_demo.sh`，优先使用 `vchar_ctl` 中的命令逐步执行 `open/write/read/close`，便于直接展示每条命令与系统调用、驱动回调之间的对应关系。

### 1. 编译、加载和检查设备

```bash
make clean
make
modinfo ./vchar_device.ko
sudo insmod vchar_device.ko
lsmod | grep vchar_device
ls -l /dev/vchar_device
sudo chmod 666 /dev/vchar_device
cat /sys/class/vchar_device/vchar_device/status
```

模块刚加载时缓冲区为空，`buffer_size=0`、`open_count=0`、`total_reads=0`、`total_writes=0`。

### 2. 使用封装后的 read 和 write 命令

`vchar_ctl` 提供可以直接从命令行执行的 `read`、`write` 子命令。每条命令内部都会打开设备、执行相应系统调用并关闭设备：

```bash
# 内部依次调用 open()、write()、close()
./vchar_ctl write "我是谁"

# 内部依次调用 open()、read()、close()
./vchar_ctl read

# 只读取指定字节数，例如一个 UTF-8 汉字通常是 3 字节
./vchar_ctl read 3

# 查看内核返回的设备状态
./vchar_ctl status
```

预期核心输出如下：

```text
open: fd=3, device=/dev/vchar_device, mode=w
write: 9 of 9 bytes
open: fd=3, device=/dev/vchar_device, mode=r
read: 9 bytes: 我是谁
```

这里的 `read` 和 `write` 是用户态命令行子命令，程序内部实际调用 Linux 的 `read()`、`write()` 系统调用，最终进入驱动的 `vchar_read()`、`vchar_write()` 回调。

### 3. 在命令行连续执行 open、read、write 和 close

如果需要单独输入 `open`、`read`、`write`、`close`，运行交互命令模式：

```bash
./vchar_ctl shell
```

然后依次输入：

```text
vchar> open rw
vchar> write 我是谁
vchar> seek 0
vchar> read 9
vchar> status
vchar> close
vchar> quit
```

这几条命令分别直接调用：

| `vchar_ctl shell` 命令 | 用户态系统调用 | 驱动回调 |
|---|---|---|
| `open rw` | `open()` | `vchar_open()` |
| `write 我是谁` | `write()` | `vchar_write()` |
| `seek 0` | `lseek()` | `vchar_llseek()` |
| `read 9` | `read()` | `vchar_read()` |
| `close` | `close()` | `vchar_release()` |

必须使用同一个交互进程，才能让 `open` 得到的文件描述符一直保留到后续 `read/write/close`。如果把四条命令做成四个独立进程，`open` 命令退出时文件描述符会被内核自动关闭，后面的命令无法继续使用它。

### 4. 使用 Bash 文件描述符单独执行 open、write 和 close

下面使用 Bash 文件描述符 3 作为写端：

```bash
# open：当前 Shell 打开设备，触发 vchar_open
exec 3>/dev/vchar_device

# 此时应看到 open_count=1
cat /sys/class/vchar_device/vchar_device/status

# write：向 fd 3 写入“我是谁”，触发 vchar_write
printf '我是谁' >&3

# 此时 buffer_size=9、total_writes=1、open_count=1
cat /sys/class/vchar_device/vchar_device/status

# close：关闭 fd 3，触发 vchar_release
exec 3>&-

# 此时 open_count 回到 0
cat /sys/class/vchar_device/vchar_device/status
```

“我是谁”包含 3 个汉字，UTF-8 编码下通常每个汉字占 3 字节，因此 `buffer_size=9`。

### 5. 使用 Bash 文件描述符单独执行 open、read 和 close

下面使用文件描述符 4 作为读端：

```bash
# open：重新打开设备，新的文件实例 offset 从 0 开始
exec 4</dev/vchar_device

# 此时应看到 open_count=1
cat /sys/class/vchar_device/vchar_device/status

# read：从 fd 4 读取到文件末尾，触发 vchar_read
cat <&4
printf '\n'

# 此时 total_reads 增加，fd 4 的 offset 已经位于数据末尾
cat /sys/class/vchar_device/vchar_device/status

# 再读一次不会输出字符，因为已经到达 EOF
cat <&4

# close：关闭 fd 4，触发 vchar_release
exec 4<&-

# 此时 open_count 回到 0
cat /sys/class/vchar_device/vchar_device/status
```

第二次 `cat <&4` 返回 0 字节是正常的 EOF 语义，不是读取失败。重新关闭并打开设备会创建新的文件实例，新 offset 又从 0 开始。

### 6. 在终端验证两个打开实例的偏移独立

以下演示继续使用缓冲区中的“我是谁”。fd 5 先读取第一个汉字，fd 6 仍能从自己的 offset=0 读出完整内容：

```bash
# 两次 open，内核中会有两个独立的 vchar_file_state
exec 5</dev/vchar_device
exec 6</dev/vchar_device

# fd 5 读取 3 字节，即第一个汉字“我”
dd bs=3 count=1 status=none <&5
printf '\n'

# fd 6 不受 fd 5 影响，仍从 offset=0 读取完整内容“我是谁”
cat <&6
printf '\n'

# fd 5 从自己的 offset=3 继续读取，输出“是谁”
cat <&5
printf '\n'

# 分别 close 两个文件描述符
exec 5<&-
exec 6<&-
```

若演示英文字符串，可把 `dd bs=3 count=1` 改为 `dd bs=1 count=1`，先读取一个英文字符。

### 7. 查看状态、内核日志并卸载

```bash
cat /sys/class/vchar_device/vchar_device/status
sudo dmesg | grep vchar_device | tail -n 20
sudo rmmod vchar_device
lsmod | grep vchar_device
```

最后一条 `lsmod | grep vchar_device` 没有输出，表示模块已经卸载。卸载后 `/dev/vchar_device` 和 sysfs 状态节点消失是正常现象。

### 操作与驱动回调的对应关系

| 终端操作 | 内核回调 | 作用 |
|---|---|---|
| `exec 3>/dev/vchar_device` | `vchar_open` | 打开写端文件实例，创建独立 offset |
| `printf '我是谁' >&3` | `vchar_write` | 将用户态数据复制到内核缓冲区 |
| `exec 3>&-` | `vchar_release` | 用户态 close，释放文件私有状态 |
| `exec 4</dev/vchar_device` | `vchar_open` | 打开读端文件实例，offset 初始化为 0 |
| `cat <&4` | `vchar_read` | 从内核缓冲区复制数据到用户态 |
| `exec 4<&-` | `vchar_release` | 关闭读端文件实例 |

Linux `file_operations` 没有单独的 `.close` 字段，用户态 `close()` 最终对应驱动的 `.release` 回调。

## 修改手动演示的输出字符

只需修改 write 步骤中 `printf` 引号内的内容，例如：

```bash
exec 3>/dev/vchar_device
printf 'hello operating system' >&3
exec 3>&-

exec 4</dev/vchar_device
cat <&4
printf '\n'
exec 4<&-
```

字符串包含空格时必须放在引号中。中文采用 UTF-8 编码，状态中的 `buffer_size` 是字节数，不是汉字个数。

建议更换为更短的字符串前重新加载模块，因为当前驱动采用普通文件式覆盖语义：覆盖已有内容不会自动缩短原来的有效数据长度。

```bash
sudo rmmod vchar_device
sudo insmod vchar_device.ko
sudo chmod 666 /dev/vchar_device
```

## test_vchar 自动测试（答辩备用）

手动演示完成后，如需额外验证用户态测试程序，可以运行：

```bash
./test_vchar "course design virtual char device test"
./test_vchar "这是修改后的测试内容"
```

不传参数直接执行 `./test_vchar` 时，程序会使用源码中的默认字符串 `hello from user space to vchar_device\n`。`test_vchar` 内部会自动完成两次打开、一次写入、三次读取、偏移调整和两次关闭，因此不适合作为逐步展示每个回调的主流程。

## 一键脚本（仅用于快速自测）

```bash
chmod +x run_demo.sh
./run_demo.sh
```

脚本会自动完成编译、加载、测试、状态查看和卸载。答辩时建议使用前面的完全手动流程。

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

## 建议答辩展示顺序

1. 用 `uname -r` 说明模块针对当前 Linux 内核编译。
2. 用 `make` 展示 `.ko` 驱动模块和 `vchar_ctl` 用户态命令工具的编译。
3. 用 `insmod`、`lsmod` 说明模块可以动态加载。
4. 用 `ls -l /dev/vchar_device` 说明设备节点由驱动自动创建。
5. 运行 `./vchar_ctl write "我是谁"` 和 `./vchar_ctl read`，展示封装后的命令行指令。
6. 运行 `./vchar_ctl shell`，依次输入 `open/write/seek/read/close`，展示完整系统调用生命周期。
7. 同时打开 fd 5 和 fd 6，证明两个打开实例具有独立 offset。
8. 在每个阶段执行 `status`，展示 `open_count` 和读写计数变化。
9. 用 `dmesg` 展示内核中的加载、打开和释放日志。
10. 用 `rmmod` 和 `lsmod` 说明模块可以动态卸载并释放资源。

## 预期输出

手动写入“我是谁”后，状态应显示 `buffer_size=9`、`total_writes=1`；读取时终端输出“我是谁”，`total_reads` 随成功读取次数增加。打开设备期间 `open_count` 增加，执行对应的 `exec N>&-` 或 `exec N<&-` 后减少。

`status` 文件会显示设备号、缓冲区容量、当前打开计数、累计读写次数等内核态状态信息。
