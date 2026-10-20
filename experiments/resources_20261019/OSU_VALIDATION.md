# OSU 7.5.2 构建验收（2026-10-20）

目标：按导师要求对比RM/RC内存、osu_latency、osu_bw，以及osu_allreduce/osu_bcast/osu_gatherv/osu_alltoall集合时延。先测既有通过版本，暂缓内存优化。正式性能运行关闭观测预加载、追踪与丢弃注入，记录协议、拓扑、绑核、预热和迭代参数，重复测量。尚未产生性能数据。

## 源码和构建
官方来源 https://mvapich.cse.ohio-state.edu/download/mvapich/osu-micro-benchmarks-7.5.2.tar.gz
节点DNS失败，用户电脑下载后上传。包SHA256为618de3d0b1122f73a9229177d2da1e5cd62e431190580cb915f2605849cbbbdc，仅固定本次包，未核对官方独立摘要。
资源根/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_resources_20261019。
源目录osu_source_7.5.2_20261020_hfJNs8/osu-micro-benchmarks-7.5.2。
成功构建目录osu_build_7.5.2_20261020_AOlmOB，日志configure.log/build.log/install.log。

节点用户返回configure、make -j4、make install成功，六项二进制均链接既有mpi_run_15c7293/install/lib/libmpi.so.40。未修改OSU源码。原运行基线五项哈希先前校验通过。node97部署及真实MPI运行待完成。

构建步骤：新建空build目录并进入；PATH前置现有HMPI/bin，LD_LIBRARY_PATH前置HMPI/lib及HUCX/lib；运行源目录configure，参数如下：
```sh
"$osu_src/configure" --prefix="$osu_build/install" \
  CC="$osu_mpi/bin/mpicc" CXX="$osu_mpi/bin/mpicxx" \
  CFLAGS="-O2" CXXFLAGS="-O2" > configure.log 2>&1
make -j4 > build.log 2>&1
make install > install.log 2>&1
```
可选GPU/PAPI参数全部省略，使用默认关闭。初次构建目录WDlBhi配置失败：7.5.2 configure.ac的AC_ARG_ENABLE(papi)在参数出现时无条件enable_papi=yes，因此--disable-papi反而触发PAPI链接；成功步骤省略该参数，不安装PAPI、不修改源码。MPI_Session_init探测no与当前MPI接口相符，不代表本轮失败。

## 六项SHA256
相对于install/libexec/osu-micro-benchmarks/mpi：
| 文件 | SHA256 |
| --- | --- |
| pt2pt/osu_latency | 8d84cf815f5a346bfd4893e352efe3b8fae302e5f1bbd7f3f99f0095e2755268 |
| pt2pt/osu_bw | dcb09f427e1e397f395e712527a62825b99a913c5084366902373300d6340ba5 |
| collective/osu_allreduce | 597d6a4150ed63312566e322391b9f41368a473f724a366183716944c529c2db |
| collective/osu_bcast | fbcd96a2963505922bc642b28e779309239931d0a1104225fbce51d7e9e09a4b |
| collective/osu_gatherv | 8d5c8f8546cd2f406076b6f0a55d530db70b931c59b9ed91ed6338e6cba90d2c |
| collective/osu_alltoall | 4d085c67a6f9fc2e8065f816cf2b1de15f9bbbd3dccff472fea9df3caefff4aa |

构建验收不等于MPI功能或性能验收。原始产物日志留节点。

## node97部署检查：通过

用户返回OSU_TRANSFER_CHECK=PASS，六项程序通过OSU_SHA256SUMS，所有ldd依赖可解析，libmpi均指向mpi_run_15c7293/install/lib/libmpi.so.40。节点安装目录与node89相同。仅完成传输完整性和依赖验证，尚未进行MPI冒烟或正式性能测量。

## OSU latency 冒烟及超时调查

