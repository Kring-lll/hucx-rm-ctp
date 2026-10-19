# 2026-10-19 全新构建与正常 MPI 验收

## 来源与证据

源码提交：`15c729309a9d554b06353760f40635218a98de2c`。
本记录依据用户在 node89/node97 执行命令后返回的输出整理；助手未直接访问节点。原始日志和安装归档保留在节点，不随本提交公开上传。

下载归档 SHA256：`fe447bb4567ec9492b1c9117ec21979de4efa10c08ba5d0b561f0216e838694f`。
载荷 9499 项、20 个符号链接校验通过。node89 从干净源码独立执行 HUCX 和原版 HMPI 的 autogen/configure/make/install，随后重新编译 MPI 测试程序。安装产物部署到 node97 并通过逐文件哈希检查；两端运行依赖指向新安装目录。

HUCX 1.15.0；HMPI 4.1.6rc4。HUCX 使用仓库 build_isolated.sh、4 个编译任务、CFLAGS/CXXFLAGS=-O2 -g、系统 URMA SDK。HMPI --with-ucx 指向新 HUCX，--without-ucg --disable-mpi-fortran --disable-oshmem --with-pic，内部 hwloc/libevent/PMIx。

## 正常功能结果

| 场景 | 64 B SEND / 1轮 | 1 MiB READ / 1轮 | 1 MiB WRITE / 1轮 | 8 MiB READ / 5轮 | 8 MiB WRITE / 5轮 |
|---|---|---|---|---|---|
| P2P | PASS（auto 配置） | PASS | PASS | PASS | PASS |
| Broadcast | PASS | PASS | PASS | PASS | PASS |
| Gather | PASS | PASS | PASS | PASS | PASS |

每项所有参与 rank 均返回数据校验 PASS，MPI 退出码 0；路径日志观察到相应 SEND/READ/WRITE。8 MiB 大包用例每项观察到 80 次 1 MiB READ 或 WRITE。P2P64 最初 smoke 使用 native=y、阈值和方案 auto，不计作强制 SEND 配置测试。

## 自动选择

三个场景的 64 B 各 1 轮观察到 SEND_ONLY，8 MiB 各 5 轮观察到 READ；另有 P2P 1 MiB 自动 READ 1 轮通过。P2P64 复用最初同配置 smoke 结果，没有再次重跑。auto WRITE 未观察到。

通过运行配置：RX_DEPTH=1024、TIMEOUT=100ms、RETRIES=8、PROTO_ENABLE=n、DIAG=n、TRACE=y、DROP_DATA=0、DROP_ACK=0。集合通信允许同节点共享内存；跨节点日志确认 rm_urma。

仅两台物理节点，最多 3 个 MPI rank。自动选择不代表使用源码全部默认参数。

## 本轮节点改动

HUCX/HMPI 的手写通信源码未修改。Autotools 生成文件和编译产物在独立目录创建。
新增的 run_mpi_case.sh 和 run_mpi_auto.sh 适配新安装、程序和 summarizer 路径；对应内容同步到 experiments/rebuild_20261019/。它们由已下发替换命令准确重建，仍待用户回传节点脚本 SHA256 进行最终字节核对。

## 节点归档

用户返回两份归档均完成 tar 可读性和 SHA256 检查；未进行恢复演练。

| 节点 | 归档名 | SHA256 |
|---|---|---|
| node89 | rebuild_normal_pass_node89_20261019.tar.gz | 1d1da8ab0a68c5855e252aeb676f0d4d54a4b9e9be9199b7bfa3bb0b898581c5 |
| node97 | rebuild_normal_pass_node97_20261019.tar.gz | aa4ce7b3a078117eb421db77e9b1bb6cb1e009c23ef3e2318c348147aae70f6c |

## 尚未完成

独立 fail-fast 变体在新构建环境中的重建及验收；参数化通用构建部署入口；MPI 请求恢复、更多物理节点扩展、资源收益和正式性能测试。既有 fail-fast 历史结果不能替代本轮重建验收。
