# Lab7：文件系统

本仓库用于展示 Lab7（文件系统）阶段的成果：基于 RISC-V 内核实现磁盘块管理、inode/目录操作、写前日志、mkfs 工具与内核自测。

---

## 1. 环境

- Ubuntu 或 WSL2
- RISC-V 交叉工具链：`riscv64-linux-gnu-gcc`/`ld`
- QEMU 5.0+，要求 virtio modern 接口（运行时需 `-global virtio-mmio.force-legacy=false`）
- Python 3（执行 `tools/mkfs.py` 构建镜像）

快速检查：

```bash
riscv64-linux-gnu-gcc --version
qemu-system-riscv64 --version
```

---

## 2. 构建与运行

1. **编译内核/用户程序**

   ```bash
   make
   ```

2. **生成文件系统镜像**

   ```bash
   python3 tools/mkfs.py fs.img 8   # 8MB，自动写入超级块、根目录、位图
   ```

3. **运行内核与自测**

   ```bash
   make qemu         # 自动串行运行 Lab7 自测
   ```
   过程中会看到 `[LAB7] test_filesystem_integrity/concurrent_access/performance`、`debug_*` 等输出，可据此确认状态。

4. **调试与清理**

   - `make qemu-gdb`：带调试 stub。
   - `make clean`：清理生成文件。

---

## 3. 文件系统特性

- **块缓存 (`kernel/fs/bio.c`)**：32 块 LRU 缓存，所有 `bread/bwrite` 都经过 virtio 驱动，并统计读写次数。
- **写前日志 (`kernel/fs/log.c`)**：`begin_op/log_write/end_op` 包装了日志头与提交流程，`recover_from_log()` 在启动时自动重放。
- **inode/目录 (`kernel/fs/fs.c`, `dir.c`, `file.c`)**：支持 11 个直接块 + 1 个一级间接块，`inode_create/dirlink/namei` 构成整个路径访问链。
- **镜像构建 (`tools/mkfs.py`)**：Python 计算布局并写入超级块、inode 区与位图，可通过参数更改镜像大小。
- **调试辅助**：`debug_filesystem_state()` 打印超级块和空闲块数，`debug_inode_usage()` 直接扫描磁盘 inode，`debug_disk_io()` 给出 I/O 统计。

---

## 4. 测试流程

`kernel/boot/main.c` 在内核启动完毕后自动执行以下测试：

| 测试 | 说明 | 默认规模 |
| --- | --- | --- |
| `test_filesystem_integrity` | 创建 `testfile` → 写入 `"Hello, filesystem!"` → 读回校验 → 删除。 | 单文件 |
| `test_concurrent_access` | 多个 worker 循环创建/删除 `test_<worker>_<iter>`，验证 inode 与块回收。 | `LAB7_CONCURRENT_WORKERS=2`、`LAB7_CONCURRENT_ITERS=2` |
| `test_filesystem_performance` | 写入 2 个 4B 小文件 + 1 个 4MB 大文件，统计 ticks。 | `LAB7_PERF_SMALL_FILES=2`、`LAB7_PERF_LARGE_WRITES=1` |
| `debug_*` | 打印超级块、空闲 inode、I/O 次数，便于定位问题。 | — |

可以在 `kernel/boot/main.c` 中调大上述宏以施加更多压力；若缓存不足，可同步修改 `kernel/fs/bio.c` 的 `NBUF`。

---

## 5. 目录导航

```
.
├── kernel/fs/            # 文件系统核心：bio、fs、dir、log、file、pipe
├── kernel/dev/virtio_disk.c
├── kernel/boot/main.c    # 启动及 Lab7 测试入口
├── tools/mkfs.py         # 生成 fs.img
├── Report.md             # Lab7 实验报告
└── README.md             # 本文件
```

---
## 6. 后续工作

- 增加更大的块缓存与 `sleep/wakeup` 机制，减少在 `begin_op` 和 `balloc` 中的忙等。
- 扩展 inode 地址字段（双重间接块或 extent）以支持更大的文件。
- 引入自动化脚本，对镜像进行崩溃恢复回归测试。

*** 更多细节请参考 `Report.md` ***
