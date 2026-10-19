> 此文为原候选包说明的历史记录。公开仓库范围和当前状态以 README.md、docs/PUBLIC_EXPORT.md 为准；历史节点日志与关联材料未纳入公开树。

# RM+CTP 完整源码候选基线（2026-10-19 v1）

**状态：用于与 node89 快照核对；尚未在节点从零重建或验收。**

本包含完整 HUCX、完整原版 HMPI、精确的独立 MPI fail-fast 两文件覆盖层、
可回放补丁和历史测试源码。它不是节点归档的直接复制，也不是已完成验收的新发布版本。
当前阶段只进行源码核对；不要在现有可用安装上覆盖构建。

## 目录

| 路径 | 内容 |
|---|---|
| `hucx/` | 以公开 HUCX 固定基线为基础的当前 RM+CTP 候选源码，含原 v1 历史元数据 |
| `hmpi/` | 公开 HMPI 固定提交的完整原版源码 |
| `variants/hmpi_failfast/` | 两个文件的覆盖层；其他文件继承 `hmpi/`，不是独立完整目录 |
| `patches/hucx_current.patch` | 从固定公开 HUCX 基线到本包 hucx/ 的完整差异，含历史元数据 |
| `patches/hmpi_failfast.patch` | 原版 HMPI 到独立 fail-fast 版的精确补丁 |
| `historical_tests/` | 上周的测试源码和原 runner；保留旧实验绝对路径，仅作证据和后续迁移输入 |
| `versions.lock.json` | 版本、关键哈希、通过配置及未验证范围 |
| `SOURCE_MAP.json` | 三个逻辑源码视图的普通文件哈希和符号链接目标 |
| `tools/compare_rm_source_archives_20261019.py` | 直接读取归档的核对工具；外部也提供同一脚本 |

## 固定上游

- HUCX：`https://github.com/kunpengcompute/hucx`
  提交 `43f61747585c67afd4e70e059232f38a7e9bfdab`。
- HMPI：`https://github.com/kunpengcompute/hmpi`
  提交 `52d943345a85efc10de2d59311eb4bd4fa94444e`。
- HMPI 原始节点 ZIP SHA256：
  `5ffb83b6b09fcb84de9f4ce144d9224c882d82ac7a93787d842343f22a05ab39`。
  该 ZIP 注释给出上述 HMPI 提交；公开 PML 文件与节点原版哈希一致。
- SDK 配套是节点观测到的 `umdk-urma-25.12.0-B131.oe2403sp3` 系列包。
  SDK/驱动/固件、设备配置及安装二进制不包含在本包中。
  历史 BASELINES.json 中的 UMDK commit 是开发参考，不能替代节点 RPM 配套版本。

## 已完成的本地核对

1. 32 个 HUCX 关键文件与用户本地 agent 返回的 node89 审计哈希精确一致，
   覆盖全部 8 个 RM .c/.h、相关 base/RC/UM/UCT/UCP 文件和全部 15 个 contrib/rm_ctp 文件。
2. 原 MPI PML 文件 SHA256：
   `13e8188a678359fa121cdae591cdfdd187e2d1f01317b11664307581e601a2e9`。
3. fail-fast 新 PML SHA256：
   `923f7f8fca3c3e21b6785255ba31eebedf532a56bf2c066e46f1b428f49ddcaf`。
4. fail-fast header SHA256：
   `33945159d2e2387c2959328eda15e2a2ee6951e734b26c613436c7ab9ea2483e`。
5. fail-fast patch SHA256：
   `8ec72e14cc3527e812b9d67a76cc0cf2f6648e5731cf1417c7ca9041cda164dd`。
6. 将 HUCX 总补丁应用到重新下载的固定公开基线，回放后的文件内容和链接目标
   与本包 hucx/ 完整一致。保留已有空白格式；没有为清理样式而改动节点对应源码。
7. 归档比较工具通过小型构造归档验证：相同内容、内容修改、节点额外文件、
   清单损坏、归档哈希不符、符号链接与硬链接；无需解压或创建链接。

这些是本地源码/工具验证，不是硬件测试、完整节点源码一致性或重建成功的证明。

## 与 node89 快照核对

node89 快照名：`node89_sources_20261019.tar.gz`，SHA256：
`1d62cb1dfb5d8575a6bd4bb67af49510c96067e235018699f8aa331cd7787492`。

用单独下载的比较脚本读取两个 tar.gz，不必在 Windows 解压本包。
脚本仅读取输入归档，将完整差异写入一个新的输出目录；不联网、不执行源码。
它会检查节点普通文件清单和链接清单，并分别比较：

1. 本包 hucx/ 与节点当前 HUCX 源码。
2. 本包 hmpi/ 与节点原 HMPI 源码。
3. hmpi/ 加 fail-fast 覆盖层，与节点独立 HMPI 源码。

退出码 0：三棵树的内容、路径和权限均一致。
退出码 2：核对完成，存在差异，需要分析 comparison.json；不是工具执行故障。
退出码 1：输入、完整性或格式错误。

节点经历 Autotools 生成步骤，可能有额外生成文件或公共辅助文件更新。
工具不会自动忽略这些差异；必须分类后再决定是否接受。
任何手写 .c/.h/脚本差异都需逐项确认，不能以“都是生成文件”一概排除。

## 上周里程碑与后续验收范围

- 普通 MPI：p2p、bcast、gather；64 B SEND 各 1 轮；
  1 MiB READ/WRITE 各 1 轮；8 MiB READ/WRITE 各 5 轮。
- 自动选择：三个场景 64 B SEND 各 1 轮、8 MiB READ 各 5 轮；另有 p2p 1 MiB READ 1 轮。
- 独立 fail-fast：P2P64 正常、单 DATA 丢弃恢复、持续丢弃后终止作业；
  P2P8MiB READ/WRITE各5、Bcast8MiB READ5、Gather8MiB WRITE5 正常回归。
- fail-fast 默认关闭，需 `HUCX_MPI_RM_FAIL_FAST=1`。
  成功的负向用例应明确报错并结束作业，参考退出码86；不是请求恢复或继续运行。
- 通过配置：RX=1024、TIMEOUT=100ms、RETRIES=8、PROTO_ENABLE=n、DIAG=n。
  TRACE=y 用于路径取证；这些是运行参数，源码默认 RX 仍为256、TIMEOUT仍为10ms。
- 仅两台物理节点，集合用例最多3个rank；未证明资源收益、更多节点扩展性或性能优势。

本包未新增可移植的统一构建/部署/测试入口。先完成源码差异核对，随后再将历史 runner
改为可配置路径、主机和设备，并在全新节点目录构建验收。
历史 hucx/TEST_REPORT.md、TEST_RESULTS.json 等保持原样，代表初版记录，不代表本候选包已验收。

## 本轮只读审计的两项纠正

- UCT 可达性入口通过 internal_ops 分派，RC 可直接指定 URMA 回调；
  不能假定一定经过通用 DEVICE-scope memcmp。
- UCT 使用函数指针，并不代表回调内部调用的 URMA 导出函数无法进行符号拦截；
  是否可用需核对目标动态符号/重定位并实测，不能预先要求必须在 UCT ops 层插桩。

尚未将本包发布到外部 Git 托管，也未修改或替换节点当前可用源码和安装。
