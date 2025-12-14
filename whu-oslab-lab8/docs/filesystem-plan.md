# 文件系统实验总体规划（Lab7）

> 依据《操作系统实践PPT-文件系统》中的 6 个任务与“测试与调试策略”章节内容，结合当前仓库（Lab6 完成度：已有 `user/`、`syscall/`、`fs/file.c` 但尚无磁盘/日志实现）的代码现状，整理实现 Lab7 的阶段性计划。

## 1. 需求总结（来源：PPT）

- **任务1 – 布局理解**：掌握 xv6 的磁盘布局（boot/super/log/inode/bitmap/data）与 `superblock`/`dinode` 结构；明确各区域大小和作用。
- **任务2 – inode 管理**：读懂 `ialloc/iget/iput/bmap` 等算法，弄清楚内存/磁盘 inode 的缓存、引用计数、同步语义。
- **任务3 – 布局设计**：在既有布局的基础上确定块大小、inode 组织、是否扩展属性等，输出自身 FS 的静态结构设计。
- **任务4 – 块缓存（bio）**：实现 `struct buf`、`bread/bwrite/brelse` 以及 LRU/引用计数策略，为所有 FS 模块提供统一的块访问层。
- **任务5 – 日志系统（log）**：实现写前日志 `logheader`、`begin_op/log_write/end_op/recover` 流程，保证事务原子性。
- **任务6 – 目录 & 路径解析**：实现 `dirent`、`dirlookup/link/unlink`、`namex`（`namei`/`nameiparent`）等，支撑用户态路径访问。
- **测试/调试章节**：覆盖完整性、并发、崩溃恢复、性能等用例，以及超级块/缓存/inode/IO 统计的调试辅助。

## 2. 阶段化实施路线

### 阶段 0：现状评估与基础准备

1. **梳理现有文件**：确认 `kernel/dev/` 尚无 VirtIO 块驱动，`kernel/fs/` 只有简化版 `file.c`（仅 console），`Makefile` 也未生成 `fs.img`。记录所有缺失模块。
2. **工具链搭建**：保留刚生成的 `docs/fs-ppt.txt` 作为 PPT 文本检索来源，确保 `.venv`+`pdfminer` 可复现（若团队成员需）。
3. **定义目标**：列出最终需要交付的内核接口（`sys_open/read/write/close/...`）、磁盘镜像格式、测试列表。

### 阶段 1：磁盘驱动与块抽象（对应任务1/4）

1. **VirtIO-BLK 接入**（PPT“磁盘和存储”章节）  
   - 新增 `kernel/dev/virtio_disk.c`（或移植 xv6 版本），在 `dev/Makefile`/`kernel/Makefile` 注册。  
   - 在 `kernel/boot/main.c` 初始化 `virtio_disk_init()`，并提供 `virtio_disk_rw()` API（同步块读写）。
   - 更新 `Makefile`：添加 `fs.img` 生成规则（可直接参考 xv6 `mkfs` 或编写最小工具），以及 `qemu` 命令里的 `-drive … -device virtio-blk-device`.
2. **块缓存骨架（bio.c/h）**  
   - 在 `kernel/fs/` 新增 `bio.c` + 头文件，定义 PPT 提到的 `struct buf`、哈希 + LRU 列表、`binit/bget/bread/bwrite/brelse`.  
   - 与 `virtio_disk_rw()` 打通：`bwrite` 检查 `b->dirty` 并触发写，`bread` 缓存 miss 时向驱动发读请求。
3. **自检**：在 `kernel/boot/main.c` 添加最小块读写测试（例如读超级块/零块），确保块缓存 + 驱动链路可单独运行。

### 阶段 2：磁盘布局与元数据（任务1/2/3）

1. **超级块/磁盘常量**  
   - 创建 `include/fs/fs.h`：定义 `BSIZE`、块布局、`struct superblock`、`struct dinode`、`NDIRECT/INDIRECT` 等；同步新增 `mkfs` 工具/脚本生成初始 `fs.img`.  
   - 在 `kernel/fs/fs.c`（新文件）实现 `fs_init()`：读取超级块、校验 `magic`、缓存 `sb`.
2. **inode cache & allocator**  
   - 依据 PPT 任务2，实现 `icache`（`struct { struct spinlock lock; struct inode inode[NINODE]; }`）、`iget/ilock/iunlock/iput/ialloc/bmap`.  
   - 把 `kernel/fs/file.c` 扩展为完整文件接口（支持 `FD_INODE`、`pipe/console`），`struct file` 中添加 `off`、`ip` 等字段。  
   - 引入 `log_write()` 占位调用（先与日志模块解耦，可用 stub，阶段 4 真正实现）。
3. **块分配/位图**  
   - 添加 `balloc/bfree`：使用 PPT 中“位图块”描述，在 `fs.c` 里读写 `bmapstart` 区域管理数据块。  
   - 确认 `bmap()` 对直接块、间接块处理，以及失败路径（空间不足）均已覆盖。
4. **内核接口联调**  
   - 在 `kernel/syscall/sysfile.c`（若不存在则新增）实现 `sys_open/read/write/close/fstat/link/unlink/mkdir/chdir/exec` 所需的内核入口，调用上述 FS API。  
   - 更新 `user/ulib.c` / `user/usys.S` 补齐对应系统调用号（Lab6 可能只实现部分）。

### 阶段 3：目录与路径解析（任务6）

1. **目录项 & 查找**  
   - 定义 `struct dirent`（PPT 提供的 `inum+name` 形式），实现 `dirlookup/dirlink/dirunlink`.  
   - 处理特殊目录项 `"."`、`".."`，并在 `ialloc` 时自动设置父子关系。
