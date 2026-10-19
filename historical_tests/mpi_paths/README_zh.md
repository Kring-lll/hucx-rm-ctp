# MPI基本通信与底层路径验收 v1（2026-10-16）

用途：使用MPI自身的进程管理，验证点对点、广播、Gather与数据完整性。
不改HUCX、URMA驱动、系统MPI或此前通过的安装。

## 安装和第一轮

将整个ZIP上传到hmpi_rm_verify_20261016/upload；解压到该目录，保留包目录名。
仅node89运行 `bash 包目录/prepare_node89.sh`，新mpicc编译后同步独立测试二进制到node97。
仅node89运行 `bash 包目录/run_case.sh p2p 64 send`。
第一轮结果确认后再按助手指导测试READ/WRITE，不要一次运行完整矩阵。

## 用例

- p2p：两个rank，89/97各一；MPI_Sendrecv双向交换。
- bcast：三个rank，89一个、97两个；MPI_Bcast，root=0。
- gather：相同三个rank；MPI_Gather，root=0；root校验每个rank的数据槽位。
- 每轮逐字节检查，数据随来源rank、迭代和偏移变化；P2P额外检查接收长度、来源、TAG。
- 检查结果用MPI_Allreduce同步，再进入下一轮；这是MPI测试同步，不实现传输层流控。
- 所有用户缓冲区基地址4KiB对齐；MPI内部临时缓冲区的注册仍需实际设备验证。
- bytes支持1～8388608，iters支持1～10（默认1）；不是性能基准测试。

## 路径控制及证据边界

`run_case.sh MODE BYTES send|read|write [ITERS]`

- send：NATIVE_RMA=n、RNDV_THRESH=inf；用于小包/历史SEND基线。
- read：NATIVE_RMA=y、RNDV_THRESH=8192、RNDV_SCHEME=get_zcopy。
- write：相同原生RMA和阈值，RNDV_SCHEME=put_zcopy。
- READ/WRITE用例至少8192B；阈值和scheme只表达选择请求，不能代替实际opcode证据。
- 固定RX_DEPTH1024、TIMEOUT100ms、RETRIES8、DIAG=n、TRACE=y、PROTO_ENABLE=n、故障注入0。
- 点对点只允许rm_urma,self；集合通信增加sm供同节点两个rank通信，跨节点仍须看rm_urma。
- 控制握手/确认仍可使用SEND。大包不要求每条操作都是READ/WRITE。
- 脚本汇总实际RM_CTP_POST的SEND/READ/WRITE数量并展示前6条RMA；保存完整日志。
- MPI退出0、所有rank数据检查通过且观察到请求opcode，才返回PATH_CHECK=PASS。
- 若API成功但未观察到请求opcode，脚本返回2，不能宣称对应原生路径通过。
- 脚本只证明本轮观察到该opcode，不证明自动阈值选择、所有集合算法或性能达标。
- 180秒进程级运行上限；错误日志不自动重试，不重放失败的WRITE。

## 已有设备能力日志的正确筛选

`bash target_caps.sh 先前的native_rma_caps日志路径`

ucx_info -d会枚举多个设备；先前只取第一个rm_urma的筛选方式会误取其他网卡。
此脚本同时匹配Transport=rm_urma和Device=udmac0d1e2:1。

## 验证状态

交付时只完成本地shell语法和静态检查，运行环境没有mpicc，未做本地真实MPI编译/运行。
prepare_node89.sh使用节点mpicc加-Wall -Wextra -Werror编译，编译不通过即停止。
本包READ/WRITE、Bcast、Gather均待节点验证，不宣称已跑通。
