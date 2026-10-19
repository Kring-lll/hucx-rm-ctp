# HUCX RM+CTP 第一版（2026-10-08）

这是一份基于公开 HUCX、独立实现的实验版本，提供可运行的 UCP TAG、PUT、GET 通信路径和有界错误处理。整库构建与模拟设备上的真实 UCP 集成测试已经通过；node89/node97 硬件、真实网络丢包、HMPI/MPI 应用和性能尚未验收。节点上的旧 RM 私有源码没有上传或被读取，本包不能当成它的逐字迁移副本。

## 版本与文件

- HUCX：`https://gitcode.com/kunpengcompute/hucx.git`，提交 `43f61747585c67afd4e70e059232f38a7e9bfdab`。
- UMDK 阅读及本地编译头文件：`https://atomgit.com/openeuler/umdk.git`，提交 `e720dbead0b6dba8742028e162afed5dfd58df95`。
- 精确来源见 `BASELINES.json`。完整代码包根目录的 `SOURCE_MANIFEST.json` 记录实现提交及所有改动文件 SHA256。
- 核心新增 **8 个 C/头文件**。修改原有 **3 个文件**：URMA `Makefile.am`、`base/urma_md.h`、`base/urma_md.c`。另有独立测试、构建脚本及说明。

| 文件组（均位于 `src/uct/urma/rm_urma/`） | 责任 |
| --- | --- |
| `rm_urma_iface.c/.h` | Jetty/JFR、收发完成队列、接收缓存、能力申报、进度与安全停止 |
| `rm_urma_ep.c/.h` | AM、原生 READ/WRITE、pending、fence、异步 flush、端点关闭 |
| `rm_urma_wireup.c/.h` | RM+CTP 导入、返回地址、ACK、收包验证及接收流管理 |
| `rm_urma_reliable.c/.h` | 独立可靠消息窗口、乱序重排、重复抑制、超时退避、报文格式 |

共享 MD 修改修复了 rkey 缓存把 64 位地址截成 32 位、只比较 EID 一部分的问题，并同步修正日志格式。此缓存也被原有 RC/UM 使用；它们的硬件回归尚未执行，因此第一版只在新目录试验。

## 第一版如何通信

UCT 声明 `CONNECT_TO_IFACE`，通过 UCP 交换的接口地址直接导入 `URMA_TM_RM + URMA_CTP`。每个 iface 共享一个 Jetty、一个 JFR 和两条 CQ；端点不依赖 UM/UTP 辅道。运行选择 `UCX_TLS=rm_urma`。

报文有版本、会话 cookie、流 ID、序列号、累计 ACK 和返回地址。一个流允许 64 个未确认消息，接收端按序递交并丢弃重复包。发送缓存直到“本地完成记录已收到”且“对端已确认递交”才回收。ACK 本身不需要 ACK；ACK 丢失时，DATA 重发会再次获得累计 ACK。

单次 SEND 总长最多 4096 B，其中 RM 首部 88 B，UCT AM 数据最多 4008 B，里面仍包含 UCP 协议首部。大 TAG 消息沿用 UCP 的 eager 分片与 rendezvous。**7259/7260 B 是旧配置的历史边界，不能套用到本版。**原生 READ/WRITE 每次最多取设备能力与 1 MiB 的较小值；更大的应用操作由 UCP 切块。

原生 RMA 在同一端点上串行执行，并等待此前 AM 确认，优先保证顺序和生命周期。设备未申报读写能力、或 `UCX_RM_URMA_NATIVE_RMA=n` 时，不申报 UCT 原生 PUT/GET；UCP 可以使用 SEND 的软件 RMA 回退。模拟测试确认了该回退路径，但它不是硬件 READ/WRITE。

## 错误边界

