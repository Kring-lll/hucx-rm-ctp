# 独立 fail-fast 重建验收（进行中）

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

新增实验 runner 同步为 `experiments/rebuild_20261019/run_mpi_failfast.sh`，保留已执行实验路径和配置。沿用 historical_tests/mpi_failfast/summarize.py，未改故障判定逻辑。它根据已下发的精确替换命令重建，节点脚本字节核对待用户回传 SHA256。

## 待完成

单次 DATA 丢弃恢复、重传耗尽主动终止、正常大消息和代表性集合通信回归；节点 runner SHA256 确认与最终归档。本轮尚不宣称 fail-fast 完整验收通过。

开关默认关闭，测试显式 HUCX_MPI_RM_FAIL_FAST=1；语义是终止整个作业，不是请求恢复或继续运行。
