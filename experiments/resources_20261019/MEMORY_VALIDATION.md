# 进程内存取样验收（2026-10-19）

源码提交2f23611bb5c40cc5c9310cd8a8eac087fb415055。节点7项源码/脚本哈希匹配；内存日志6项、资源日志14项测试通过，节点procfs读取自检通过。真实MPI编译成功，二进制SHA256=c64686be3579503111f5ff62f80844b5a0202d7b830c6ac5ec06346e03f3d732；node89/node97部署哈希一致，依赖新重建普通MPI。原有二进制及通信库未修改。

## RM 单对端：取样流程通过

用户返回memory_star_depth1024_priority5_rm_peers1_c33w3yx7：MPI退出0，内容/放置/资源快照/创建深度/退出配平和四阶段内存样本全部通过。中心JFC2、JFR1、Jetty1、target_jetty3，其余0。

中心KiB增量：init_minus_preinit RSS89984、PSS87863、private_sum86256；exchange_minus_init RSS148、PSS148、private_sum148。仅单次探索，包含MPI/SDK、建链、缓存及观测开销，不等于Jetty专属成本或RM节省量。RC对应样本、多规模重复取样和归档均待完成。PSS等为各rank本地阶段样本，不严格同时；优先级硬件读回、驱动/设备总内存和性能均未验证。
