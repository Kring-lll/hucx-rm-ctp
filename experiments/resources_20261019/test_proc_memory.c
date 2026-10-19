/* Standalone procfs parser/read check: no MPI or URMA. */
#include <assert.h>
#include "proc_memory.h"
int main(void)
{
    struct rm_memory m = {0};
    rm_mem_parse(&m, "Rss: 123 kB\n", 1);
    rm_mem_parse(&m, "Pss: 77 kB\n", 1);
    rm_mem_parse(&m, "Private_Clean: 4 kB\n", 1);
    rm_mem_parse(&m, "Private_Dirty: 8 kB\n", 1);
    rm_mem_parse(&m, "VmSize: 999 kB\n", 0);
    rm_mem_parse(&m, "Rss: 999 MB\n", 1);
    rm_mem_parse(&m, "Pss: -1 kB\n", 1);
    assert(m.rss == 123 && m.pss == 77 && m.vm_size == 999);
    assert(m.private_clean + m.private_dirty == 12);
    assert(rm_mem_read(&m) == 0);
    assert(m.rss > 0 && m.pss >= 0);
    assert(rm_mem_emit("selftest", 0) == 0);
    puts("PROC_MEMORY_SELFTEST=PASS no_MPI_no_URMA=1");
    return 0;
}
