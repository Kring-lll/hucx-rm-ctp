# 独立 fail-fast 重建验收（计划验收项及双节点归档完成）

证据来自用户返回的节点执行输出，助手未直接访问节点。原始运行日志保留在节点。

## 已完成

从固定源码提交 `15c729309a9d554b06353760f40635218a98de2c` 的 HMPI 和两个 fail-fast 覆盖文件生成独立源码副本。PML/header SHA256 与仓库一致；独立执行 autogen/configure/make/install，链接本轮新 HUCX。正常版安装哈希前后保持不变。

安装产物部署到第二节点，归档及两库哈希通过，两端依赖指向新的 fail-fast MPI 和新 HUCX。

| 产物 | SHA256 |
|---|---|
| mca_pml_ucx.so | 25d43bd463319a748697876d15572626397f3c55ae6266fb8da0c5823038e4bb |
| libmpi.so.40 | bd9b97928de7c83a68b579fc42777a1b179ff79f4f9a011dde0a0f6ec5f08563 |
| 部署归档 | 713c1bdfcda93453e8957cd5c753d05390708dfadc2b453a5448108a6731112f |

基线 P2P64、1轮、auto、FAIL_FAST=1：两个 rank 注册错误回调并完成内容校验，MPI 退出码0；SEND=8、READ=0、WRITE=0；DATA/ACK 丢弃均0；FUNCTION_CHECK=PASS。

新增实验 runner 同步为 `experiments/rebuild_20261019/run_mpi_failfast.sh`，保留已执行实验路径和配置。沿用 historical_tests/mpi_failfast/summarize.py，未改故障判定逻辑。它根据已下发的精确替换命令重建，用户回传节点 SHA256 为 `cc30bef7be6e8f93025639073b50d6d837265812a80609b5aa956542afebdfed`，与仓库一致，字节核对通过。

## 故障注入验收

- data1：仅接收端丢弃1次 DATA，同一 kind=1 操作提交2次；两个rank内容校验PASS、退出0，FUNCTION_CHECK=PASS。SEND=9、READ/WRITE=0，未触发作业终止。
- exhaust：接收端 DATA 丢弃预算64，实际观察到18次丢弃；两个 kind=1 操作各提交9次（首次+8次重试）。出现 UCT send timeout 和 HMPI_RM_FAIL_FAST 延迟终止标识；MPI退出码86，作业约21秒，NEGATIVE_CHECK=PASS。未依赖180秒外层超时。

以上为接收侧软件丢弃注入，不是物理链路丢包测试。

## 正常回归及脚本同步

用户检查负向测试的两个已知进程PID，均已不存在；仅确认这两个测试进程退出，不宣称全系统无任何残留。

| 回归 | 大小 | 轮数 | 结果 | RMA提交数 |
|---|---|---:|---|---:|
| P2P READ | 8 MiB | 5 | 两rank校验通过，退出0 | READ80 |
| P2P WRITE | 8 MiB | 5 | 两rank校验通过，退出0 | WRITE80 |
| Broadcast READ | 8 MiB | 5 | 三rank校验通过，退出0 | READ80 |
| Gather WRITE | 8 MiB | 5 | 三rank校验通过，退出0 | WRITE80 |

上述正常回归均无丢弃注入，启用FAIL_FAST=1，观察到回调注册和RM跨节点路径。集合通信检查器返回COLLECTIVE_REGRESSION=PASS。

新增集合入口同步为 `experiments/rebuild_20261019/run_mpi_failfast_collective.py`，保留原结果判定，仅适配新目录及二进制清单检查。用户回传SHA256与仓库一致：`6fdde93922516b25294c8c7dc1d8f8f5a4515ed3555a28c529c725494062cc00`。两个fail-fast入口均完成节点字节核对。

本轮计划验收项通过，用户随后完成两台节点增量归档和校验。不是fail-fast所有场景完整覆盖；未测集体通信故障、请求恢复、多线程或正式性能。

## 待完成

通用参数化构建部署入口，以及已明确未覆盖的故障/恢复、资源及性能验证。

开关默认关闭，测试显式 HUCX_MPI_RM_FAIL_FAST=1；语义是终止整个作业，不是请求恢复或继续运行。

## 最终增量归档

两节点用户输出均显示运行库哈希不变、tar归档可读和归档SHA256校验通过。归档留在节点，没有上传公开仓库。每份增量包需配合对应节点的普通版本全量备份使用；没有执行恢复演练。

| 节点 | 增量归档名 | SHA256 |
|---|---|---|
| node89 | failfast_rebuild_pass_node89_20261019.tar.gz | f82f8b99c289dc6cae7a1270aaa1e9eabd70829d4b985a682b672645e7b22773 |
| node97 | failfast_rebuild_pass_node97_20261019.tar.gz | ee9f8ff45db2645598deeb4ba531b35af892a6e4598abcf114f8b9cab5a970f0 |