| 情况 | 第一版行为 |
| --- | --- |
| DATA/ACK 丢失，但本地 SEND 有成功 CQE | 按累计 ACK 重传、退避、去重；达到上限后报告端点超时 |
| 本地 CQE 缺失 | 默认 5 s 后将共享 iface 标为失败，先停止 Jetty/JFR，再结束相关操作 |
| 硬件完成报错或 CQ 轮询报错 | 整个共享 iface 失败；相关端点错误回调最多一次；原生 READ/WRITE 不盲目重发 |
| 硬件构造的 fake CQE、无效完成 ID | 不把 `user_ctx` 当指针；保守停止共享资源 |
| 队列无法删除 | 保留注册内存及未完成 DMA；不伪造零拷贝成功，不释放可能仍被设备使用的内存 |
| 端点在操作途中关闭 | 延迟释放，待操作结束后清理；pending 按 UCT 关闭约定处理 |

超时需要应用持续调用 progress；URMA API 本身若阻塞，不能靠软件 progress 定时器打断。验收程序另有进程级时限。硬件错误后的自动重建和恢复尚未实现。

普通 LOCAL flush 支持轮询和异步 completion，并以调用时的操作 ID 为边界，后续操作不会拖住这次 flush。`flush(CANCEL)`、`flush(REMOTE)`、iface 异步 flush、UCP peer-failure 模式/`-e`、EP_CHECK、原子操作、AM_ZCOPY、事件唤醒、GPU 内存和 MULTI worker 均未开放。不要只打开能力位来启用这些功能。

接收流状态保留到 iface 关闭，默认最多 4096 个流；没有 FIN/安全淘汰机制。乱序载荷另有全局 16 MiB 预算，缺口包允许立即补齐并释放缓存。EP/操作查找仍使用链表，且每片单独注册内存；性能估计尚未校准，不能据此声称已达到 HMPI 的资源或性能目标。

## 本地验证与复现

`test_software.sh` 覆盖 64002 次按序递交、乱序、重复包、ACK 重放、窗口、报文校验、退避和反复清理。

`test_transport.sh` 直接运行本版 UCT 代码，模拟 DATA/ACK 丢失、重复包、64 格窗口回压、pending、fence、flush 快照、ACK 先于 CQE、READ/WRITE、错误回调、重试耗尽、fake/无效 CQE、缺失 CQE、队列删除失败、缓存预算、延迟销毁和构造中途失败。

`test_ucp.sh` 使用真实 UCP/UCT 和模拟 URMA/MD，逐字节验证双向 TAG 的 64、1024、4000、4008、4096、7259、7260、65536、1 MiB、8 MiB；分别强制 eager、原生 GET rendezvous 和 SEND-only 回退。显式 PUT/GET 验证 1 MiB，包括最后 1 B。原生 GET 路径同时检查 READ 提交计数，防止把回退误当硬件指令路径。

`acceptance.c` 还已在本机两进程 TCP 上验证 TAG 至 8 MiB 和双向 1 MiB PUT/GET，检查验收程序自身的握手、校验、关闭逻辑。**这项 TCP 结果不属于 RM+CTP 硬件证据。**节点运行时 TCP 只交换 worker 地址和 rkey；之后数据与测试协调均通过选定的 UCP transport。

本地复现需要 Linux、编译工具和 verbs 头文件。`build_host.sh` 使用公开 UMDK 头文件生成故意不可通信的 mock 链接库，编译整库并运行上述软件测试：

```bash
ASAN_OPTIONS=detect_leaks=0 HUCX_SANITIZE=1 HUCX_JOBS=8 \
  bash contrib/rm_ctp/build_host.sh /path/to/umdk /new/absolute/host-stage
```

本工作环境的 LeakSanitizer 无法读取进程信息，所以禁用了 LSan；不宣称已通过泄漏扫描。模拟提供者对创建/导入/注册对象的计数在各场景结束后归零。公开基线的打包、链表和内存池代码触发 alignment/pointer-overflow 告警，因此整体宿主构建关闭这两项 UBSan 检查，其余告警默认立即失败。纯可靠协议测试保留完整 ASan/UBSan。新 URMA/RM 模块另行保留完整检查集运行，具体结果见交付测试报告。

