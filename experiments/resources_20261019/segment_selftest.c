#include <urma_api.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
extern void segment_mock_fail_delete(void);
int main(void)
{
    urma_seg_cfg_t cfg = {0};
    assert(!urma_register_seg(NULL, &cfg) && errno == ENOMEM);
    cfg.len = 4096;
    urma_target_seg_t *a = urma_register_seg(NULL, &cfg);
    assert(a && errno == EAGAIN);
    cfg.len = 8192;
    urma_target_seg_t *b = urma_register_seg(NULL, &cfg);
    assert(b && errno == EAGAIN);
    segment_mock_fail_delete();
    assert(urma_unregister_seg(a) != 0 && errno == EBUSY);
    assert(urma_unregister_seg(a) == 0 && errno == EAGAIN);
    assert(urma_unregister_seg(b) == 0 && errno == EAGAIN);
    puts("SEGMENT_FORWARDING_SELFTEST=PASS mock_no_hardware=1");
    return 0;
}
