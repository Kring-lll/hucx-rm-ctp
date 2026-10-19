# 队列深度对齐配置验收（2026-10-19）

源码提交a06148f0a3bbe41c55a44ab44c8c43f22ad04e2e；节点run_star.py、check_star_logs.py、test_check_star_logs.py哈希均匹配，14项合成日志测试通过。HUCX/HMPI/观测库/星形程序未修改。

## RM 单对端：通过

用户返回formal_star_depth1024_priority5_rm_peers1_2m53oyay：MPI退出0、远端日志收集及完整检查通过、DEPTH_CONFIG_CHECK=PASS。检查每个rank成功创建API事件：Jetty发送深度256，JFR接收深度1024，两JFC深度256/1024。中心通信后JFC2、JFR1、Jetty1、target_jetty3，其余0。Finalize及退出配平检查通过。

优先级证据仅RM请求配置5、RC/UM源码常量5；当前观察器不记录优先级，priority_hardware_observation=NOT_RUN。深度是API创建参数，不是设备读回。RC对齐单对端已通过，对齐2对端已通过，4/8对端已通过，不宣称整组对照已完成。


## RC 单对端：通过

用户返回formal_star_depth1024_priority5_rc_peers1_qoyl64a4：MPI退出0，远端日志收集、完整日志及深度检查全部通过。每rank RC/UM成功创建事件的Jetty发送深度256、JFR接收深度1024，四JFC深度256和1024各两份。中心通信后JFC4、JFR2、Jetty3、JFCE2、target_jetty4、binding2，其余0。Finalize及退出配平检查通过。RM和RC单对端在API创建深度对齐后计数仍与既有趋势一致；2对端已补齐，4/8对端已通过。优先级硬件读回未验证。


## RM / RC 两个对端：均通过

用户返回RM目录formal_star_depth1024_priority5_rm_peers2_fvk8jake与RC目录formal_star_depth1024_priority5_rc_peers2_o2s1stbk。两轮MPI退出0、全部rank内容/放置/阶段快照/退出配平及创建深度检查通过，远端日志收集成功。RM中心JFC2、JFR1、Jetty1、target_jetty5，其余0；RC中心JFC4、JFR2、Jetty4、JFCE2、target_jetty6、binding3，其余0。API深度对齐后本地Jetty数量差异仍存在；不等于内存或性能收益已量化。优先级读回未验证。


## RM / RC 四个对端：均通过

用户返回RM目录formal_star_depth1024_priority5_rm_peers4_odq7i7vb、RC目录formal_star_depth1024_priority5_rc_peers4_colg4a1d。两轮MPI退出0，5个rank的内容、放置、各阶段快照、退出配平和API创建深度检查全部通过。RM中心JFC2、JFR1、Jetty1、target_jetty9，其余0；RC中心JFC4、JFR2、Jetty6、JFCE2、target_jetty10、binding5，其余0。优先级读回未验证。8对端已通过，核心通信库与二进制未改动。


## RM / RC 八个对端：均通过

用户返回RM目录formal_star_depth1024_priority5_rm_peers8_00xgeyly、RC目录formal_star_depth1024_priority5_rc_peers8_2ztynpq6。两轮MPI退出0，9个rank的内容、放置、阶段快照、退出配平与API创建深度检查全部通过。RM中心JFC2、JFR1、Jetty1、target_jetty17，其余0；RC中心JFC4、JFR2、Jetty10、JFCE2、target_jetty18、binding9，其余0。全部远端JSONL收集到中心。

## 完整对齐对照结论

|中心通信后存量|1对端|2对端|4对端|8对端|
|---|---:|---:|---:|---:|
|RM Jetty|1|1|1|1|
|RC+辅助UM Jetty|3|4|6|10|
|RM target_jetty|3|5|9|17|
|RC+辅助UM target_jetty|4|6|10|18|
|RC binding|2|3|5|9|

八组全部通过，覆盖38份进程日志。各组发送Jetty深度256、JFR深度1024；每iface发送/接收JFC为256/1024。深度通过每rank的真实创建API事件检查；不是硬件读回。RM请求优先级5，RC/UM源码优先级5，当前观测器未记录优先级。

在本轮两物理节点、最多9进程、64 B顺序通信范围内，深度对齐后RM中心本地Jetty保持1，而RC+辅助UM随对端增长，趋势与此前不同深度基线一致。对齐的是每个队列配置深度，不是每进程总容量；不宣称实际内存收益、硬件TP极限、并发压力稳定性或性能已验证。程序与核心库不变，仅运行工具/config profile更新。新一轮归档已完成。


## 自动运行与对齐实验增量归档：完成

用户返回ALIGNED_BACKUP_OK，依赖基线包与新归档SHA256校验均通过。

- 节点：node89，已包含脚本收集的node97日志。
- 路径：hmpi_rm_resources_20261019/aligned_pass_node89_20261019_oXZZ2X/aligned_pass_node89_20261019.tar.gz（实验根相对路径）。
- SHA256：fe651d093a813427dddcfb0e70fa943ff19a519e2503e1a9ee354fb5fbee434f。
- 依赖：resource_pass_node89_20261019_Lt61Ue/resource_pass_node89_20261019.tar.gz，SHA256=1a2184a4d06b4d0d315116b0b689ed76d1f83eb365aa4fb59d0e196327af101a；该基线再引用普通版完整备份，依赖不可省略。

包内包含新运行工具/源码包、配置查询、自动基线两组、深度对齐八组的中心及收集远端日志、run.json/summary.json和基线元数据。完成tar目录读取与SHA256检查，未做解包恢复演练。原始日志/二进制只留节点，公开仓库仅同步源码与必要验收摘要。本阶段完成。