2. **路径解析**  
   - 实现 `namex(path, nameiparent, name)`，并基于它导出 `namei` 与 `nameiparent`。  
   - 在 `sys_open/link/unlink/mkdir/chdir` 中调用 `namex`，遵循 PPT 对长文件名、目录大小的限制说明（`DIRSIZ` 14/32）。  
   - 为并发访问添加 `sleeplock` 防护，避免目录读写竞态。

### 阶段 4：日志系统（任务5）

1. **log 结构体**  
   - 按 PPT 的 `struct logheader`/`log_state` 定义 `kernel/fs/log.c`，并在 `fs_init()` 后调用 `log_init(dev, &sb)`.  
   - 支持 `LOGSIZE` >= `MAXOPBLOCKS`，确保一次系统调用的最大块写数量不超过日志容量。
2. **接口实现**  
   - `begin_op/end_op`：跟踪并发系统调用（`outstanding`），在 `end_op` 里调用 `commit`。  
   - `log_write(struct buf *b)`：给 `buf` 打 `B_DIRTY`，并将 `blockno` 记录在 `logheader`。  
   - `recover_from_log()`：启动时检查日志头，必要时 replay。  
   - 与 `bio`/`virtio_disk` 串联，确保提交时复制数据块到日志区再刷回数据区。
3. **调用点替换**  
   - 将 `bwrite()` 只负责写缓存，真正的磁盘刷写由日志提交调用 `write_log()`/`install_trans()` 负责。  
   - 在 inode/data/bitmap 修改处调用 `log_write()`，例如 `bmap` 扩容、`dirlink` 更新目录块、`balloc` 置位等。

### 阶段 5：文件描述符层与系统调用整合

1. **`struct file` 扩展**：新增 `enum filetype { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_CONSOLE }`、记录偏移 `off`、指向 `struct pipe`/`struct inode`.  
2. **`sysfile.c`**：实现 `sys_read/write/open/close/fstat/link/unlink/mkdir/chdir/exec/pipe/dup`.  
3. **用户库**：在 `user/` 中补齐 `open/close/pipe/mkdir/chdir` 等封装，并添加最小 `sh`/`ls`/`cat` 等测试程序，生成到 `fs.img`.  
4. **引导程序**：更新 `user/init.c` 在启动时挂载根目录、运行 `sh` 或测试。确保 `kernel/boot/main.c` 恢复为“加载 initcode -> scheduler -> 用户态”流程。

### 阶段 6：测试与调试（对应 PPT “测试与调试策略”）

1. **单元测试**：在 `user/` 添加以下测试并加入 `Makefile` 目标：  
   - `fs_integrity`: 覆盖 PPT 中的 “创建/写入/读取/删除” 流程。  
   - `fs_concurrent`: `fork` 多个子进程并发创建/删除文件。  
   - `fs_perf_small` & `fs_perf_large`: 量化 PPT 中的小文件 & 大文件写入时间。  
   - `fs_crash`: 通过在 `log.c` 中插入“崩溃点”或使用 `panic_on_purpose` 模拟崩溃后重启，验证日志恢复。
2. **内核调试钩子**：根据 PPT “调试建议” 添加：  
   - `debug_superblock()`：打印 `sb` 内字段。  
   - `debug_icache()`：遍历 `icache` 显示 `ref/type/size`.  
   - `debug_bcache()`：统计缓存命中/丢失。  
   - `debug_disk_io()`：维护 `disk_read_count`/`disk_write_count`.  
   - 通过 `sys_debugfs` 或内核命令开关控制输出。
3. **自动化脚本**：在根 `Makefile` 增加 `make fs-tests`，顺序运行用户测试并收集日志；提供 `make format-fs`（重新生成 `fs.img`）以保持一致性。

### 阶段 7：报告与文档

1. **更新 `Report.md`**：补充文件系统架构、数据结构、日志流程图、性能/一致性测试结果。  
2. **补充 `README.md`**：文档运行方式（如何生成 `fs.img`、如何运行测试、如何模拟崩溃恢复）。  
3. **PPT 思考题复盘**：在报告尾部回答“设计权衡/一致性/性能/扩展/可靠性”五类问题，确保与 PPT 对应。

## 3. 里程碑与风险提示

- **M1（磁盘 + 块缓存）**：完成 VirtIO 驱动、`bio`，能稳定读写块。风险：驱动初始化失败、QEMU 参数遗漏。
- **M2（inode + 目录）**：`fs.img` 可被内核挂载，`ls` 能列出根目录。风险：位图/引用计数错误导致 panic。
- **M3（日志系统）**：`fs_crash` 测试通过，日志可恢复。风险：`begin_op/end_op` 嵌套或 `LOGSIZE` 不足。
- **M4（测试通过）**：全部 `fs-*` 用户测试 + 手工压测通过，报告/README 完成。

出现以下情况需额外关注：
- `fs.img` 损坏：保留“clean image”备份，调试时启用 `mkfs` 重建。
- 日志恢复死循环：确保 `recover_from_log` 幂等，在提交前先写 header，恢复后清空。
- 并发死锁：检查 `icache.lock` / `log.lock` / `bcache.lock` 获取顺序，必要时引入层级约束。

> 以上计划覆盖 PPT 明确的所有任务点，并映射到仓库中需新增/修改的具体文件。按照阶段推进可逐步得到一个具备 VirtIO 块设备、块缓存、inode/目录、日志、系统调用与测试配套的 Lab7 文件系统实现。
