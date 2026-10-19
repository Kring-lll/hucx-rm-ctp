# 2026-10-19 重建验收脚本

本目录同步当天在 node89 新增、修改并运行通过的两个 runner。它们根据历史 runner 和对用户提供的精确替换命令重建，未改动 HUCX/HMPI 通信源码。用户已回传 node89 文件 SHA256，两个脚本均与仓库逐字节一致。

- `run_mpi_case.sh`：强制 SEND / get_zcopy / put_zcopy。
- `run_mpi_auto.sh`：native=y，threshold=auto，scheme=auto。

这些脚本保留已验收实验的绝对安装路径、节点配置和测试程序位置，当前是实验环境专用脚本，不宣称任意目录可直接运行。auto runner 使用固定来源树中的 summarize_auto.py。

节点执行位置是实验根目录下的同名脚本；仓库文件用于保留其准确内容。不要直接覆盖原始 historical_tests/ 中的历史版本。

已验收源码提交及测试范围见 ../../docs/REBUILD_ACCEPTANCE_20261019.md。

## fail-fast 入口

`run_mpi_failfast.sh` 适配新的独立 MPI 安装目录，沿用原故障注入和结果判定。baseline 64 B、单次 DATA 丢弃恢复和重传耗尽主动终止均验收通过。节点 SHA256 已确认与仓库一致；大消息和集合回归仍待完成。