初次命令遗漏既有通过runner的--mca pml_ucx_tls any与--mca pml_ucx_devices any，PML选择失败，未进入测试。补齐后64 B、10预热、100计时、-c -u 5的RM运行出现UCT send timeout并由外层终止（124）；RC因脚本遇错停止尚未执行。目录osu_latency_retry_20261020_t974wqbe，环境来源osu_latency_smoke_20261020_GT8ibz。这是未解决的重复通信问题，不能算性能验收。

随后单次诊断目录osu_diag_20261020__ims2853：两节点每节点1rank，均绑定socket0/core0。64 B、-x 0 -i 1 -c -u 0；RM开启TRACE/DIAG，日志info，RC为info。两者Validation Pass、退出0。输出RM17487.84 us，RC336.09 us，仅冷启动单次含诊断观测，禁止作为正式性能对比。

RM日志可见DATA/ACK发送及成功CQE。node89动态注册7次、注销1031次（含1024接收段清理），进度期间unreg_max约20ms，cleanup总统计最大约114ms；node97进度期间约51ms，含cleanup最大约125ms。初始化/清理及带追踪开销不能混同稳态时延；单次通过没有解释100次超时，尚未确认根因。

## 100次校验诊断：RM失败，RC通过

用户返回osu_repeat_diag_20261020_hk1h5oll。64 B、10预热、100计时、-c -u 5；原绑定每节点socket0/core0，RM DIAG=y TRACE=n，日志info。RM在若干收发之后再次出现Endpoint timeout，最终外层退出124，未输出最终Validation；普通MPI无端点错误回调，因此错误后继续等待，与已知能力边界一致。RC输出Validation Pass、退出0、2.52 us。诊断/校验单轮数据不作正式性能对比，也不能把RM超时记录为极高时延数值。

node89失败汇总rx257/tx251，reposts257、repost_errors0，reg254/unreg254，reg_max约60.5ms、unreg_max约73.0ms；cleanup/stop期间tail约13.24s。共享接口失败导致跨节点与本机端点同时报告错误，不能仅凭两个peer名称断言两条独立链路同时故障。未确定首个触发原因。下一步保持相同参数，仅取消core绑定对照，检查SDK辅助进度与绑核影响；不直接增大超时掩盖问题，不修改通过安装。

## 取消绑核对照：本轮两者通过

目录osu_unbound_diag_20261020_w668qn80，相比上一轮仅--bind-to core改为none，其余64 B、10预热、100计时、-c -u 5及诊断设置不变。用户返回RM/RC均Validation Pass、退出0；RM10894.61 us、RC6.05 us。这仍是带校验和诊断的单轮，不作为正式性能数据。取消绑核与本轮通过有关联，尚不能确认根因或保证后续稳定。

RM cleanup汇总：node89 reg1569/unreg2593、node97 reg1619/unreg2643（注销包含各1024接收段）；动态注册/注销显著频繁，进度中毫秒级调用仍存在。不能把汇总注册次数直接当作MPI消息数或计时区间操作数。下一步关闭校验、DIAG/TRACE及info日志，三轮交替测量64 B，保留所有退出状态，用于建立首个无诊断重复样本；不扩大范围直到确认稳定。

## 64 B 无诊断重复样本：六轮退出0，环境稳定性待确认

目录osu_latency64_repeat_20261020_00a15e9r。每节点1rank，bind-to none，64 B，10预热、100计时；不启用-c，无观测预加载，TRACE/DIAG关闭，日志warn，继承前述对齐队列及自动协议配置。顺序RM1、RC1、RC2、RM2、RM3、RC3。全部退出0，无此轮数据校验，不能称本轮Validation Pass。

| 轮次 | RM平均时延(us) | RC平均时延(us) |
| --- | ---: | ---: |
| 1 | 15863.14 | 651.16 |
| 2 | 11897.20 | 470.57 |
| 3 | 13062.46 | 2.71 |

