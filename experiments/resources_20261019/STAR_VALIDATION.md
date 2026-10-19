# MPI 中心多对端验收（2026-10-19）

源码提交1a80d06f3c37724c0b1132b99f79f3f26373b77d。节点返回源码与构建脚本SHA256均匹配；节点真实MPI编译成功。测试二进制SHA256=e8b351bea9a3eddd47a02e2067528f6464b00f0a1fad16f38d91abd56a78e03a，两节点传输校验通过，依赖新重建普通MPI。

## RM 一个对端：通过

用户返回：node89中心、node97一个对端，64 B顺序请求回复，两个STAR_CONTENT_PASS，MPI退出0。观测器二进制使用此前验证版本。RX_DEPTH=1024、TIMEOUT=100ms、RETRIES=8、RNDV_THRESH=inf、PROTO_ENABLE=n、无故障注入。

中心after_init：JFC live2、JFR live1、Jetty live1、target_jetty live1；after_exchange：前三项不变，target_jetty live3。其余类别0。after_finalize和process_exit八类live均0，获取/释放配平，valid=1，无失败。对端process_exit同样全部配平有效，JFC2、JFR1、Jetty1、target_jetty3。

该用例的1个对端验证完成，不等于多对端规模收益已经验证。RC星形用例、2/4/8个对端尚未运行。API导入引用不等于硬件连接；实际内存和性能未测量。原始节点日志留在节点。
