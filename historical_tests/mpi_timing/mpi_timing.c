#define _POSIX_C_SOURCE 200112L
#include <mpi.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int rank_id = -1;

static uint64_t now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static double ms(uint64_t duration)
{
    return (double)duration / 1000000.0;
}

static void mpi_check(int rc, const char *operation)
{
    if (rc != MPI_SUCCESS) {
        char message[MPI_MAX_ERROR_STRING];
        int length = 0;
        MPI_Error_string(rc, message, &length);
        fprintf(stderr, "FAIL MPI rank=%d operation=%s error=%.*s\n",
                rank_id, operation, length, message);
        MPI_Abort(MPI_COMM_WORLD, 1);
        exit(1);
    }
}

static void invalid(const char *message)
{
    fprintf(stderr, "FAIL MPI rank=%d reason=%s\n", rank_id, message);
    MPI_Abort(MPI_COMM_WORLD, 1);
    exit(1);
}

static unsigned long number(const char *s, unsigned long maximum)
{
    char *end;
    unsigned long value;
    errno = 0;
    if (!s[0] || s[0] < '0' || s[0] > '9') invalid("invalid_number");
    value = strtoul(s, &end, 10);
    if (errno || *end || !value || value > maximum) invalid("invalid_number");
    return value;
}

static unsigned char pattern(size_t i, int source, unsigned iteration)
{
    return (unsigned char)(i + (i >> 8) * 17u +
                           (unsigned)source * 37u + iteration * 13u + 90u);
}

static unsigned char *buffer(size_t length)
{
    void *p = NULL;
    size_t rounded = (length + 4095u) & ~(size_t)4095u;
    if (posix_memalign(&p, 4096u, rounded)) invalid("allocation_failed");
    memset(p, 0, rounded);
    return p;
}

static int verify(const unsigned char *p, size_t n, int source, unsigned iteration)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char expected = pattern(i, source, iteration);
        if (p[i] != expected) {
            fprintf(stderr,
                    "FAIL DATA rank=%d source=%d iteration=%u offset=%zu expected=%u actual=%u\n",
                    rank_id, source, iteration, i, (unsigned)expected, (unsigned)p[i]);
            return 1;
        }
    }
    return 0;
}

typedef struct {
    uint64_t fill, prepare, communication, verify, allreduce;
} iteration_t;

