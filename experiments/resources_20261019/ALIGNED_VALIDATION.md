# 队列深度对齐配置验收（2026-10-19）

源码提交a06148f0a3bbe41c55a44ab44c8c43f22ad04e2e；节点run_star.py、check_star_logs.py、test_check_star_logs.py哈希均匹配，14项合成日志测试通过。HUCX/HMPI/观测库/星形程序未修改。

## RM 单对端：通过

用户返回formal_star_depth1024_priority5_rm_peers1_2m53oyay：MPI退出0、远端日志收集及完整检查通过、DEPTH_CONFIG_CHECK=PASS。检查每个rank成功创建API事件：Jetty发送深度256，JFR接收深度1024，两JFC深度256/1024。中心通信后JFC2、JFR1、Jetty1、target_jetty3，其余0。Finalize及退出配平检查通过。

优先级证据仅RM请求配置5、RC/UM源码常量5；当前观察器不记录优先级，priority_hardware_observation=NOT_RUN。深度是API创建参数，不是设备读回。RC对齐单对端及对齐多对端尚未运行，不宣称整组对照已完成。
