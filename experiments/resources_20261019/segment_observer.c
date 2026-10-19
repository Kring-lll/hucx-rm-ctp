#define _GNU_SOURCE
#include <urma_api.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "proc_memory.h"
#define CAP 16384
struct entry { urma_target_seg_t *ptr; uint64_t bytes, refs; };
static struct entry table[CAP];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static __typeof__(&urma_register_seg) next_register;
static __typeof__(&urma_unregister_seg) next_unregister;
static int fd = -1, rank_id = -1, valid = 1;
static uint64_t seq, registered, unregistered, register_failed, unregister_failed;
static uint64_t live, bytes_live, bytes_total, peak;
static void die(const char *why)
{ fprintf(stderr, "SEG_OBSERVER_ERROR %s\n", why); _exit(125); }
static void *symbol(const char *name)
{
    void *s = dlsym(RTLD_NEXT, name);
    if (!s) {
        void *h = dlopen("liburma.so.0", RTLD_NOW | RTLD_NOLOAD);
        if (h) { s = dlsym(h, name); dlclose(h); }
    }
    if (!s) die(name);
    return s;
}
static void initialize(void)
{
    *(void **)(&next_register) = symbol("urma_register_seg");
    *(void **)(&next_unregister) = symbol("urma_unregister_seg");
    const char *dir = getenv("RM_SEG_OBSERVER_DIR");
    if (!dir || !*dir) return;
    const char *rank = getenv("OMPI_COMM_WORLD_RANK");
    if (rank) rank_id = atoi(rank);
    int directory = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) die("directory");
    char name[96];
    snprintf(name, sizeof(name), "segment_rank%d_pid%ld.jsonl", rank_id, (long)getpid());
    fd = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    close(directory);
    if (fd < 0) die("exclusive_log");
    (void)&rm_mem_emit; /* Header also provides emitter, unused by this library. */
    struct rm_memory warm;
    if (rm_mem_read(&warm)) die("procfs_warmup");
}
static int milestone(uint64_t count)
{ return count && (!(count & (count - 1)) || count <= 4); }
static void record(const char *event, uint64_t length, int rc,
                   int sampled, long long before, long long after)
{
    char line[1024];
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"%s\",\"seq\":%llu,\"pid\":%ld,\"rank\":%d,"
        "\"length\":%llu,\"rc\":%d,\"registered\":%llu,\"unregistered\":%llu,"
        "\"register_failed\":%llu,\"unregister_failed\":%llu,\"live\":%llu,"
        "\"bytes_live\":%llu,\"bytes_total\":%llu,\"peak\":%llu,\"valid\":%d,"
        "\"sampled\":%d,\"rss_before_KiB\":%lld,\"rss_after_KiB\":%lld}\n",
        event, (unsigned long long)++seq, (long)getpid(), rank_id,
        (unsigned long long)length, rc, (unsigned long long)registered,
        (unsigned long long)unregistered, (unsigned long long)register_failed,
        (unsigned long long)unregister_failed, (unsigned long long)live,
        (unsigned long long)bytes_live, (unsigned long long)bytes_total,
        (unsigned long long)peak, valid, sampled, before, after);
    if (n < 0 || (size_t)n >= sizeof(line)) die("format");
    size_t done = 0;
    while (done < (size_t)n) {
        ssize_t wrote = write(fd, line + done, (size_t)n - done);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) die("write");
        done += (size_t)wrote;
    }
}
static long long rss(void)
{
    struct rm_memory m;
    if (rm_mem_read(&m)) die("procfs_read");
    return m.rss;
}
urma_target_seg_t *urma_register_seg(urma_context_t *ctx, urma_seg_cfg_t *cfg)
{
    int saved = errno;
    pthread_once(&once, initialize);
    if (fd < 0) { errno = saved; return next_register(ctx, cfg); }
    pthread_mutex_lock(&mutex);
    int sample = milestone(registered + register_failed + 1);
    long long before = sample ? rss() : -1;
    uint64_t length = cfg ? cfg->len : 0;
    errno = saved;
    urma_target_seg_t *result = next_register(ctx, cfg);
    saved = errno;
    long long after = sample ? rss() : -1;
    if (result) {
        unsigned slot = CAP;
        for (unsigned i = 0; i < CAP; ++i)
            if (table[i].refs && table[i].ptr == result) { slot = i; break; }
        if (slot == CAP)
            for (unsigned i = 0; i < CAP; ++i) if (!table[i].refs) { slot = i; break; }
        if (slot == CAP) valid = 0;
        else {
            if (table[slot].refs && table[slot].bytes != length) valid = 0;
            table[slot].ptr = result; table[slot].bytes = length; ++table[slot].refs;
        }
        ++registered; ++live; bytes_live += length; bytes_total += length;
        if (live > peak) peak = live;
    } else ++register_failed;
    record("register", length, result ? 0 : -1, sample, before, after);
    pthread_mutex_unlock(&mutex);
    errno = saved;
    return result;
}
urma_status_t urma_unregister_seg(urma_target_seg_t *target)
{
    int saved = errno;
    pthread_once(&once, initialize);
    if (fd < 0) { errno = saved; return next_unregister(target); }
    pthread_mutex_lock(&mutex);
    uint64_t length = 0;
    unsigned slot = CAP;
    for (unsigned i = 0; i < CAP; ++i)
        if (table[i].refs && table[i].ptr == target) { slot = i; length = table[i].bytes; break; }
    int sample = milestone(unregistered + unregister_failed + 1);
    long long before = sample ? rss() : -1;
    errno = saved;
    urma_status_t result = next_unregister(target);
    saved = errno;
    long long after = sample ? rss() : -1;
    if (result == 0) {
        if (slot == CAP) valid = 0;
        else { --table[slot].refs; --live; bytes_live -= length; }
        ++unregistered;
    } else ++unregister_failed;
    record("unregister", length, (int)result, sample, before, after);
    pthread_mutex_unlock(&mutex);
    errno = saved;
    return result;
}
__attribute__((destructor)) static void finish(void)
{
    if (fd < 0) return;
    pthread_mutex_lock(&mutex);
    record("process_exit", 0, 0, 0, -1, -1);
    close(fd); fd = -1;
    pthread_mutex_unlock(&mutex);
}