int main(int argc, char **argv)
{
    uint64_t entry = now_ns();
    uint64_t init_start = now_ns();
    int rc = MPI_Init(&argc, &argv);
    uint64_t init_end = now_ns();
    uint64_t setup_end, loop_end, finalize_start, finalize_end;
    uint64_t parts, other;
    iteration_t rows[10] = {{0}}, total = {0};
    int ranks, provided, host_length;
    char host[MPI_MAX_PROCESSOR_NAME];
    const char *mode;
    size_t n, receive_length, i;
    unsigned iterations, iteration;
    unsigned char *tx, *rx;
    int p2p, broadcast, gather;
    if (rc != MPI_SUCCESS) {
        fprintf(stderr, "FAIL MPI operation=init status=%d elapsed_ms=%.6f\n", rc, ms(init_end-init_start));
        return 1;
    }
    mpi_check(MPI_Comm_rank(MPI_COMM_WORLD, &rank_id), "rank");
    mpi_check(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN), "errhandler");
    mpi_check(MPI_Comm_size(MPI_COMM_WORLD, &ranks), "size");
    mpi_check(MPI_Query_thread(&provided), "query_thread");
    mpi_check(MPI_Get_processor_name(host, &host_length), "processor_name");
    if (argc != 4) invalid("usage_mode_bytes_iterations");
    mode = argv[1];
    p2p = !strcmp(mode, "p2p");
    broadcast = !strcmp(mode, "bcast");
    gather = !strcmp(mode, "gather");
    if (!p2p && !broadcast && !gather) invalid("invalid_mode");
    if ((p2p && ranks != 2) || (!p2p && (ranks < 3 || ranks > 16)))
        invalid("invalid_rank_count");
    n = (size_t)number(argv[2], 8388608ul);
    iterations = (unsigned)number(argv[3], 10ul);
    receive_length = gather && rank_id == 0 ? n * (size_t)ranks : n;
    tx = buffer(n);
    rx = buffer(receive_length);
    printf("MPI_PATH_SETUP mode=%s rank=%d ranks=%d node=%.*s bytes=%zu thread_level=%d alignment=4096 timing=v1\n",
           mode, rank_id, ranks, host_length, host, n, provided);
    fflush(stdout);
    setup_end = now_ns();

    for (iteration = 0; iteration < iterations; ++iteration) {
        int local_error = 0, global_error = 0;
        MPI_Status status;
        uint64_t a = now_ns(), b, c, d, e, f;
        for (i = 0; i < n; ++i) tx[i] = pattern(i, rank_id, iteration);
        b = now_ns();
        memset(rx, 165, receive_length);
        if (broadcast && rank_id == 0) memcpy(rx, tx, n);
        c = now_ns();
        if (p2p) {
            int peer = 1 - rank_id;
            mpi_check(MPI_Sendrecv(tx, (int)n, MPI_BYTE, peer, 201,
                                   rx, (int)n, MPI_BYTE, peer, 201,
                                   MPI_COMM_WORLD, &status), "sendrecv");
        } else if (broadcast) {
            mpi_check(MPI_Bcast(rx, (int)n, MPI_BYTE, 0, MPI_COMM_WORLD), "bcast");
        } else {
            mpi_check(MPI_Gather(tx, (int)n, MPI_BYTE, rx, (int)n, MPI_BYTE,
                                 0, MPI_COMM_WORLD), "gather");
        }
        d = now_ns();
        if (p2p) {
            int count = -1, peer = 1 - rank_id;
            mpi_check(MPI_Get_count(&status, MPI_BYTE, &count), "get_count");
            local_error = count != (int)n || status.MPI_SOURCE != peer || status.MPI_TAG != 201;
            local_error |= verify(rx, n, peer, iteration);
        } else if (broadcast) {
            local_error = verify(rx, n, 0, iteration);
        } else if (rank_id == 0) {
            int source;
            for (source = 0; source < ranks; ++source)
                local_error |= verify(rx + (size_t)source * n, n, source, iteration);
        }
        e = now_ns();
        mpi_check(MPI_Allreduce(&local_error, &global_error, 1, MPI_INT,
                                MPI_MAX, MPI_COMM_WORLD), "verify_allreduce");
        f = now_ns();
        if (global_error) invalid("content_or_receive_metadata_mismatch");
        rows[iteration].fill = b-a;
        rows[iteration].prepare = c-b;
        rows[iteration].communication = d-c;
        rows[iteration].verify = e-d;
        rows[iteration].allreduce = f-e;
        total.fill += b-a;
        total.prepare += c-b;
        total.communication += d-c;
        total.verify += e-d;
        total.allreduce += f-e;
    }
    loop_end = now_ns();
    printf("PASS MPI mode=%s rank=%d ranks=%d bytes=%zu iterations=%u\n",
           mode, rank_id, ranks, n, iterations);
    fflush(stdout);
    free(rx);
    free(tx);
    finalize_start = now_ns();
    rc = MPI_Finalize();
    finalize_end = now_ns();
    if (rc != MPI_SUCCESS) {
        fprintf(stderr, "FAIL MPI rank=%d operation=finalize status=%d\n", rank_id, rc);
        return 1;
    }
    parts = (init_end-init_start) + (setup_end-init_end) + total.fill + total.prepare +
            total.communication + total.verify + total.allreduce +
            (finalize_start-loop_end) + (finalize_end-finalize_start);
    other = finalize_end-entry >= parts ? finalize_end-entry-parts : 0;
    printf("MPI_TIMING_SUMMARY rank=%d node=%.*s entry_ns=%" PRIu64
           " finalize_end_ns=%" PRIu64 " init_ms=%.6f setup_ms=%.6f fill_ms=%.6f"
           " prepare_ms=%.6f communication_ms=%.6f verify_ms=%.6f allreduce_ms=%.6f"
           " cleanup_ms=%.6f finalize_ms=%.6f other_ms=%.6f app_ms=%.6f\n",
           rank_id, host_length, host, entry, finalize_end,
           ms(init_end-init_start), ms(setup_end-init_end), ms(total.fill), ms(total.prepare),
           ms(total.communication), ms(total.verify), ms(total.allreduce),
           ms(finalize_start-loop_end), ms(finalize_end-finalize_start), ms(other), ms(finalize_end-entry));
    for (iteration = 0; iteration < iterations; ++iteration) {
        printf("MPI_TIMING_ITER rank=%d iteration=%u fill_ms=%.6f prepare_ms=%.6f"
               " communication_ms=%.6f verify_ms=%.6f allreduce_ms=%.6f\n",
               rank_id, iteration, ms(rows[iteration].fill), ms(rows[iteration].prepare),
               ms(rows[iteration].communication), ms(rows[iteration].verify), ms(rows[iteration].allreduce));
    }
    fflush(stdout);
    return 0;
}
