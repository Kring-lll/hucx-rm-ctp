# 队列深度对齐配置验收（2026-10-19）

源码提交a06148f0a3bbe41c55a44ab44c8c43f22ad04e2e；节点run_star.py、check_star_logs.py、test_check_star_logs.py哈希均匹配，14项合成日志测试通过。HUCX/HMPI/观测库/星形程序未修改。

## RM 单对端：通过

用户返回formal_star_depth1024_priority5_rm_peers1_2m53oyay：MPI退出0、远端日志收集及完整检查通过、DEPTH_CONFIG_CHECK=PASS。检查每个rank成功创建API事件：Jetty发送深度256，JFR接收深度1024，两JFC深度256/1024。中心通信后JFC2、JFR1、Jetty1、target_jetty3，其余0。Finalize及退出配平检查通过。

优先级证据仅RM请求配置5、RC/UM源码常量5；当前观察器不记录优先级，priority_hardware_observation=NOT_RUN。深度是API创建参数，不是设备读回。RC对齐单对端已通过，对齐2对端已通过，4对端已通过，8对端待验证，不宣称整组对照已完成。


## RC 单对端：通过

用户返回formal_star_depth1024_priority5_rc_peers1_qoyl64a4：MPI退出0，远端日志收集、完整日志及深度检查全部通过。每rank RC/UM成功创建事件的Jetty发送深度256、JFR接收深度1024，四JFC深度256和1024各两份。中心通信后JFC4、JFR2、Jetty3、JFCE2、target_jetty4、binding2，其余0。Finalize及退出配平检查通过。RM和RC单对端在API创建深度对齐后计数仍与既有趋势一致；2对端已补齐，4对端已通过，8对端待验证。优先级硬件读回未验证。


## RM / RC 两个对端：均通过

用户返回RM目录formal_star_depth1024_priority5_rm_peers2_fvk8jake与RC目录formal_star_depth1024_priority5_rc_peers2_o2s1stbk。两轮MPI退出0、全部rank内容/放置/阶段快照/退出配平及创建深度检查通过，远端日志收集成功。RM中心JFC2、JFR1、Jetty1、target_jetty5，其余0；RC中心JFC4、JFR2、Jetty4、JFCE2、target_jetty6、binding3，其余0。API深度对齐后本地Jetty数量差异仍存在；不等于内存或性能收益已量化。优先级读回未验证。


## RM / RC 四个对端：均通过

用户返回RM目录formal_star_depth1024_priority5_rm_peers4_odq7i7vb、RC目录formal_star_depth1024_priority5_rc_peers4_colg4a1d。两轮MPI退出0，5个rank的内容、放置、各阶段快照、退出配平和API创建深度检查全部通过。RM中心JFC2、JFR1、Jetty1、target_jetty9，其余0；RC中心JFC4、JFR2、Jetty6、JFCE2、target_jetty10、binding5，其余0。优先级读回未验证。8对端待运行，核心通信库与二进制未改动。
