/* Linux process memory sampling. Values from procfs are in KiB. */
#ifndef RM_PROC_MEMORY_H
#define RM_PROC_MEMORY_H
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct rm_memory {
    long long rss, pss, private_clean, private_dirty, locked;
    long long vm_size, vm_rss, vm_hwm, vm_lck, vm_pin;
};
static void rm_mem_parse(struct rm_memory *m, const char *line, int rollup)
{
    char key[64], unit[16];
    long long value;
    if (sscanf(line, "%63[^:]: %lld %15s", key, &value, unit) != 3 ||
        value < 0 || strcmp(unit, "kB")) return;
#define FIELD(name, member) if (!strcmp(key, name)) m->member = value
    if (rollup) {
        FIELD("Rss", rss); FIELD("Pss", pss);
        FIELD("Private_Clean", private_clean); FIELD("Private_Dirty", private_dirty);
        FIELD("Locked", locked);
    } else {
        FIELD("VmSize", vm_size); FIELD("VmRSS", vm_rss); FIELD("VmHWM", vm_hwm);
        FIELD("VmLck", vm_lck); FIELD("VmPin", vm_pin);
    }
#undef FIELD
}
static int rm_mem_read(struct rm_memory *m)
{
    *m = (struct rm_memory){-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    const char *paths[] = {"/proc/self/status", "/proc/self/smaps_rollup"};
    char line[512];
    for (int i = 0; i < 2; ++i) {
        FILE *f = fopen(paths[i], "r");
        if (!f) return -1;
        while (fgets(line, sizeof(line), f)) rm_mem_parse(m, line, i == 1);
        int failed = ferror(f);
        fclose(f);
        if (failed) return -1;
    }
    return m->rss >= 0 && m->pss >= 0 && m->private_clean >= 0 &&
           m->private_dirty >= 0 && m->vm_size >= 0 ? 0 : -1;
}
static int rm_mem_emit(const char *phase, int rank)
{
    struct rm_memory m;
    struct timespec now;
    if (rm_mem_read(&m) || clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    /* One short JSON line; no addresses, mappings or cross-node clock math. */
    printf("STAR_MEMORY {\"rank\":%d,\"pid\":%ld,\"phase\":\"%s\","
           "\"unit\":\"KiB\",\"time_ns\":%lld,\"rss\":%lld,\"pss\":%lld,"
           "\"private_clean\":%lld,\"private_dirty\":%lld,\"private_sum\":%lld,"
           "\"locked\":%lld,\"vm_size\":%lld,\"vm_rss\":%lld,\"vm_hwm\":%lld,"
           "\"vm_lck\":%lld,\"vm_pin\":%lld}\n",
           rank, (long)getpid(), phase, (long long)now.tv_sec*1000000000LL+now.tv_nsec,
           m.rss, m.pss, m.private_clean, m.private_dirty, m.private_clean+m.private_dirty,
           m.locked, m.vm_size, m.vm_rss, m.vm_hwm, m.vm_lck, m.vm_pin);
    return fflush(stdout) ? -1 : 0;
}
#endif
