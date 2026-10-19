# MPI RC/RM 资源对照：观测工具原型

**状态：实验原型；仅本地替身接口和模拟后端自检通过。尚未在节点真实 SDK 编译，尚未验证真实 MPI 拦截或进行资源比较。**

此目录不修改 HUCX、HMPI、URMA SDK 或驱动。只对指定测试进程使用
LD_PRELOAD；不要把它加入全局环境，也不要加入系统动态加载配置。

## 观测依据与范围

用户返回的 node89 ELF 输出显示，本轮 HUCX 的 URMA 创建、释放、导入和绑定接口
存在 R_AARCH64_JUMP_SLOT，liburma.so.0 导出相应符号。动态拦截因此具备可测试条件，
但实际运行是否完整拦截仍须通过 MPI 自检确认。

记录本进程经过这些导出 API 成功创建/导入的 JFC、JFR、JFS、Jetty、JFCE、
target Jetty/JFR，以及成功绑定和解绑的调用。记录本地对象ID、上下文指针、模式、
配置队列深度、累计成功/失败次数、存活引用和峰值；快照另外记录不同指针的存活数。
同一导入指针如多次返回，以成功导入引用计数，不能直接解释成多个物理对象。

不统计发包数据路径，不拦截内存注册，不计算内存字节或硬件 TP 数量。
Jetty 隐含的内部队列不能与显式 JFS 调用重复相加。SDK 内部、内联或其他未经过
这些导出接口的路径不在已证明覆盖范围内。不能把这些计数称为设备全量资源。

日志每个进程一个独占新文件，存放在预先创建的 RM_RESOURCE_OBSERVER_DIR；
文件包括PID和MPI rank（环境中不存在时为-1）。只有带完整开始/结束快照且
valid=1的日志才可用于分析。对象表满、未知释放或本地对象重复创建会使统计无效；
日志或符号解析失败以125退出。固定表容量16384项，不代表设备能力上限。
fork后的统计、信号处理器中使用、跨线程真实SDK调用尚未验证。

## 第一步：节点SDK编译和无硬件自检

```bash
bash build_probe.sh /ABSOLUTE/NEW_OUTPUT_DIRECTORY
```

默认头文件目录为 `/usr/include/ub/umdk/urma`，可用 URMA_OBSERVER_INCLUDE 指定。
编译器直接检查包装函数与节点头文件的签名。输出目录必须不存在。

自检程序只链接 `unit/liburma_observer_mock.so`，模拟对象创建/释放、失败调用和绑定。
它不调用真实设备。库不命名为liburma.so，不安装，不用于MPI或性能测试。
自检检查8类对象的存活/峰值/归零、失败释放保留存活计数、errno及返回值转发。

期望：

- OBSERVER_FORWARDING_SELFTEST=PASS
- OBSERVER_ACCOUNTING_SELFTEST=PASS
- OBSERVER_BUILD_AND_MOCK_SELFTEST_OK=...
- REAL_MPI_INTERPOSITION_AND_RESOURCE_COMPARISON=NOT_RUN

模拟自检不能替代真实SDK、MPI和资源释放验收。本地曾使用最小替身头文件验证计数逻辑，
该头文件不随此目录交付，不能用于节点ABI或设备结论。

## 后续步骤

1. 单个真实MPI对端：验证实际资源事件、数据正确性、正常释放，先比较有/无观测器。
2. 同一MPI中心进程与多个对端直接通信，逐步2/4/8对端，保留通信前/后阶段快照。
3. 对照RC/RM；RC辅助UM建链对象、MPI初始化临时资源、配置队列深度和优先级单列。
4. 另行将本进程资源ID与设备查询相对照；硬件TP及共享关系不可由API计数直接推断。

当前尚未交付或验证上述MPI多对端用例，不宣称RM资源节省或扩展收益。
节点验收通过后立即同步状态、准确文件和SHA256，原始节点日志留在节点。