**mock SDK 仅用于本地编译/模拟测试，不能部署到节点。**节点构建使用节点已安装、与驱动配套的 SDK；不要用新下载的头文件混配旧二进制库。

## 节点隔离部署（SDK 核对后执行）

先把完整源码 ZIP 解压到一个新目录。它包含公开 HUCX 的完整源码和本版改动，尚未生成 configure/Makefile。检查 `SOURCE_MANIFEST.json` 和包哈希。

先只读运行 `contrib/rm_ctp/inspect_node.sh`，核对两节点 SDK、头文件能力、内核及 CTP 优先级映射。每轮仍只需要 1～2 条短输出命令；不索取节点源码。

依赖为 gcc/g++、make、autoconf、automake、libtoolize、flex、URMA 开发包及 verbs 头文件。公开 UM 源码即使配置 `--without-verbs` 仍包含 `infiniband/verbs.h`；脚本不会自动安装依赖或更新驱动。

构建脚本只接受实验根目录下一个**不存在**、且在源码树之外的目录，复制 source 并建立独立 build/install。把源码包解压目录和构建阶段目录分别命名，避免将阶段目录放进源码树。默认实验根为 `/home/y00993710/urma_rm_ctp_lab`，可通过 `HUCX_LAB` 指定。默认节点 SDK `/usr`，编译器 gcc/g++。不提供覆盖已有阶段目录的选项：

```bash
bash /path/to/hucx-rm-ctp-v1/contrib/rm_ctp/build_isolated.sh \
  /home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008
```

构建日志、安装哈希和验收程序均放在该新阶段目录。生成 `install/bin/rm_ctp_acceptance`。新协议与历史私有 RM 不兼容，**两端都使用本版新安装**。node97 可独立构建；若复制 node89 的安装产物，需要先核对架构、SDK/运行库并保持安装绝对路径一致。

节点优先级默认 1，只根据旧设备表选择，运行前须核实当前两端映射；`HUCX_CTP_PRIORITY` 可覆盖。`HUCX_DEVICE` 默认 `udmac0d1e2:1`。

首先单独确认 SEND 与大消息分片。两端设置相同模式、先启动 node97：

```bash
UCX_RM_URMA_NATIVE_RMA=n UCX_RNDV_THRESH=inf \
  bash /path/to/hucx-rm-ctp-v1/contrib/rm_ctp/run_node.sh \
  /home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install \
  --server 19991 --tag-only --iters 5
```

node89 使用同样设置，最后改为 `--client 9.88.43.97 19991 --tag-only --iters 5`。

SEND 通过后，再在两端启用原生 RMA，强制 `UCX_RNDV_THRESH=8k UCX_RNDV_SCHEME=get_zcopy`，去掉 `--tag-only`，使用另一个空闲端口。开启 `UCX_RM_URMA_TRACE=y` 可记录 `RM_CTP_POST/CQE`；确认 rendezvous 的 READ、PUT 的 WRITE、GET 的 READ，并检查两端非零失败是否如实报告。验收不应使用旧 perftest 的单独 exit=0 替代内容校验。

验收通过需要两端退出 0、各自 `PASS SUMMARY`、所有字节校验通过，且日志中的库及 lane 来自新目录的 `rm_urma`。正常通信以后再安排真实硬件故障/丢包、并发长测、资源消耗和 HMPI/MPI 应用；当前没有声称这些已通过。

## 继续开发与恢复

恢复时以完整代码包、补丁和检查点为依据，无需依赖先前临时开发路径。补丁仅对应上面的精确公开 HUCX 基线；有 Git 时先 `git apply --check`，无 Git 时直接使用完整源码包。

下一步最有价值的证据是：节点 SDK 兼容性、RM/CTP import 和接收完成的实际语义、SEND-only 跨节点校验、原生 READ/WRITE 的全字节校验。然后再设计安全的连接回收、硬件错误恢复、并发和资源优化。不要将这里的模拟丢包结果写成真实网络丢包结果。
