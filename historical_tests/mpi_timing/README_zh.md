# MPI阶段计时 v1（2026-10-16）

目的：区分MPI作业等待时间发生在启动、初始化、缓冲准备、MPI通信、校验、同步还是退出。
不改HUCX、MPI安装、原验收二进制或已归档通过版本。生成新的独立计时二进制。

## 首轮

在node89解压后执行 `bash prepare_node89.sh`：校验两端旧基线、新程序编译、同步并校验哈希。
随后仅node89执行 `bash run_timing.sh p2p 64 1`，先看一次小包分阶段结果，不批量跑矩阵。
mpicc使用C99/O2/-Wall/-Wextra/-Werror；程序保持原数据模式、4KiB对齐和MPI调用顺序。
没有额外插入MPI_Barrier、预热、休眠或线程，不改接收深度与超时。

## 计时字段（毫秒）

- init_ms：MPI_Init调用。
- setup_ms：rank/模式查询、参数解析、对齐分配及初始清零、原有setup日志。
- fill_ms：各轮发送数据模式生成，累计。
- prepare_ms：各轮接收区memset，及Bcast root的预复制，累计。
- communication_ms：MPI_Sendrecv/Bcast/Gather调用，累计；包含MPI/HUCX处理、进度与等待对端。
- verify_ms：接收元数据检查和逐字节校验，累计。
- allreduce_ms：原程序已有的结果Allreduce，包含等其他rank完成校验，累计。
- cleanup_ms：原有PASS输出/flush及用户缓冲释放。
- finalize_ms：MPI_Finalize调用。
- other_ms：其余计时点和循环记账开销。
- app_ms：进入main后的首个时间点到MPI_Finalize返回。
- TIMING_ITER_EDGE展示第一/最后一轮通信耗时，完整每轮数据在日志中。

启动器使用Python clock_gettime_ns(CLOCK_MONOTONIC)，C使用相同CLOCK_MONOTONIC。
仅89本机启动器与rank0时间线相减；97只使用其自身时间差，禁止跨节点相减。

TIMING_JOB把作业总时间拆为：
1. launch_to_rank0_main_ms：启动器开始到89 rank0进入main，包括启动协调/进程创建/加载等。
2. rank0_app_ms：89 rank0从main入口到MPI_Finalize返回。
3. rank0_finalize_to_job_end_ms：此后到启动器回收命令，包括结果输出、其他rank完成和进程退出等待。
第一项不能单独等同SSH时间，第三项不能单独等同MPI_Finalize时间。

## 配置和判定

native=y、RNDV_THRESH=auto、RNDV_SCHEME=auto，RX1024/TIMEOUT100ms/RETRIES8。
PROTO_ENABLE=n、DIAG=n、TRACE默认y、注入0、原设备/优先级；不同时调整参数。
小包期望SEND、大包观察原生READ或WRITE，复用auto摘要验证数据及实际opcode。
每rank阶段总和与app_ms、每轮总和与阶段累计、89本机启动器三段关系都做一致性检查。
TIMING_CHECK PASS只表示计时记录完整自洽，不代表性能达标或原因为RM。
计时行在MPI_Finalize成功返回后集中打印，减少每轮输出对通信节奏的影响。

接口：`bash run_timing.sh p2p|bcast|gather BYTES [ITERS=1] [TRACE=y]`。
TRACE=n仅供之后根据证据单变量比较日志影响；本轮首测保持y。
关闭TRACE时明确PATH_CHECK NOT_EVALUATED，不伪报原生路径通过。

这不是OSU延迟/带宽基准；通信调用可能包含对端未就绪、注册、进度和调度等待。
计时与TRACE本身有开销，首轮只用于定位主要耗时阶段，不能将通信调用毫秒数当设备单包延迟。

## 验证边界

交付前本地没有MPI安装。C仅用最小MPI声明做语法/格式检查，非真实MPI编译链接。
脚本语法、阶段摘要模拟日志、启动计时包装器及包完整性在本地验证。
真实MPI编译和计时结果需节点执行后确认。
