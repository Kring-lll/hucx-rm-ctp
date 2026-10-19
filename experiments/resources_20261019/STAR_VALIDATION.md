# MPI 中心多对端验收（2026-10-19）

源码提交1a80d06f3c37724c0b1132b99f79f3f26373b77d。节点返回源码与构建脚本SHA256均匹配；节点真实MPI编译成功。测试二进制SHA256=e8b351bea9a3eddd47a02e2067528f6464b00f0a1fad16f38d91abd56a78e03a，两节点传输校验通过，依赖新重建普通MPI。

## RM 一个对端：通过

用户返回：node89中心、node97一个对端，64 B顺序请求回复，两个STAR_CONTENT_PASS，MPI退出0。观测器二进制使用此前验证版本。RX_DEPTH=1024、TIMEOUT=100ms、RETRIES=8、RNDV_THRESH=inf、PROTO_ENABLE=n、无故障注入。

中心after_init：JFC live2、JFR live1、Jetty live1、target_jetty live1；after_exchange：前三项不变，target_jetty live3。其余类别0。after_finalize和process_exit八类live均0，获取/释放配平，valid=1，无失败。对端process_exit同样全部配平有效，JFC2、JFR1、Jetty1、target_jetty3。

该用例的1个对端验证完成，不等于多对端规模收益已经验证。RC星形1/2/4/8对端已全部运行。API导入引用不等于硬件连接；实际内存和性能未测量。原始节点日志留在节点。


## RM 两个对端：通过

同一源码和二进制，node89中心、node97两个对端。用户返回3个STAR_CONTENT_PASS，MPI退出0。中心after_init仍为JFC2、JFR1、Jetty1、target_jetty1；after_exchange前三项不变，target_jetty5。中心after_finalize和process_exit全部归零配平有效。两个对端process_exit各JFC2、JFR1、Jetty1、target_jetty4，全部配平、live0、valid1、失败计数0。远端进程中的额外导入可能涉及MPI内部通信，不能全部归为业务星形边。


## RM 四个对端：通过

用户返回5个STAR_CONTENT_PASS、MPI退出0。中心after_init：JFC2、JFR1、Jetty1、target_jetty1；after_exchange：JFC2、JFR1、Jetty1、target_jetty9。中心after_finalize及process_exit全部归零配平有效。四个对端process_exit各JFC2、JFR1、Jetty1、target_jetty6，其余0，全部live0、valid1、失败计数0。当前1/2/4对端中心本地队列数量保持不变，对端相关导入引用仍增长；不是总资源恒定或硬件内存收益证明。日志初始化阶段间隔增大，未作性能归因。


## RM 八个对端：通过

用户返回9个STAR_CONTENT_PASS、MPI退出0。中心after_init仍JFC2、JFR1、Jetty1、target_jetty1；after_exchange为JFC2、JFR1、Jetty1、target_jetty17。中心after_finalize/process_exit及八个对端process_exit均live0、valid1、失败0、获取释放配平。每个对端累计JFC2、JFR1、Jetty1、target_jetty10。

1/2/4/8个远端进程下，中心通信后本地Jetty/JFR/JFC保持1/1/2；远端导入引用分别3/5/9/17，符合本轮2N+1的观察关系，不代表总资源恒定。两物理节点、最多9个MPI进程、64 B顺序通信；RC星形1/2/4/8对端已运行，尚无统一队列配置、硬件TP/内存或性能收益结论。


## RC 一个对端：通过

相同星形程序，TLS=rc_urma,um_urma:aux,self。用户返回2个STAR_CONTENT_PASS、MPI退出0。中心after_init：JFC4、JFR2、Jetty2、JFCE2、target_jetty2、binding1；after_exchange：JFC4、JFR2、Jetty3、JFCE2、target_jetty4、binding2。JFS及target_jfr均0。中心after_finalize/process_exit及对端process_exit全部配平、live0、valid1、无失败。对端累计获取计数与中心通信后相同。包含辅助UM和自连接，不直接换算内存收益。


## RC 两个对端：通过

用户返回3个STAR_CONTENT_PASS、MPI退出0。中心after_init与RC单对端相同：JFC4、JFR2、Jetty2、JFCE2、target_jetty2、binding1；after_exchange为JFC4、JFR2、Jetty4、JFCE2、target_jetty6、binding3。中心after_finalize/process_exit及两对端process_exit全部live0、valid1、失败0、获取释放配平。两对端累计对象计数与中心通信后相同。中心Jetty较单对端增加1，RM同规模仍1。RC8对端已补齐验证，不外推硬件极限或内存收益。


