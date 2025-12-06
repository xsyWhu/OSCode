# Lab7：文件系统（Lab — Filesystem）

## 概述

本实验在现有内核基础上实现了一个简化的 xv6 风格的文件系统，使用一个 RAM-backed block device（内存盘）作为磁盘，并提供缓冲缓存、inode 缓存、简单的写前日志（log）以及目录/文件的读写接口。实现目标是让内核拥有自包含的文件系统以便在 QEMU 下运行与测试。

主要实现要点：
- 块大小 `BSIZE = 4096`（4KB），内存盘大小 `RAMDISK_BLOCKS = 8192`（约 32MB）。
- 支持 inode、直接/间接块（`NDIRECT = 12`）、目录条目、文件读写与创建/删除。
- 简单的写前日志接口以保持 FS 代码结构一致（RAM 磁盘不需跨重启恢复）。
- 缓冲缓存（bcache）采用小型 LRU 列表。
- 提供 `virtio` 磁盘驱动（`virtio_blk`）作为设备层，当前也有一个内存盘实现用于自包含测试。

## 环境与依赖

- Ubuntu/WSL2 或等效 Linux 环境
- RISC-V 交叉编译工具链（`riscv64-linux-gnu-*`）
- QEMU（支持 riscv64 virt 机器）

建议已完成前序实验（内存管理、中断）以保证内核其余部分可用。

## 构建与运行

在仓库根目录运行：

```bash
make           # 构建 kernel-qemu
make qemu      # 启动 QEMU（nographic）
```

清理：

```bash
make clean
```

注意：若因 `/tmp` 权限或交叉编译临时文件问题导致构建失败，可临时设置 `TMPDIR`：

```bash
export TMPDIR=$PWD/tmp
```

## 主要文件与目录

- `include/fs/fs.h`：文件系统接口与 on-disk 结构定义（superblock、dinode、inode、buf 等）。
- `kernel/fs/fs.c`：核心文件系统实现（inode 缓存、分配、目录、读写、bmap 等）。
- `kernel/fs/bio.c`：缓冲缓存（bcache）实现，包含 LRU 管理与设备读写接口封装。
- `kernel/fs/file.c`：进程/内核文件表实现、文件读写/关闭等操作。
- `kernel/fs/log.c`：简化的写前日志（log）接口。
- `kernel/fs/ramdisk.c`：RAM-backed block device（用于自包含测试）。
- `kernel/dev/virtio_blk.c`：virtio 块设备驱动（与 QEMU 的 virtio-blk 交互）。

## 常用调试 / 统计接口

- `fs_count_free_blocks()`：统计文件系统空闲数据块数。
- `fs_count_free_inodes()`：统计空闲 inode 数。
- `bcache_get_hits()` / `bcache_get_misses()`：缓冲缓存命中/未命中计数。
- `ramdisk_get_reads()` / `ramdisk_get_writes()`：内存盘读写计数。

这些函数在内核测试驱动或启动日志中可以被调用用于验证实现正确性与性能观察。

## 常见问题与说明

- 文件系统以 RAM 磁盘为主（`ramdisk.c`），因此在 QEMU 重启后数据不会持久化（但这使测试更可控）。
- `virtio_blk` 驱动会尝试探测 MMIO 区并与 QEMU 的 virtio 设备交互；在无 virtio 设备的环境中，RAM 磁盘仍然可用用于运行 FS。
- 编译器告警（例如未使用变量）在启用 `-Werror` 时会导致构建失败，请按提示修改或标记未使用变量。

## 后续扩展建议

- 持久化支持（将 RAM 磁盘替换为 virtio-backed 设备并验证日志恢复）。
- 引入更复杂的缓存策略或哈希加速的 buffer lookup。  
- 增加目录树遍历工具、`fsck` 风格的完整性检查。  
- 支持多线程并发下更强的锁策略（当前使用自旋锁、inode 缓存简单实现）。
