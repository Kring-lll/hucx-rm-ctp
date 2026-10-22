# 节点观测验证（2026-10-19）

观测器源提交：d886fc31feb09ee5f4af6a5556b2646a99df9562。

## SDK 编译与模拟自检

依据用户从 node89 返回的输出：五个源码及脚本 SHA256 匹配；节点 URMA SDK 编译成功；调用转发、errno 保留和八类资源计数模拟自检通过。模拟后端未执行硬件操作。

## 真实 RM MPI 小消息观测

依据用户返回的双节点终端输出：普通 MPI / HUCX 15c7293 重建版本，两个物理节点、两个 rank，p2p 64 B 一轮，UCX_TLS=rm_urma,self；RX_DEPTH=1024，TIMEOUT=100ms，RETRIES=8，RNDV_THRESH=inf，PROTO_ENABLE=n，无故障注入。两个 rank 数据校验通过，MPI_EXIT_CODE=0。

观测库在两节点校验一致，SHA256=0b129d8a0e8bccd5b3690a03d0e9fb77737d7d9f7f378c7b095e8c662e7411dd。仅测试进程设置 LD_PRELOAD，未加载模拟后端。

每个 rank 的退出快照均为：JFC acquired/released=2/2、peak=2；JFR=1/1、peak=1；Jetty=1/1、peak=1；target_jetty=3/3、peak=3；其余四类为零。八类均 live=0、unique_live=0、valid=1，acquire_failed/release_failed=0。每个进程日志为23行。远端导入计数表示 API 获取引用次数，不等于三个硬件连接，也不能直接视为三个业务对端。隐含在 Jetty 内部的 JFS 不单独计数。

本轮证明真实进程资源 API 调用能够被观测并在退出时配平；尚未完整逐事件独立复核，也未证明所有潜在调用路径均被覆盖。RC 实测、多对端规模比较、硬件 TP 数、实际内存成本及性能收益均待验证。原始日志留在节点。


## 真实 RC + 辅助 UM MPI 小消息观测

用户返回：相同重建 MPI/HUCX，双节点双 rank，p2p 64 B 一轮，UCX_TLS=rc_urma,um_urma:aux,self，RNDV_THRESH=inf，PROTO_ENABLE=n。两个 rank 数据校验通过，退出码0。每个进程43行观测记录。

两个 rank 的退出快照一致：JFC acquired/released=4/4、peak=4；JFR=2/2、peak=2；Jetty=3/3、peak=3；JFCE=2/2、peak=2；target_jetty=4/4、peak=4；binding=2/2、peak=2；JFS与target_jfr均0。八类全部live=0、unique_live=0、valid=1，无获取或释放失败。

本轮计数包含 RC 与辅助 UM，并包含运行时自连接等开销，尚未按传输模式逐事件归因。对照 RM 的 Jetty=1，本轮 RC+UM 的 Jetty=3仅是此配置下的 API 对象计数，不可推导实际内存节约比例或大规模扩展收益。RC与RM队列配置尚未统一。跨物理节点的RC日志仍显示intra-node命名；本轮未修改或解决其标志问题。