## RC 四个对端：通过

用户返回5个STAR_CONTENT_PASS、MPI退出0。中心after_init仍JFC4、JFR2、Jetty2、JFCE2、target_jetty2、binding1；after_exchange为JFC4、JFR2、Jetty6、JFCE2、target_jetty10、binding5。JFS/target_jfr为0。中心after_finalize/process_exit及四个对端process_exit均全部配平、live0、valid1、失败0，各对端累计计数与中心通信后相同。RC中心Jetty在1/2/4对端为3/4/6，RM为1/1/1；这是本轮配置的API对象趋势，尚无硬件TP、实际内存或性能结论。


## RC 八个对端：通过

用户返回9个STAR_CONTENT_PASS、MPI退出0。中心after_init：JFC4、JFR2、Jetty2、JFCE2、target_jetty2、binding1；after_exchange：JFC4、JFR2、Jetty10、JFCE2、target_jetty18、binding9。其余两类0。中心after_finalize/process_exit及八个对端process_exit全部配平、live0、valid1、失败0。八对端各累计计数与中心通信后相同。

## 本轮完整对照

|中心after_exchange存量|1对端|2对端|4对端|8对端|
|---|---:|---:|---:|---:|
|RM Jetty|1|1|1|1|
|RC+辅助UM Jetty|3|4|6|10|
|RM JFC/JFR|2/1|2/1|2/1|2/1|
|RC+辅助UM JFC/JFR|4/2|4/2|4/2|4/2|
|RM target_jetty引用|3|5|9|17|
|RC+辅助UM target_jetty引用|4|6|10|18|
|RC binding|2|3|5|9|

范围：两物理节点、最多9个MPI进程、64 B顺序直接收发，每规模一轮，八组均数据通过并退出0，观察的退出快照均配平有效。RM中心本地队列在本轮范围不随对端数增长，RC+UM的Jetty与绑定增长。全部规模中心创建事件已按mode归因；完整日志机器复核已通过。队列深度未统一，无实际内存、硬件TP数量、最大连接规模、并发压力或性能结论。新增源码和构建脚本均来自固定仓库提交，无本轮核心库改动。


## 中心 Jetty 按模式归因：已核对

用户在node89读取八组完整中心JSONL并统计成功创建事件：RM的1/2/4/8对端分别均RM Jetty1；RC对应UM Jetty均1，RC Jetty分别2/3/5/9。因此辅助UM为固定开销，RC Jetty在本轮为N+1，RM Jetty为1。这里N是远端MPI进程数，非物理节点数。累计创建数与此前存量快照一致。自连接身份未由观测器逐个目标标注，不能将N+1中的固定1个按对象ID严格归因。


## 双节点完整日志机器校验：通过

用户返回两节点各8组LOG_CHECK=PASS及ALL_STAR_LOG_CHECKS=PASS。覆盖38份进程日志（中心8份、对端30份）。检查预期rank集合/文件数、单文件PID/rank一致、start记录、valid标志、连续序号、四阶段各八类快照、获取减释放等于存量、无获取/释放失败、Finalize后及退出时live/unique_live为0、末记录为退出快照。该检查不证明SDK内部路径完全覆盖或实际硬件成本。node97重复执行只重复只读校验，不增加MPI测试轮次。节点归档已完成。


## 节点增量归档：完成

用户返回两端RESOURCE_BACKUP_OK及归档SHA256校验通过；原普通版本五项运行文件哈希与观测库、星形测试二进制的固定哈希均通过。归档留在节点，未上传原始日志/二进制至公开仓库。

- node89：resource_pass_node89_20261019_Lt61Ue/resource_pass_node89_20261019.tar.gz；SHA256=1a2184a4d06b4d0d315116b0b689ed76d1f83eb365aa4fb59d0e196327af101a。
- node97：resource_pass_node97_20261019_K0rfI4/resource_pass_node97_20261019.tar.gz；SHA256=b694f92aff59ba00fc34f218a92778e2817a64c204543d6da71035be6f895bcb。

归档目录位于节点实验根hmpi_rm_resources_20261019之下。两个增量包分别依赖各节点此前rebuild_normal_pass完整备份，具体依赖路径及其归档哈希已写入包内BASELINE.txt和BASELINE_ARCHIVE_SHA256SUMS。包内含本轮源码/工具或运行文件、八组星形日志及初始观测日志。完成tar目录读取及SHA256检查，未进行解包恢复演练。本轮资源对象规模验证收尾完成；不据此宣称实际内存、硬件TP极限或性能已验证。
