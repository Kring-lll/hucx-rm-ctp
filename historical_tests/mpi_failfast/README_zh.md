# HMPI RM端点错误通知实验补丁 v1（2026-10-16）

## 目标与依据

原MPI重试耗尽测试：RM按100ms初始超时、8次重试在约15.9003秒后报告Endpoint timeout，
UCP提示没有安装端点错误回调；MPI没有自行结束，最终被180秒外层timeout终止，退出124。
节点当前pml_ucx.c仅给UCP端点设置远端地址和PEER_HOST_ADDR，没有注册ERR_HANDLER。

本补丁的目标是**实验性失败即终止作业（fail-fast）**：接收端点错误，明确诊断，
在UCX进度调用返回后使用当前MPI已有的ompi_mpi_abort接口结束作业。
它不实现MPI请求错误返回、MPI容错恢复、自动重连或失败后继续运行。
不能把这个实验补丁写成MPI_ERRORS_RETURN全语义已实现。

## 实现范围

- 复制node89现有、已经autogen的HMPI源码到一个全新mpi_failfast_run_XXXXXX/source。
- 只在新源码副本修改pml_ucx.c并增加pml_ucx_rm_failfast.h。
- 为现有UCP端点增加可选ERR_HANDLER，保留远端地址、vpid和peer_hostname。
- 不设置UCP_ERR_HANDLING_MODE_PEER；现有RM未承诺完整PEER错误处理语义。
- 开关是HUCX_MPI_RM_FAIL_FAST=1，仅精确字符串1开启，默认关闭。
- 回调通过GCC原子操作发布首次错误，不调用MPI、关闭端点、释放请求或打印日志。
- 覆盖已核实的3处直接ucp_worker_progress调用，包括普通进度函数和两个等待循环。
- 进度调用返回后记录HMPI_RM_FAIL_FAST并调用ompi_mpi_abort(world,86)。
  若该内部终止函数意外返回，_exit(86)为防御兜底；这不是优雅MPI_Finalize。
- 回调上下文编码peer vpid，不依赖可能提前释放的proc指针。
- 补丁器拒绝进度调用数不是3、已有错误策略/USER_DATA或不符合节点地址设置的源码。
- 在新build、新install完整编译MPI，仍链接原HUCX，禁用UCG/Fortran/OSHMEM。
- node97只安装相同绝对路径的新MPI运行产物；不编译其旧HUCX源码。
- 原源码、旧MPI安装、HUCX安装、旧测试程序及正常测试脚本均不覆盖。

原MPI前缀：
`/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016/run_e5R0dm/install`

## 文件

- patch_source.py：只对新源码副本打补丁，并在新run目录输出rm_failfast.patch。
- pml_ucx_rm_failfast.h：错误记录和延迟终止实现。
- prepare_node89.sh：两节点原版本校验、新源码副本/构建/安装、传输新MPI与验证。
- install_remote.sh：node97独立运行安装。
- check_original.sh：检查当前已通过HUCX/验收程序及原MPI；来自已有auto检查器。
- run_case.sh：仅在node89发起新MPI的P2P验收，保持原参数并显式打开实验开关。
- summarize.py：区分正常通过和预期失败通过；拒绝只凭非零退出判通过。
- LOCAL_VALIDATION.md：本地检查和未验证部分。
- SHA256SUMS：包内文件校验。

## 部署

上传ZIP到：
`/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016/upload/`

在node89解压全新包目录，先按交付消息核对ZIP SHA256，再运行prepare_node89.sh。
该脚本会完整编译一套独立MPI，使用make -j4；可能需要数分钟。
它打印RUN路径及configure/build日志位置；失败打印末尾日志并停止。
失败的新run目录保留，不覆盖旧产物。成功才生成包目录内runtime.json。
runtime.json存在时拒绝重复准备，避免无意重新构建或替换已选实验运行环境。
没有自动执行故障测试。

成功标志：BOTH_NODES_FAILFAST_PREPARE_OK。
新MPI仍显示4.1.6rc4；区分版本应看新前缀、PML组件哈希、ARMED日志与补丁记录，
不能只看版本字符串。

## 分步测试，先正常再失败

1. `bash run_case.sh baseline`：64B/1轮/auto，无注入，开关1。
2. 第1项通过后再`bash run_case.sh data1`：node97丢第一个有效DATA。
3. `bash run_case.sh exhaust`：node97 DATA预算64、8次重试、100ms初始ACK超时。
4. 负向退出通过后再回归大包：
   `bash run_case.sh baseline 8388608 5 read`
   `bash run_case.sh baseline 8388608 5 write`

ACK观察可用`bash run_case.sh ack1`，但丢1个ACK可能被后续累计ACK覆盖。
没有重发/重复识别时，仅记ACK丢失后通信成功，不记重发分支通过。
所有故障用例初始限制为P2P64B/1轮/auto；不扩大节点或改变重试/超时参数。
注入针对每iface收到的前N个有效RM DATA/ACK，可能承载UCX控制消息。
不能直接称为“已建链业务数据包的定点注入”。

正常用例要求：实际RM传输、回调注册证据、两个不同rank内容通过、MPI退出0、
无设备非零CQE，且实际SEND/READ/WRITE符合请求。

耗尽用例要求：实际DATA丢弃、同进程DATA操作9次POST、RM超时、
HMPI_RM_FAIL_FAST status=Endpoint timeout且callback_deferred=1、MPI非零退出、
无应用PASS、无“未装回调”提示、无设备CQE错误；124/137外层结束一律不通过。
日志保留真实MPI_EXIT_CODE；脚本自身仅在负向证据齐备时返回0并输出NEGATIVE_CHECK=PASS。
预计单操作耗尽约15.9秒，加启动和退出；具体退出行为必须由节点验证。
不能把MPI运行时结束其他rank称为远端收到RM失败通告或MPI请求已返回错误。

## 回退与保留

本包不切换系统默认MPI、不改shell启动文件，不替换原安装；脚本环境只在子进程有效。
停止使用本包run_case.sh，继续使用此前run_auto.sh/run_case.sh即使用原MPI前缀。
保留新run/source/build/install、日志、rm_failfast.patch、原文件哈希及runtime.json便于审查。
不必删除实验目录来回退，不批量杀进程或重置设备。

## 当前未验证的内容

本地没有真实MPI/URMA环境；尚未在节点编译本实验MPI或验证终止接口行为。
先正常通信、再重试耗尽；之后按需要回归集合通信再考虑是否正式集成。
未验证多线程MPI、进程故障恢复、全部挂起请求错误完成、更多物理节点或性能。
开关默认关闭表示不启用新的失败终止策略，不表示二进制完全相同或没有测量开销。

## 公开参考（节点源码仍为实际依据）

- UCP错误回调说明：https://openucx.github.io/ucx/api/latest/html/group___u_c_p___c_o_m_m.html
- MPI错误处理边界：https://docs.open-mpi.org/en/v5.0.x/man-openmpi/man3/MPI_Errors.3.html
- Open MPI 4.1系列启动器说明：https://www.open-mpi.org/doc/v4.1/man1/mpirun.1.php

上述网页为接口/语义参考，不表示节点版本已升级或所有参考行为已经通过节点测试。
