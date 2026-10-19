# 公开源码导出范围

本次公开提交包含完整 HUCX/HMPI 手写源码、构建源、测试源码及补丁，保留上游许可证和版权声明。

不公开原候选包中以下节点历史材料：

- `hucx/BASELINE_CHANGES.patch`
- `hucx/SOURCE_MANIFEST.json`
- `hucx/NODE_ACCEPTANCE.md`
- `hucx/RECOVERY.md`
- `hucx/TEST_LOGS.zip`
- `hucx/TEST_REPORT.md`
- `hucx/TEST_RESULTS.json`

其中历史日志 ZIP 的公开上传被自动审批审查拒绝，原因是节点构建、验收和运行日志可能包含内部路径、环境与测试信息。该归档没有通过其他形式重新上传。其余关联历史报告和清单一并排除。

`patches/hucx_current.patch` 已重新生成为不包含上述材料的源码补丁；SOURCE_MAP.json 和 PAYLOAD_SHA256.json 对应本公开源码树。原候选包及其哈希保留为外部历史核对依据，公开树不是原候选包的逐文件复制。代码与链接目标保持一致。
