/* Resource scaling probe: direct, sequential center/peer exchanges.
 * No MPI collectives before the after_exchange snapshot.
 * Process counts measure software endpoints, not physical-node scaling. */
#include <mpi.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int rank_id;
static void fail(const char *why)
{
    fprintf(stderr, "STAR_FAIL rank=%d reason=%s\n", rank_id, why);
    MPI_Abort(MPI_COMM_WORLD, 2);
    _exit(2);
}
static void check(int rc) { if (rc != MPI_SUCCESS) fail("MPI_error"); }
static unsigned char pattern(int peer, int i)
{ return (unsigned char)((peer * 37 + i * 13) & 255); }
int main(int argc, char **argv)
{
    int size;
    char host[256] = {0};
    unsigned char data[64];
    void (*snapshot)(const char *) = NULL;
    int (*abi)(void) = NULL;
    /* Required by design: refuse an unobserved run instead of silent success. */
    *(void **)(&snapshot) = dlsym(RTLD_DEFAULT, "rm_resource_observer_snapshot");
    *(void **)(&abi) = dlsym(RTLD_DEFAULT, "rm_resource_observer_abi_version");
    if (!snapshot || !abi || abi() != 1 || !getenv("RM_RESOURCE_OBSERVER_DIR")) {
        fprintf(stderr, "STAR_FAIL reason=observer_required\n");
        return 2;
    }
    check(MPI_Init(&argc, &argv));
    check(MPI_Comm_rank(MPI_COMM_WORLD, &rank_id));
    check(MPI_Comm_size(MPI_COMM_WORLD, &size));
    if (size < 2 || size > 9) fail("require_2_to_9_ranks");
    if (gethostname(host, sizeof(host)-1)) fail("gethostname");
    printf("STAR_SETUP rank=%d ranks=%d node=%s bytes=64 topology=star_sequential\n",
           rank_id, size, host);
    fflush(stdout);
    snapshot("after_init");
    if (rank_id == 0) {
        for (int peer = 1; peer < size; ++peer) {
            for (int i = 0; i < 64; ++i) data[i] = pattern(peer, i);
            check(MPI_Send(data, 64, MPI_BYTE, peer, 101, MPI_COMM_WORLD));
            memset(data, 0, sizeof(data));
            check(MPI_Recv(data, 64, MPI_BYTE, peer, 102, MPI_COMM_WORLD,
                           MPI_STATUS_IGNORE));
            for (int i = 0; i < 64; ++i)
                if (data[i] != (unsigned char)(pattern(peer, i) ^ 0x5a))
                    fail("reply_content");
        }
    } else {
        check(MPI_Recv(data, 64, MPI_BYTE, 0, 101, MPI_COMM_WORLD,
                       MPI_STATUS_IGNORE));
        for (int i = 0; i < 64; ++i) {
            if (data[i] != pattern(rank_id, i)) fail("request_content");
            data[i] ^= 0x5a;
        }
        check(MPI_Send(data, 64, MPI_BYTE, 0, 102, MPI_COMM_WORLD));
    }
    snapshot("after_exchange");
    /* Peers stay alive while the center takes its snapshot. These extra
     * control exchanges reuse the same center/peer edges, no peer mesh. */
    if (rank_id == 0) {
        for (int peer = 1; peer < size; ++peer)
            check(MPI_Send(data, 1, MPI_BYTE, peer, 103, MPI_COMM_WORLD));
    } else {
        check(MPI_Recv(data, 1, MPI_BYTE, 0, 103, MPI_COMM_WORLD,
                       MPI_STATUS_IGNORE));
    }
    printf("STAR_CONTENT_PASS rank=%d ranks=%d peers=%d\n", rank_id, size, size-1);
    fflush(stdout);
    check(MPI_Finalize());
    snapshot("after_finalize");
    return 0;
}
