# 单次运行与自动汇总（待节点验收）

新增 run_star.py、check_star_logs.py、test_check_star_logs.py。观测器及MPI程序不变。源码应从固定提交解压；本轮只替换运行工具，不覆盖已通过安装。

示例：

```bash
python3 run_star.py rm 1 --stage /绝对路径/资源实验目录 --runtime /绝对路径/重建运行目录
```

运行节点为中心；默认node89/node97和既有已验收二进制路径。可通过参数调整两节点地址、名称及根目录。当前支持1/2/4/8对端、baseline_20261019与depth1024_priority5配置，固定二进制哈希，非任意MPI版本通用工具。

程序逐次校验本地/远端二进制及本地原运行基线哈希；隔离继承的UCX变量，记录配置。只在应用进程加载观测器；每次新建独立目录，保留原始MPI日志和run.json，收集远端日志到remote_logs，检查数据通过标记、rank放置、各阶段快照及退出配平，输出summary.json。失败不清理证据，也不会自动扩大规模。退出码非零或日志收集失败不算通过。程序只测对象数，不将MPI整体时间当作性能结果。

本地验证：Python语法检查、RM/RC dry-run配置输出、10项合成日志测试。模拟日志不验证真实MPI运行；节点运行与跨节点收集待验收。

## 下一组配置准备

源码确认RC/UM共用URMA配置表有TX_DEPTH=256、RX_DEPTH=4096；RC/UM各有TX_JFC_LEN=4096。嵌套配置名称应以节点ucx_info -f -c输出为准，不在确认前猜测变量名。RC和UM Jetty创建代码priority=UB_URMA_MAX_PRIORITY（当前宏值5），没有本轮已确认的可配置优先级；RM PRIORITY可配置，今天为1。

节点配置清单已确认，新profile已实现，真实运行待验收。即便数值相同，RC每端点发送队列和RM共享发送队列的总容量也不同，这正是需要记录的结构差异。辅助UM资源单列；不能把全部队列容量数值当实际内存字节。

## depth1024_priority5 配置（待节点验收）

节点ucx_info确认RC/UM的TX_DEPTH、RX_DEPTH、TX_JFC_LEN变量名存在。新profile通过参数--profile depth1024_priority5启用；默认仍为既有baseline。RM设置发送256、接收1024、优先级5；RC/UM分别设置发送256、接收1024、发送JFC256。RM发送/接收JFC由TX/RX深度创建，无独立TX_JFC_LEN配置。

校验器必须在每个rank的真实成功创建事件中确认Jetty发送深度256、JFR接收深度1024，以及RM两JFC深度256/1024、RC+UM四JFC深度256/1024各两份。配置未生效直接失败。此处depth是URMA创建API配置参数，不是硬件读回查询。优先级未被当前观测器记录，只有RM请求值和RC/UM源码常量证据，输出明确标记priority_hardware_observation=NOT_RUN。

本地新增4项深度配置检查fixture，总14项日志测试通过；RM/RC新profile的dry-run通过。未在节点运行新profile。不改HUCX/HMPI/观测库/测试程序。原baseline单对端RM和RC真实自动运行均已通过，其他规模自动运行待验收。
