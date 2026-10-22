# 进程内存取样原型（待节点验收）

新建独立mpi_memory_star程序、proc_memory.h、build_memory_star.sh、run_memory_star.py及内存日志校验/自检。原mpi_resource_star、run_star.py、观测库和通信库均不修改。MPI星形通信保持64 B顺序模式、1/2/4/8对端；默认沿用depth1024_priority5。

取样阶段：before_init、after_init、after_exchange、after_finalize。所有进程自身读取/proc/self/status与/proc/self/smaps_rollup，权限不足/字段缺失不默默当零。观察器先做一次memory_warmup快照，读取器也先预热一次；使用不缓冲stdout，避免首次样本之后才分配stdio输出缓冲。采样本身和观测器会占内存，结果是包含观测成本的进程值，不是无扰动精确通信库成本。

主要指标：RSS、PSS、Private_Clean+Private_Dirty（private_sum，不含另列的HugeTLB私有页）；单位KiB。VmSize是虚拟地址空间，VmHWM是生命周期高水位，不代表当前驻留；VmLck/VmPin只能作为辅助字段，不能当作完整设备注册或固定内存统计。缺少非必需状态字段时为-1。不读取映射地址或其他进程数据。

关注中心after_exchange原始值及exchange_minus_init增量，也保留初始化增量。中心快照时对端尚未Finalize，但各rank采样不严格同步，PSS可能随共享页关系和后台线程变化。不能将所有RSS直接相加当唯一物理内存，更不能据此断言设备/驱动总资源。RC辅助UM、自连接、MPI初始化缓存和SDK开销均在测量中。

先真实单对端验收RM/RC，再逐级运行。正式比较采用相同源码/配置，交错传输顺序并重复采样，报告原始值及波动；单次结果只算探索，出现负增量不自动判错。第一轮不测性能，不改驱动，不重建HUCX/HMPI。

本地：procfs解析及本机读取自检通过；内存校验6项合成日志测试通过；既有资源校验14项通过；MPI源只用替代头文件严格语法检查，未链接真实MPI。真实节点编译、MPI运行及任何内存收益结论均未验证。新二进制编译后以SHA256校验两节点一致，原二进制继续保留。
