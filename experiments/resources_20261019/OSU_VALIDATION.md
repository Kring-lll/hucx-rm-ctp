# OSU 7.5.2 构建验收（2026-10-20）

目标：按导师要求对比RM/RC内存、osu_latency、osu_bw，以及osu_allreduce/osu_bcast/osu_gatherv/osu_alltoall集合时延。先测既有通过版本，暂缓内存优化。正式性能运行关闭观测预加载、追踪与丢弃注入，记录协议、拓扑、绑核、预热和迭代参数，重复测量。尚未产生性能数据。

## 源码和构建
官方来源 https://mvapich.cse.ohio-state.edu/download/mvapich/osu-micro-benchmarks-7.5.2.tar.gz
节点DNS失败，用户电脑下载后上传。包SHA256为618de3d0b1122f73a9229177d2da1e5cd62e431190580cb915f2605849cbbbdc，仅固定本次包，未核对官方独立摘要。
资源根/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_resources_20261019。
源目录osu_source_7.5.2_20261020_hfJNs8/osu-micro-benchmarks-7.5.2。
成功构建目录osu_build_7.5.2_20261020_AOlmOB，日志configure.log/build.log/install.log。

节点用户返回configure、make -j4、make install成功，六项二进制均链接既有mpi_run_15c7293/install/lib/libmpi.so.40。未修改OSU源码。原运行基线五项哈希先前校验通过。node97部署及真实MPI运行待完成。

构建步骤：新建空build目录并进入；PATH前置现有HMPI/bin，LD_LIBRARY_PATH前置HMPI/lib及HUCX/lib；运行源目录configure，参数如下：
```sh
"$osu_src/configure" --prefix="$osu_build/install" \
  CC="$osu_mpi/bin/mpicc" CXX="$osu_mpi/bin/mpicxx" \
  CFLAGS="-O2" CXXFLAGS="-O2" > configure.log 2>&1
make -j4 > build.log 2>&1
make install > install.log 2>&1
```
可选GPU/PAPI参数全部省略，使用默认关闭。初次构建目录WDlBhi配置失败：7.5.2 configure.ac的AC_ARG_ENABLE(papi)在参数出现时无条件enable_papi=yes，因此--disable-papi反而触发PAPI链接；成功步骤省略该参数，不安装PAPI、不修改源码。MPI_Session_init探测no与当前MPI接口相符，不代表本轮失败。

## 六项SHA256
相对于install/libexec/osu-micro-benchmarks/mpi：
| 文件 | SHA256 |
| --- | --- |
| pt2pt/osu_latency | 8d84cf815f5a346bfd4893e352efe3b8fae302e5f1bbd7f3f99f0095e2755268 |
| pt2pt/osu_bw | dcb09f427e1e397f395e712527a62825b99a913c5084366902373300d6340ba5 |
| collective/osu_allreduce | 597d6a4150ed63312566e322391b9f41368a473f724a366183716944c529c2db |
| collective/osu_bcast | fbcd96a2963505922bc642b28e779309239931d0a1104225fbce51d7e9e09a4b |
| collective/osu_gatherv | 8d5c8f8546cd2f406076b6f0a55d530db70b931c59b9ed91ed6338e6cba90d2c |
| collective/osu_alltoall | 4d085c67a6f9fc2e8065f816cf2b1de15f9bbbd3dccff472fea9df3caefff4aa |

构建验收不等于MPI功能或性能验收。原始产物日志留节点。

## node97部署检查：通过

用户返回OSU_TRANSFER_CHECK=PASS，六项程序通过OSU_SHA256SUMS，所有ldd依赖可解析，libmpi均指向mpi_run_15c7293/install/lib/libmpi.so.40。节点安装目录与node89相同。仅完成传输完整性和依赖验证，尚未进行MPI冒烟或正式性能测量。
