# Lab8 扩展项目说明

仅包含 Lab8 扩展相关的快速说明与使用方法。

## 已实现的扩展
- 优先级调度 / 多级反馈队列（MLFQ）+ aging/boost
- 内核日志系统（klog + logread）
- ELF 加载器
- IPC：管道（已有）+ 消息队列（msgget/msgsend/msgrecv）
- Copy-on-Write (COW) fork
- 实时调度：简易 EDF（setrealtime）

## 构建与运行 
- 运行：`make qemu`
- 输出：控制台包含内置测试（priority_test、elfdemo、msgdemo、cowtest、rtdemo、MLFQ showcase）；logread 自动打印内核日志。

## 主要用户测试/工具
- `priority_test`：验证 setpriority/getpriority 与 MLFQ 行为。
- `logread`：读取内核日志。
- `elfdemo`：验证 ELF 加载段映射。
- `msgdemo`：消息队列收发示例。
- `cowtest`：验证 COW 写时复制。
- `rtdemo`：实时任务（EDF）示例。

## 关键接口
- 调度/实时：`setpriority(pid, prio)`、`getpriority(pid)`、`setrealtime(pid, deadline_ticks)`
- 日志：`klog(buf, n)`（用户态读取），`klog(level, fmt, ...)`（内核态记录）
- IPC：`pipe(fd[2])`、`msgget(key)`、`msgsend(qid, buf, len)`、`msgrecv(qid, buf, maxlen)`

更多实现细节参见 `Report.md`。***
