# MPI 中心多对端验收（2026-10-19）

源码提交1a80d06f3c37724c0b1132b99f79f3f26373b77d。节点返回源码与构建脚本SHA256均匹配；节点真实MPI编译成功。测试二进制SHA256=e8b351bea9a3eddd47a02e2067528f6464b00f0a1fad16f38d91abd56a78e03a，两节点传输校验通过，依赖新重建普通MPI。

## RM 一个对端：通过

用户返回：node89中心、node97一个对端，64 B顺序请求回复，两个STAR_CONTENT_PASS，MPI退出0。观测器二进制使用此前验证版本。RX_DEPTH=1024、TIMEOUT=100ms、RETRIES=8、RNDV_THRESH=inf、PROTO_ENABLE=n、无故障注入。

中心after_init：JFC live2、JFR live1、Jetty live1、target_jetty live1；after_exchange：前三项不变，target_jetty live3。其余类别0。after_finalize和process_exit八类live均0，获取/释放配平，valid=1，无失败。对端process_exit同样全部配平有效，JFC2、JFR1、Jetty1、target_jetty3。

该用例的1个对端验证完成，不等于多对端规模收益已经验证。RC星形2/4/8对端尚未运行。API导入引用不等于硬件连接；实际内存和性能未测量。原始节点日志留在节点。


## RM 两个对端：通过

同一源码和二进制，node89中心、node97两个对端。用户返回3个STAR_CONTENT_PASS，MPI退出0。中心after_init仍为JFC2、JFR1、Jetty1、target_jetty1；after_exchange前三项不变，target_jetty5。中心after_finalize和process_exit全部归零配平有效。两个对端process_exit各JFC2、JFR1、Jetty1、target_jetty4，全部配平、live0、valid1、失败计数0。远端进程中的额外导入可能涉及MPI内部通信，不能全部归为业务星形边。


## RM 四个对端：通过

用户返回5个STAR_CONTENT_PASS、MPI退出0。中心after_init：JFC2、JFR1、Jetty1、target_jetty1；after_exchange：JFC2、JFR1、Jetty1、target_jetty9。中心after_finalize及process_exit全部归零配平有效。四个对端process_exit各JFC2、JFR1、Jetty1、target_jetty6，其余0，全部live0、valid1、失败计数0。当前1/2/4对端中心本地队列数量保持不变，对端相关导入引用仍增长；不是总资源恒定或硬件内存收益证明。日志初始化阶段间隔增大，未作性能归因。


## RM 八个对端：通过

用户返回9个STAR_CONTENT_PASS、MPI退出0。中心after_init仍JFC2、JFR1、Jetty1、target_jetty1；after_exchange为JFC2、JFR1、Jetty1、target_jetty17。中心after_finalize/process_exit及八个对端process_exit均live0、valid1、失败0、获取释放配平。每个对端累计JFC2、JFR1、Jetty1、target_jetty10。

1/2/4/8个远端进程下，中心通信后本地Jetty/JFR/JFC保持1/1/2；远端导入引用分别3/5/9/17，符合本轮2N+1的观察关系，不代表总资源恒定。两物理节点、最多9个MPI进程、64 B顺序通信；RC星形仅1对端已运行，尚无统一队列配置、硬件TP/内存或性能收益结论。


## RC 一个对端：通过

相同星形程序，TLS=rc_urma,um_urma:aux,self。用户返回2个STAR_CONTENT_PASS、MPI退出0。中心after_init：JFC4、JFR2、Jetty2、JFCE2、target_jetty2、binding1；after_exchange：JFC4、JFR2、Jetty3、JFCE2、target_jetty4、binding2。JFS及target_jfr均0。中心after_finalize/process_exit及对端process_exit全部配平、live0、valid1、无失败。对端累计获取计数与中心通信后相同。包含辅助UM和自连接，不直接换算内存收益。
