/* Experimental URMA API observer. No data-path interception or SDK patching. */
#define _GNU_SOURCE
#include <urma_api.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum kind { JFC, JFR, JFS, JETTY, JFCE, TJETTY, TJFR, BIND, NKINDS };
static const char *const names[] = {
    "jfc", "jfr", "jfs", "jetty", "jfce", "target_jetty", "target_jfr", "binding"
};
struct stats { uint64_t acquired, released, acquire_failed, release_failed, live, peak; };
struct object { const void *ptr, *ctx; uint32_t id; unsigned refs; enum kind kind; };
#define MAX_OBJECTS 16384
static struct object objects[MAX_OBJECTS];
static struct stats totals[NKINDS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static int log_fd = -1, enabled, valid = 1, rank_id = -1;
static uint64_t sequence;

static void die(const char *message)
{
    fprintf(stderr, "RM_RESOURCE_OBSERVER_ERROR %s\n", message);
    _exit(125);
}

static void output(const char *line, size_t length)
{
    while (length) {
        ssize_t n = write(log_fd, line, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) die("log_write_failed");
        line += n;
        length -= (size_t)n;
    }
}

static void initialize(void)
{
    const char *directory = getenv("RM_RESOURCE_OBSERVER_DIR");
    if (!directory || !*directory) return;
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) die("output_directory_unavailable");
    const char *rank = getenv("OMPI_COMM_WORLD_RANK");
    if (rank && *rank) {
        char *end;
        long value = strtol(rank, &end, 10);
        if (!*end && value >= 0 && value <= 1000000) rank_id = (int)value;
    }
    char filename[96], line[256];
    snprintf(filename, sizeof(filename), "rank%d_pid%ld.jsonl", rank_id, (long)getpid());
    log_fd = openat(dir, filename, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    close(dir);
    if (log_fd < 0) die("exclusive_log_creation_failed");
    enabled = 1;
    int n = snprintf(line, sizeof(line),
        "{\"schema\":1,\"event\":\"start\",\"pid\":%ld,\"rank\":%d,"
        "\"scope\":\"URMA_API_handles_not_hardware_TPs\",\"capacity\":%u}\n",
        (long)getpid(), rank_id, MAX_OBJECTS);
    output(line, (size_t)n);
}

static void *resolve(const char *name)
{
    void *symbol = dlsym(RTLD_NEXT, name);
    if (!symbol) {
        /* UCX may dlopen its transport and SDK with local visibility. */
        void *handle = dlopen("liburma.so.0", RTLD_NOW | RTLD_NOLOAD);
        if (handle) {
            symbol = dlsym(handle, name);
            dlclose(handle);
        }
    }
    if (!symbol) die(name);
    return symbol;
}

static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) die("monotonic_clock_failed");
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void record(enum kind kind, int acquire, const void *ptr, const void *ctx,
                   uint32_t id, int mode, uint32_t depth, int rc)
{
    pthread_once(&init_once, initialize);
    if (!enabled) return;
    pthread_mutex_lock(&lock);
    struct stats *s = &totals[kind];
    struct object *found = NULL, *empty = NULL;
    for (unsigned i = 0; i < MAX_OBJECTS; ++i) {
        if (objects[i].refs && objects[i].kind == kind && objects[i].ptr == ptr)
            found = &objects[i];
        if (!objects[i].refs && !empty) empty = &objects[i];
    }
    if (rc) {
        if (acquire) ++s->acquire_failed; else ++s->release_failed;
    } else if (acquire) {
        ++s->acquired;
        if (found) {
            if (kind != TJETTY && kind != TJFR) valid = 0;
            ++found->refs;
        } else if (empty && ptr) {
            *empty = (struct object){ptr, ctx, id, 1, kind};
        } else valid = 0;
        ++s->live;
        if (s->live > s->peak) s->peak = s->live;
    } else {
        ++s->released;
        if (found && s->live) {
            ctx = found->ctx;
            id = found->id;
            --found->refs;
            --s->live;
        } else valid = 0;
    }
    char line[768];
    int n = snprintf(line, sizeof(line),
        "{\"schema\":1,\"event\":\"%s\",\"seq\":%" PRIu64 ","
        "\"time_ns\":%" PRIu64 ",\"kind\":\"%s\",\"pid\":%ld,\"rank\":%d,"
        "\"ptr\":\"%p\",\"ctx\":\"%p\",\"id\":%u,\"mode\":%d,\"depth\":%u,"
        "\"rc\":%d,\"acquired\":%" PRIu64 ",\"released\":%" PRIu64 ","
        "\"live\":%" PRIu64 ",\"peak\":%" PRIu64 ",\"valid\":%d}\n",
        acquire ? "acquire" : "release", ++sequence, now_ns(), names[kind],
        (long)getpid(), rank_id, ptr, ctx, id, mode, depth, rc,
        s->acquired, s->released, s->live, s->peak, valid);
    if (n < 0 || (size_t)n >= sizeof(line)) die("event_buffer_overflow");
    output(line, (size_t)n);
    pthread_mutex_unlock(&lock);
}

