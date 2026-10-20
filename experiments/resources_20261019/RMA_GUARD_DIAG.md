# RMA guard diagnostic experiment — not a fix

Prepared 2026-10-20 after the 256→512 KiB OSU transition reproduced MPI_ERR_INTERN (17), while isolated 512 KiB repeated READ completed. See OSU_VALIDATION.md. No SDK/node build acceptance yet; local checks cover Python syntax, build-plan safety and diff whitespace only, not C compilation or MPI.

Changes: RM RMA invalid-parameter exits now log iov_shape, local_range, bcopy_size or remote_or_size. The latter reports requested length, device max, remote length, identity-match booleans and relative offset, never raw addresses. Import failure logs errno and remote length, preserving errno across logging before existing cleanup. Existing checks/return statuses/posting behavior are retained. UB rkey cache hit with different length or attrs now warns UB_RKEY_CACHE_MISMATCH; it still retains the existing cached entry and increments the reference exactly as before. These logs diagnose, not repair. Cache warnings can affect RC too because base MD is shared; use only independent diagnostic install.

Overlay files are the complete two modified C files. Builder verifies pinned baseline hashes and overlay hashes, copies original source with generated configure to a unique directory, replaces only those files, and uses original configure options with a new prefix. No make install into the baseline. Plain invocation checks hashes only; --execute builds. Requires node89 SDK, gcc/g++, make and existing source. No MPI launched. Upon successful build, record hash then deploy the new independent installation to node97; use new LD_LIBRARY_PATH/UCX_MODULE_DIR with existing MPI. Verify loaded libucp/module paths before trusting diagnostic evidence. Do not mix old libucp and accidental old module search paths or publish diagnostic timings as performance.

Expected reason logs identify a rejected branch; a cache mismatch alone does not prove it causes failure. Do not remove range checks or overwrite a shared cached descriptor without ownership/lifecycle analysis. Root cause and repair remain pending. Memory optimization also remains pending.


## node89 SDK构建验收通过
用户返回上传包SHA匹配、三项构建安全自检通过、SOURCE_AND_OVERLAY_CHECK PASS。源提交5ea75df7515b2aba6e8e8c11be8b6a383e47cc1f，源目录rma_guard_diag_source_HjCqmn/resource_rma_guard_diag_5ea75df，独立构建目录rma_guard_diag_build_1xitpuf6（资源根见OSU_VALIDATION）。
configure/make/install均成功，模块包含两个诊断标记，ldd新libuct/libucs/libucm路径正确，SDK依赖解析。libuct_ub.so.0.0.0 SHA256=5453d6feb84b3dabb457bf572f998b87ae47a22e4f1ee8b24df7641173f29b92。复制前后基线两项源码hash保持匹配。tar未来时间戳提示存在，未阻止构建，不修改时钟。node97部署与真实MPI诊断NOT_RUN，未修复原问题，禁止标记功能验收完成。

## node97独立安装部署通过
用户返回DIAG_LIBRARY_SHA256SUMS全部文件OK、模块ldd依赖均可解析，新libuct/libucs/libucm均指向rma_guard_diag_build_1xitpuf6/install/lib；RMA_DIAG_TRANSFER_CHECK PASS。原MPI不重建，旧HUCX安装保留。实际进程加载新libucp与新模块仍需复现日志确认。下一步恢复原默认UCP注册缓存配置，使用诊断安装复现同一两点用例。
