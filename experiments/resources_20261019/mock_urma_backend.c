/* Self-test backend ONLY. Never linked into MPI or installed as liburma.so. */
#include <urma_api.h>
#include <errno.h>
#include <stdlib.h>

static unsigned next_id = 100;
static int fail_delete;
void rm_mock_fail_next_delete(void) { fail_delete = 1; }

#define CREATE(name, type, cfgtype, idfield) \
    type *name(urma_context_t *ctx, cfgtype *cfg) { \
        if (!cfg) { errno = ENOMEM; return NULL; } \
        type *p = calloc(1, sizeof(*p)); \
        if (p) { p->urma_ctx = ctx; p->idfield.id = ++next_id; errno = EAGAIN; } \
        return p; \
    }
CREATE(urma_create_jfc, urma_jfc_t, urma_jfc_cfg_t, jfc_id)
CREATE(urma_create_jfr, urma_jfr_t, urma_jfr_cfg_t, jfr_id)
CREATE(urma_create_jfs, urma_jfs_t, urma_jfs_cfg_t, jfs_id)
CREATE(urma_create_jetty, urma_jetty_t, urma_jetty_cfg_t, jetty_id)

urma_jfce_t *urma_create_jfce(urma_context_t *ctx)
{
    (void)ctx;
    urma_jfce_t *p = calloc(1, sizeof(*p));
    if (p) errno = EAGAIN;
    return p;
}

#define DELETE(name, type) \
    urma_status_t name(type *p) { \
        if (!p || fail_delete) { fail_delete = 0; errno = EBUSY; return (urma_status_t)-1; } \
        free(p); errno = EAGAIN; return (urma_status_t)0; \
    }
DELETE(urma_delete_jfc, urma_jfc_t)
DELETE(urma_delete_jfr, urma_jfr_t)
DELETE(urma_delete_jfs, urma_jfs_t)
DELETE(urma_delete_jetty, urma_jetty_t)
DELETE(urma_delete_jfce, urma_jfce_t)
DELETE(urma_unimport_jetty, urma_target_jetty_t)
DELETE(urma_unimport_jfr, urma_target_jetty_t)

#define IMPORT(name, remote_type) \
    urma_target_jetty_t *name(urma_context_t *ctx, remote_type *remote, urma_token_t *token) { \
        (void)ctx; (void)remote; (void)token; \
        urma_target_jetty_t *p = calloc(1, sizeof(*p)); \
        if (p) errno = EAGAIN; \
        return p; \
    }
IMPORT(urma_import_jetty, urma_rjetty_t)
IMPORT(urma_import_jfr, urma_rjfr_t)

urma_status_t urma_bind_jetty(urma_jetty_t *jetty, urma_target_jetty_t *target)
{
    (void)jetty; (void)target; errno = EAGAIN; return (urma_status_t)0;
}
urma_status_t urma_unbind_jetty(urma_jetty_t *jetty)
{
    (void)jetty; errno = EAGAIN; return (urma_status_t)0;
}
