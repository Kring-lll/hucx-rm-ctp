# 本地验证范围

没有真实MPI、URMA或节点访问权限，不宣称节点编译或负向退出已修复。

已执行：

1. 对节点实际输出所对应结构的最小PML文本样本，检查3个进度入口全部替换、
   新回调参数加入、原peer_hostname保留且没有开启PEER错误模式。
   已补丁、调用数变化、未知地址字段、已有错误策略的样本拒绝。共5类。
2. GCC -std=gnu99 -O2 -Wall -Wextra -Werror -pthread真实编译并运行交付的原始头文件，
   只把MPI/UCX外部符号替换为测试桩，不是完整MPI编译。
   检查开关未设置/0/1/yes/11共5例。
3. 正常progress返回值、从worker内部触发错误后等其返回才终止、首次错误保留、
   peer0空值编码、8线程争用首次错误共5例；错误例退出86且仅调用一次终止桩。
   终止桩若发现仍处于worker函数中会退出99，所有例均未发生。
   线程例仅检查原子记录辅助逻辑，不代表MPI_THREAD_MULTIPLE已验收。
4. 15个摘要样本，覆盖正常/少量DATA/ACK/耗尽，以及强杀/信号退出、超过重试上限、无回调、无注入、
   无9次重发、重复rank冒充双rank、设备CQE错误等拒绝路径。
5. 所有Shell脚本bash -n、Python语法检查。

实际输出：

```
PATCH_GUARDS_PASS cases=5
REAL_C_HELPER_PASS switch_cases=5 progress_cases=5 including_deferred_abort_and_first_error
SUMMARY_FIXTURES_PASS cases=15
SHELL_AND_PYTHON_SYNTAX_PASS
```

未执行：完整节点HMPI编译链接、真正ompi_mpi_abort运行、双节点新组件加载、
真实错误回调及资源回收、正常/负向MPI用例、大包和集合回归、性能测量。
prepare_node89.sh与install_remote.sh只完成静态语法与人工流程检查，
没有用假编译器模拟完整安装传输流程。
