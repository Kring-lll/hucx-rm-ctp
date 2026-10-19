/* Experimental HMPI RM failure notification adapter. Disabled unless opted in.
 * The UCP callback only publishes the first error. Abort is requested after
 * ucp_worker_progress returns, never from inside the UCP callback. */
#ifndef HMPI_RM_FAILFAST_20261016_V1_H
#define HMPI_RM_FAILFAST_20261016_V1_H

#include "ompi/runtime/mpiruntime.h"
#include "ompi/communicator/communicator.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 0: empty; 1: first writer; 2: published; 3: abort requested. */
static int hmpi_rm_ff_state;
static ucs_status_t hmpi_rm_ff_status;
static uint32_t hmpi_rm_ff_peer;

static int hmpi_rm_ff_enabled(void)
{
    const char *value = getenv("HUCX_MPI_RM_FAIL_FAST");
    return value != NULL && strcmp(value, "1") == 0;
}

static void hmpi_rm_ff_error_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    int expected = 0;
    (void)ep;
    if (__atomic_compare_exchange_n(&hmpi_rm_ff_state, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        hmpi_rm_ff_status = status;
        hmpi_rm_ff_peer = (uint32_t)(uintptr_t)arg;
        __atomic_store_n(&hmpi_rm_ff_state, 2, __ATOMIC_RELEASE);
    }
}

static unsigned hmpi_rm_ff_worker_progress(void)
{
    unsigned progressed = ucp_worker_progress(ompi_pml_ucx.ucp_worker);
    int expected = 2;
    if (__atomic_compare_exchange_n(&hmpi_rm_ff_state, &expected, 3, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        PML_UCX_ERROR("HMPI_RM_FAIL_FAST peer=%u status=%s action=abort_job "
                      "callback_deferred=1 exit_code=86",
                      (unsigned)hmpi_rm_ff_peer,
                      ucs_status_string(hmpi_rm_ff_status));
        ompi_mpi_abort(&ompi_mpi_comm_world.comm, 86);
        /* Defensive fallback only if the MPI runtime abort unexpectedly returns.
         * This is fail-fast, not MPI request recovery or graceful finalization. */
        _exit(86);
    }
    return progressed;
}

#endif