RM范围约11.9–15.9ms，RC跨轮波动约240倍。三个数字分别为独立运行的OSU平均值，不是单次消息分布或尾时延。作为初步观测保留全部样本，不挑选最快值，也不宣称正式模式性能差距。当前数据不支持小包RM占优；应核查共享节点负载、调度和SDK注册注销热路径，再确定可复现测量条件。未修改超时或核心通信代码。

## 环境负载核查：正式性能测量暂停

2026-10-20用户返回两节点负载采样：每节点192逻辑CPU，node89 load average约212/211/175、node97约211/211/175；vmstat在首行历史均值之后的5个1秒样本，用户态CPU约100%、idle0%，运行队列约199–205。进程快照存在大量osu_alltoall，单进程CPU约97%。本轮助手提供的命令仅启动两rank osu_latency，尚未发起alltoall作业，需核对作业来源，禁止全局清理或终止归属不明进程。

该证据确认采样时CPU饱和，不能追溯证明所有前轮相同负载，亦不能把RM毫秒级注册开销或超时全部归因于它。先前六轮作为共享繁忙环境初步观测保留，不发布为可复现正式性能比较。待明确资源使用和空闲/隔离窗口后复测；已有功能/资源验证不会由此被改写为失败。

## 固定OSU runner节点准备：通过，未启动MPI

工具源提交0a68fa57f511b4a5b94891fea63a254cf2caa150。用户在node89返回上传包与run_osu_compare.py、test_osu_compare.py、OSU_RUNNER_GUIDE.md全部SHA256匹配；8项本地逻辑测试通过；gate计划6项检查通过（64 B、core绑定、预热20、计时100、重复3，顺序RM/RC、RC/RM、RM/RC）。PLAN_CHECK=PASS no_MPI_launched=1。

节点源目录/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_resources_20261019/osu_runner_source_wnkYLN/resource_osu_runner_0a68fa5。测试框架与参数生成准备通过，不等于ARM实际MPI或性能验收。等待用户协调的空闲窗口，复查负载和竞争通信作业后运行gate。时间到达本身不证明节点空闲。

## 空闲窗口64 B gate：六轮通过且跨轮稳定

用户先提供两节点实时vmstat后三行idle100%、r0、无现存OSU进程。历史load average仍较高但不代表实时忙碌。随后返回osu_gate_r76edpfm，工具0a68fa57f511b4a5b94891fea63a254cf2caa150。启动前命令要求两节点原五项运行基线SHA验证；本次回传片段为六轮运行结果，未包含前置哈希和preflight正文，不能声称本轮逐项回传核对。完整证据保存在节点。

每节点1rank，均socket0/core0，20预热、100计时、64 B，关闭-c和诊断/观测，warn日志，coll排除hcoll/ucc，其余见plan。六轮均RUN_STATUS PASS及ALL_OSU_RUN_CHECKS PASS（exit0且表格完整，不含数据校验）。
| 轮次 | RM us | RC us |
| --- | ---: | ---: |
| 1 | 917.81 | 2.31 |
| 2 | 926.82 | 2.29 |
| 3 | 933.04 | 2.30 |

独立运行均值的中位数RM926.82us、RC2.30us。当前实现/配置的64 B单对端RM明显较慢，不能支持小包RM占优。相同core绑定此次未超时，先前超时尚未确认为绑核本身；重载影响有证据但不是全部根因定论。不得用本轮反推其他包长、带宽或大量对端表现。下一步同配置有限校验，再扩展基准覆盖。当前不改通信代码。

## 空闲窗口64 B数据校验：RM/RC通过

目录osu_gate_hrabslfw。固定runner，core绑定，两节点各1rank；--validate，预热5、计时10、校验预热-u 5、重复1；RM889.62us、RC2.28us，均Validation Pass及RUN_STATUS PASS，总检查PASS。此轮用于正确性，不并入无校验性能三轮统计。空闲窗口core绑定重复通信目前通过，原重载超时记录仍保留。下一步原代码全消息范围短迭代探测，尚未完成带宽或集合性能验收。
