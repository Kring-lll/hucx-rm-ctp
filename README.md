# HUCX RM+CTP / HMPI 源码基线

本仓库保存 RM+CTP 在 HUCX 中的实现、HMPI 对接补丁和 MPI 验证源码。

**当前状态：2026-10-19 已完成源码对齐，并根据用户返回的节点输出完成固定提交的全新构建及双节点正常 MPI 验收。独立 fail-fast 已重建部署，正常小消息、单次丢弃恢复、耗尽主动退出、大消息及代表性集合回归均通过；已完成两节点增量归档与校验。**

## 内容

| 目录 | 用途 |
|---|---|
| `hucx/` | 完整 HUCX 源码及 RM+CTP 改动 |
| `hmpi/` | 完整原版 HMPI 固定源码 |
| `variants/hmpi_failfast/` | 独立 MPI fail-fast 变体的两个覆盖文件 |
| `patches/` | HUCX 总补丁及 HMPI fail-fast 补丁 |
| `historical_tests/` | 已验证实验的测试源码与历史脚本，仍含旧实验路径 |
| `tools/` | 直接读取归档的源码核对工具 |
| `docs/SOURCE_ALIGNMENT_20261019.md` | 本次源码核对结果、证据来源与未验证范围 |

## 上游来源

- [kunpengcompute/hucx](https://github.com/kunpengcompute/hucx)，固定提交 `43f61747585c67afd4e70e059232f38a7e9bfdab`。
- [kunpengcompute/hmpi](https://github.com/kunpengcompute/hmpi)，固定提交 `52d943345a85efc10de2d59311eb4bd4fa94444e`。
- 上游及各第三方组件的许可证、版权声明保留在原目录。根目录不另行指定覆盖所有组件的统一许可证。

版本、通过配置和关键文件哈希见 `versions.lock.json`。URMA SDK、驱动、固件和安装二进制不包含在仓库内；参考环境为 ARM64 openEuler 和 `umdk-urma-25.12.0-B131.oe2403sp3` 配套。

## 源码对齐与历史功能

三份逻辑源码视图的重叠文件内容、类型和权限均匹配 node89 快照。节点额外文件经用户本地 agent 分类为 Autotools 生成文件，详见核对记录。

上周在节点通过的功能包括：MPI 点对点、广播、聚合；64 B SEND；1 MiB 和 8 MiB READ/WRITE；自动选择小消息 SEND、大消息 READ。代表性 8 MiB 场景完成 5 轮数据校验。独立 fail-fast 实验版本可在重传耗尽后终止整个作业，默认关闭，未实现请求恢复。

历史版本结果与本轮重建验收分别记录。固定提交 15c7293 已由用户从仓库下载，在节点全新构建并完成上述正常功能与自动选择测试，见 `docs/REBUILD_ACCEPTANCE_20261019.md`。独立 fail-fast 本轮计划验收项已通过，详细范围见 `docs/FAILFAST_REBUILD_ACCEPTANCE_20261019.md`。仅两台物理节点、最多 3 个 MPI 进程，尚未量化资源收益或性能优势。

## 下载与后续构建

可使用 Git 克隆或 GitHub 下载源码归档。Linux 构建需要正确保留符号链接和可执行权限；Windows 上无需通过资源管理器解压后再上传。

HUCX 和 HMPI 是干净源码形态，需要各自执行 Autotools 生成步骤。`variants/hmpi_failfast/` 必须覆盖到 `hmpi/` 的独立副本，不能作为完整 MPI 直接构建。当前历史 runner 含实验目录，不是通用的一键安装入口。

节点专用的已验收 runner 已同步到 `experiments/rebuild_20261019/`。下一步完善通用参数化入口，并开展 MPI RC/RM 资源对照。不要覆盖现有可用节点安装。

## 清单与历史材料

`SOURCE_MAP.json` 描述导入的源码视图；`PAYLOAD_SHA256.json` 是初次导入提交 `15c7293` 的载荷清单（不包含清单自身），用于核验该固定归档；后续新增文件和元数据修订由 Git 提交跟踪，不宣称此历史清单覆盖最新 HEAD。`README_zh.md` 和 `LOCAL_VALIDATION.txt` 保留打包时的原始记录，其中 `NOT_RUN` 状态应结合本次 `docs/SOURCE_ALIGNMENT_20261019.md` 阅读。

节点历史日志和关联材料已排除，具体路径见 `docs/PUBLIC_EXPORT.md`；HUCX 源码补丁已重新生成并完成回放验证。
