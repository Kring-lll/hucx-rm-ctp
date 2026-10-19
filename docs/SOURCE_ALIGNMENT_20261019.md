# node89 源码对齐记录（2026-10-19）

## 证据来源

本记录依据用户返回的本地 agent 只读核对报告整理。仓库维护助手没有直接访问 node89，也没有在本地取得原始节点归档或 comparison.json；以下全树核对结果由用户本地执行返回。

- 候选包：`hucx_rm_ctp_source_candidate_20261019_v1.tar.gz`
- SHA256：`f39baf00fc4231a2bd36d2e94cb69faf2dde8eb077a91a807d1b659baa6faea9`
- 节点快照：`node89_sources_20261019.tar.gz`
- SHA256：`1d62cb1dfb5d8575a6bd4bb67af49510c96067e235018699f8aa331cd7787492`
- 比较工具 SHA256：`5d6717c497d2886d4ca7b5bcccb49f691e1b913d38f3875ce256beb3214bda82`

## 返回结果

完整性通过；SOURCE_SHA256SUMS 18897 条通过，SYMLINKS.txt 40 条通过。

| 视图 | 匹配 | 内容或类型差异 | 候选有而节点缺失 | 节点独有 | 权限差异 |
|---|---:|---:|---:|---:|---:|
| HUCX | 1189 | 0 | 0 | 61 | 0 |
| HMPI 原版 | 8271 | 0 | 0 | 568 | 0 |
| HMPI + fail-fast 覆盖层 | 8272 | 0 | 0 | 568 | 0 |

工具退出码 2（REVIEW_REQUIRED）：比较完成，需要审阅节点独有项，不是完整性失败，也不是逐字节相同归档。

用户本地 agent 将节点独有项归类为 Autotools 生成文件，包括 Makefile.in、configure、aclocal.m4、autom4te.cache、libtool 辅助文件及生成头文件；报告未发现手写源码遗漏。HMPI 两个视图的节点独有集合一致。后续如归档原始 comparison.json，应保留逐路径分类，不按扩展名泛化忽略。

## 本次仓库导入验证

仓库中的导入载荷应按原 PAYLOAD_SHA256.json 校验，源码视图按 SOURCE_MAP.json 校验。Git 保存符号链接目标和可执行位。公开导出排除了节点历史日志及关联材料，见 PUBLIC_EXPORT.md；当前仓库清单已重新生成，源码补丁回放通过。

## 边界

- 本轮接受干净源码与节点构建生成文件的差异。
- 原版 HMPI 与 fail-fast 变体分别维护；覆盖层只有两个文件，构建时应生成独立副本。
- 原候选包及其 LOCAL_VALIDATION.txt 保留打包时状态；本记录补充之后收到的全树核对结论。
- 从零编译、运行库重建及新的节点 MPI 验收尚未完成。
- 历史测试脚本需要路径、主机和设备参数化。
- SDK、驱动、固件和当前节点安装产物未纳入源码树对齐。
