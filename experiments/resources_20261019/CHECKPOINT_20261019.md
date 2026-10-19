# 2026-10-19 停工检查点

用户决定今天停止，明天继续。最后验收结果提交5114727cf5f0b2cc550f9b900d02424e04539f5b，工作分支resource-observer-20261019。保持既有可用安装，不覆盖，不在缺少节点结果时宣称验收完成。每轮给用户1至2个命令块，用户执行并回传。遵守根AGENTS.md，节点新增/修改代码验收后立即同步。

## 已完成
- 原HUCX/HMPI重建基线及独立fail-fast验收、归档已完成，未由本轮诊断修改核心通信实现。
- RM/RC星形资源对照：1/2/4/8远端进程，2物理节点最多9 MPI进程；默认及depth1024_priority5对齐配置均通过内容/资源/释放检查。RM中心RM Jetty始终1；RC中心RC Jetty为2/3/5/9，另固定UM Jetty1。不是硬件TP上限或性能测试。
- 内存程序三次RM、三次RC单对端通过。before_init至after_exchange的private_sum增量中位数RM86404 KiB、RC47864 KiB，当前单对端RM未表现内存节省。不能只比较exchange增量。
- 注册诊断库真实SDK构建、mock自检及双节点部署通过；RM、RC真实单对端运行全部检查PASS，MPI退出0。
- RM中心1027次注册/注销，1024个4096字节及88/97/160字节各1，总请求4194649字节。抽样注册通常RSS+68 KiB，注销-64 KiB。
- RC+UM中心4次注册/注销，6 MiB两次、10 MiB两次，总32 MiB；注册窗口RSS增加与段长度一致，注销窗口RSS不变。
- 结果支持优先研究RM大量独立小段注册开销，尚未确认SDK内部机制，不应将64 KiB视为普遍固定注册成本。

## 明天从这里继续
1. 先归档本轮内存与注册诊断源码、构建、二进制哈希、六次原内存样本、两次注册诊断及收集的远端日志。新归档引用已有aligned/resource/full基线；核验tar可读与SHA256。本轮新材料尚未归档。
2. 再审查RM接收缓冲分配、注册、post_recv、销毁及失败清理路径，准备独立版本：连续分配接收区，保持每槽4 KiB对齐，整段注册；核实SDK与SGE使用要求后实施。不覆盖旧安装。
3. 源码先提交固定实验版本，再独立构建部署。验证收发、软件丢弃重传、大消息READ/WRITE、MPI及资源注销配平，复测内存。合并注册优化目前未实现、未验证。
4. 优化正确性和内存验证后再测性能；带注册诊断日志的作业不作为性能基准。

## 节点路径
统一根目录：/home/y00993710/urma_rm_ctp_lab/experiments/
资源目录hmpi_rm_resources_20261019；运行基线hucx_rm_repo_rebuild_20261019。
内存源码：memory_source_FuwWj3/hucx-rm-ctp-2f23611bb5c40cc5c9310cd8a8eac087fb415055/experiments/resources_20261019。
内存构建两节点：memory_build_2f23611/mpi_memory_star；SHA256 c64686be3579503111f5ff62f80844b5a0202d7b830c6ac5ec06346e03f3d732。
注册诊断库：node89 segment_build_e170fcb、node97 segment_runtime_e170fcb/liburma_segment_observer.so；SHA256 7ee2ea19d04a9bc8174ca7ad4bbf55528fd908ff7973f836cfc323b27e9ab4f1。
注册runner：segment_runner_source_2SaAvR/hucx-rm-ctp-03554bfab26c8af1e7a05b3b91c4cf6d373cac5c/experiments/resources_20261019/run_segment_star.py。
真实诊断：segment_star_depth1024_priority5_rm_peers1_9mynizfg、segment_star_depth1024_priority5_rc_peers1_nwqm27zg。

六次内存case前缀memory_star_depth1024_priority5_：
rm_peers1_c33w3yx7、rc_peers1_3pjieaoq、rc_peers1_5q7ywcyv、rm_peers1_w8gxo9dg、rm_peers1_e33iqtsy、rc_peers1_7_w118oz。

已有最近归档：aligned_pass_node89_20261019_oXZZ2X/aligned_pass_node89_20261019.tar.gz，SHA256 fe651d093a813427dddcfb0e70fa943ff19a519e2503e1a9ee354fb5fbee434f。
完整历史、资源归档依赖、详细结果见ALIGNED_VALIDATION.md、STAR_VALIDATION.md、MEMORY_VALIDATION.md、SEGMENT_VALIDATION.md。原始节点日志和二进制保留节点，公开仓库只保存必要结构化摘要。

本检查点不创建提醒，不执行后续节点操作。
