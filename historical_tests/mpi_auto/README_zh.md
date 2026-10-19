# MPI自动选路检查（2026-10-16 v1）

本包仅新增独立运行脚本和日志摘要程序；复用已编译、已通过的MPI验收二进制。
不修改HUCX、MPI安装、原验收包、通过日志或已归档版本；不需要重编译，不向97安装脚本。

先在89解压到MPI实验目录的upload，再执行 `bash check_nodes.sh`。
该检查核对两端HUCX模块、UCP和验收二进制的既有哈希，确认仍是已通过版本。
只需89有Python3，摘要脚本只读取本地日志；不联网、不收集额外设备文件。

## 使用顺序

所有命令仅node89执行；每次一步，查看结果后再继续。

1. `bash run_auto.sh p2p 64`
2. 第一步确认后，`bash run_auto.sh p2p 1048576`
3. 结果确认后，8MiB以及Bcast/Gather的小/大包按需推进。

接口：`bash run_auto.sh p2p|bcast|gather BYTES [ITERS=1]`
bytes范围1～8MiB；iters范围1～10。180秒作业上限，失败不自动重试。

## 本阶段只让阈值和scheme自动选择

全部大小均使用NATIVE_RMA=y、RNDV_THRESH=auto、RNDV_SCHEME=auto。
RX_DEPTH仍1024、TIMEOUT仍100ms、RETRIES8、PROTO_ENABLE=n、DIAG=n、TRACE=y、注入0。
这不是“全部参数恢复默认”，不会同时恢复RX256/10ms，也不会切到PROTO_ENABLE=y。
小包和大包使用同一组选路配置，测试进程仍按大小独立启动，并非一次作业内混合大小消息。
auto只观察运行时选择，不能承诺必选READ或必选WRITE；两者之一可以满足大包RMA路径目标。

## 结果判定

- 先检查MPI退出、不同rank的数据校验、非零CQE以及跨节点rm_urma选择。
- <=4096B：期待SEND，且不出现RM READ/WRITE。
- >=1MiB：记录实际READ、WRITE或两者；若只有SEND且数据正确，标为大包自动RMA未证实，返回2。
  这与“数据收发失败”区分，后续需要核查自动阈值/协议选择。
- 中间大小仅记录实际路径，不预设转换点。
- 汇总SEND/READ/WRITE提交次数和RMA提交字节数，打印前6条RMA日志，完整日志单独保留。
- JOB_WALL_SECONDS_APPROX只是mpirun作业粗略总秒数，含启动/初始化/通信/退出，不是网络延迟或性能基准。
- 集合通信保留sm用于97内两个rank；跨节点业务应观察rm_urma。

## 已完成的交付前验证

shell语法检查、摘要程序模拟日志测试、包内哈希及ZIP完整性检查。
模拟测试不替代真实MPI/URMA运行；节点auto路径尚待执行。
此前受控READ/WRITE通过基线保留，可与auto观察结果对照。
