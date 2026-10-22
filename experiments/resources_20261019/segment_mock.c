#include <urma_api.h>
#include <errno.h>
#include <stdlib.h>
static int fail_delete;
void segment_mock_fail_delete(void) { fail_delete = 1; }
urma_target_seg_t *urma_register_seg(urma_context_t *ctx, urma_seg_cfg_t *cfg)
{
    (void)ctx;
    if (!cfg || !cfg->len) { errno = ENOMEM; return NULL; }
    urma_target_seg_t *s = calloc(1, sizeof(*s));
    errno = EAGAIN;
    return s;
}
urma_status_t urma_unregister_seg(urma_target_seg_t *target)
{
    if (fail_delete) { fail_delete = 0; errno = EBUSY; return (urma_status_t)-1; }
    free(target); errno = EAGAIN; return (urma_status_t)0;
}
