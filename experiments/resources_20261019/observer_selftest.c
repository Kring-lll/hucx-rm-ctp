#include <urma_api.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

extern void rm_mock_fail_next_delete(void);
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "OBSERVER_SELFTEST_FAIL line=%d\n", __LINE__); exit(2); } } while (0)

int main(void)
{
    void (*snapshot)(const char *) = (void (*)(const char *))dlsym(RTLD_DEFAULT, "rm_resource_observer_snapshot");
    int (*version)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rm_resource_observer_abi_version");
    REQUIRE(snapshot && version && version() == 1);
    snapshot("before");
    REQUIRE(urma_create_jfc(NULL, NULL) == NULL && errno == ENOMEM);
    urma_jfc_cfg_t cq = {0}; cq.depth = 16;
    urma_jfr_cfg_t rq = {0}; rq.depth = 32;
    urma_jfs_cfg_t sq = {0}; sq.depth = 64;
    urma_jetty_cfg_t jq = {0}; jq.jfs_cfg.depth = 128;
    urma_jfc_t *c1 = urma_create_jfc(NULL, &cq);
    REQUIRE(c1 && errno == EAGAIN);
    urma_jfc_t *c2 = urma_create_jfc(NULL, &cq);
    urma_jfr_t *r = urma_create_jfr(NULL, &rq);
    urma_jfs_t *s = urma_create_jfs(NULL, &sq);
    urma_jetty_t *j = urma_create_jetty(NULL, &jq);
    urma_jfce_t *e = urma_create_jfce(NULL);
    urma_target_jetty_t *t = urma_import_jetty(NULL, NULL, NULL);
    urma_target_jetty_t *tr = urma_import_jfr(NULL, NULL, NULL);
    REQUIRE(c2 && r && s && j && e && t && tr);
    REQUIRE(urma_bind_jetty(j, t) == 0 && errno == EAGAIN);
    snapshot("live");
    rm_mock_fail_next_delete();
    REQUIRE(urma_delete_jfc(c1) != 0 && errno == EBUSY);
    snapshot("failed_delete");
    REQUIRE(urma_unbind_jetty(j) == 0);
    REQUIRE(urma_unimport_jetty(t) == 0);
    REQUIRE(urma_unimport_jfr(tr) == 0);
    REQUIRE(urma_delete_jetty(j) == 0);
    REQUIRE(urma_delete_jfs(s) == 0);
    REQUIRE(urma_delete_jfr(r) == 0);
    REQUIRE(urma_delete_jfc(c1) == 0);
    REQUIRE(urma_delete_jfc(c2) == 0);
    REQUIRE(urma_delete_jfce(e) == 0 && errno == EAGAIN);
    snapshot("after");
    puts("OBSERVER_FORWARDING_SELFTEST=PASS backend=mock no_hardware_operations=1");
    return 0;
}