/* Called by a later MPI probe through dlsym, and once at normal process exit. */
void rm_resource_observer_snapshot(const char *phase)
{
    int saved = errno;
    pthread_once(&init_once, initialize);
    if (!enabled) { errno = saved; return; }
    char label[80];
    size_t j = 0;
    for (const char *p = phase ? phase : "unknown"; *p && j + 1 < sizeof(label); ++p)
        label[j++] = ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_') ? *p : '_';
    label[j] = 0;
    pthread_mutex_lock(&lock);
    for (int k = 0; k < NKINDS; ++k) {
        struct stats *s = &totals[k];
        uint64_t unique = 0;
        for (unsigned i = 0; i < MAX_OBJECTS; ++i)
            unique += objects[i].refs && objects[i].kind == (enum kind)k;
        char line[768];
        int n = snprintf(line, sizeof(line),
            "{\"schema\":1,\"event\":\"snapshot\",\"seq\":%" PRIu64 ","
            "\"phase\":\"%s\",\"kind\":\"%s\",\"pid\":%ld,\"rank\":%d,"
            "\"acquired\":%" PRIu64 ",\"released\":%" PRIu64 ","
            "\"acquire_failed\":%" PRIu64 ",\"release_failed\":%" PRIu64 ","
            "\"live\":%" PRIu64 ",\"peak\":%" PRIu64 ","
            "\"unique_live\":%" PRIu64 ",\"valid\":%d}\n",
            ++sequence, label, names[k], (long)getpid(), rank_id,
            s->acquired, s->released, s->acquire_failed, s->release_failed,
            s->live, s->peak, unique, valid);
        if (n < 0 || (size_t)n >= sizeof(line)) die("snapshot_buffer_overflow");
        output(line, (size_t)n);
    }
    pthread_mutex_unlock(&lock);
    errno = saved;
}

int rm_resource_observer_abi_version(void) { return 1; }

__attribute__((destructor)) static void finish(void)
{
    rm_resource_observer_snapshot("process_exit");
    if (log_fd >= 0) close(log_fd);
}

#define REAL(name) \
    static __typeof__(&name) real_##name; \
    static pthread_once_t once_##name = PTHREAD_ONCE_INIT; \
    static void resolve_##name(void) { real_##name = (__typeof__(&name))resolve(#name); }
#define CALL(name, ...) ({ int incoming_errno = errno; \
    pthread_once(&once_##name, resolve_##name); errno = incoming_errno; \
    real_##name(__VA_ARGS__); })

#define CREATE_LOCAL(name, type, cfgtype, kind_, idfield, mode_, depth_) \
    REAL(name) \
    type *name(urma_context_t *ctx, cfgtype *cfg) { \
        type *obj = CALL(name, ctx, cfg); int saved = errno; \
        record(kind_, 1, obj, ctx, obj ? obj->idfield.id : 0, \
               cfg ? (mode_) : -1, cfg ? (depth_) : 0, obj ? 0 : -1); \
        errno = saved; return obj; \
    }
CREATE_LOCAL(urma_create_jfc, urma_jfc_t, urma_jfc_cfg_t, JFC, jfc_id, -1, cfg->depth)
CREATE_LOCAL(urma_create_jfr, urma_jfr_t, urma_jfr_cfg_t, JFR, jfr_id, (int)cfg->trans_mode, cfg->depth)
CREATE_LOCAL(urma_create_jfs, urma_jfs_t, urma_jfs_cfg_t, JFS, jfs_id, (int)cfg->trans_mode, cfg->depth)
CREATE_LOCAL(urma_create_jetty, urma_jetty_t, urma_jetty_cfg_t, JETTY, jetty_id,
             (int)cfg->jfs_cfg.trans_mode, cfg->jfs_cfg.depth)

REAL(urma_create_jfce)
urma_jfce_t *urma_create_jfce(urma_context_t *ctx)
{
    urma_jfce_t *obj = CALL(urma_create_jfce, ctx); int saved = errno;
    record(JFCE, 1, obj, ctx, 0, -1, 0, obj ? 0 : -1);
    errno = saved; return obj;
}

#define DELETE_LOCAL(name, type, kind_) \
    REAL(name) \
    urma_status_t name(type *obj) { \
        const uintptr_t address = (uintptr_t)obj; \
        urma_status_t rc = CALL(name, obj); int saved = errno; \
        record(kind_, 0, (void *)address, NULL, 0, -1, 0, (int)rc); \
        errno = saved; return rc; \
    }
DELETE_LOCAL(urma_delete_jfc, urma_jfc_t, JFC)
DELETE_LOCAL(urma_delete_jfr, urma_jfr_t, JFR)
DELETE_LOCAL(urma_delete_jfs, urma_jfs_t, JFS)
DELETE_LOCAL(urma_delete_jetty, urma_jetty_t, JETTY)
DELETE_LOCAL(urma_delete_jfce, urma_jfce_t, JFCE)
DELETE_LOCAL(urma_unimport_jetty, urma_target_jetty_t, TJETTY)
DELETE_LOCAL(urma_unimport_jfr, urma_target_jetty_t, TJFR)

#define IMPORT(name, remote_type, kind_) \
    REAL(name) \
    urma_target_jetty_t *name(urma_context_t *ctx, remote_type *remote, urma_token_t *token) { \
        urma_target_jetty_t *obj = CALL(name, ctx, remote, token); int saved = errno; \
        record(kind_, 1, obj, ctx, 0, -1, 0, obj ? 0 : -1); \
        errno = saved; return obj; \
    }
IMPORT(urma_import_jetty, urma_rjetty_t, TJETTY)
IMPORT(urma_import_jfr, urma_rjfr_t, TJFR)

REAL(urma_bind_jetty)
urma_status_t urma_bind_jetty(urma_jetty_t *jetty, urma_target_jetty_t *target)
{
    urma_status_t rc = CALL(urma_bind_jetty, jetty, target); int saved = errno;
    record(BIND, 1, jetty, jetty ? jetty->urma_ctx : NULL,
           jetty ? jetty->jetty_id.id : 0, -1, 0, (int)rc);
    errno = saved; return rc;
}
REAL(urma_unbind_jetty)
urma_status_t urma_unbind_jetty(urma_jetty_t *jetty)
{
    urma_status_t rc = CALL(urma_unbind_jetty, jetty); int saved = errno;
    record(BIND, 0, jetty, NULL, 0, -1, 0, (int)rc);
    errno = saved; return rc;
}
