#include "rm_urma_reliable.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <arpa/inet.h>
static unsigned received;
static void deliver(void *arg, uint8_t id, void *p, size_t n)
{
    const unsigned *value = p;
    assert(id == 7 && n == sizeof(*value) && *value == ++received);
}
int main(void)
{
    rm_rx_state_t rx = {0};
    rm_header_t h = {0};
    unsigned value, base, step, sequence, i;
    /* Reordered packets plus duplicate DATA and lost ACK: delivery exactly once. */
    value = 2; assert(rm_rx_push(&rx, 2, 7, &value, sizeof(value), deliver, NULL) == 0);
    assert(received == 0);
    assert(rx.buffered == sizeof(value));
    assert(rm_rx_push(&rx, 2, 7, &value, sizeof(value), deliver, NULL) == 1);
    value = 1; assert(rm_rx_push(&rx, 1, 7, &value, sizeof(value), deliver, NULL) == 0);
    assert(received == 2);
    assert(rx.buffered == 0);
    assert(rm_rx_push(&rx, 1, 7, &value, sizeof(value), deliver, NULL) == 1);
    assert(rm_rx_push(&rx, 0, 7, &value, sizeof(value), deliver, NULL) < 0);
    assert(rm_rx_push(&rx, 67, 7, &value, sizeof(value), deliver, NULL) < 0);
    /* Deterministic permutation exercises every circular-window position. */
    for (base = 3; base < 64003; base += RM_WINDOW) {
        for (step = 0; step < RM_WINDOW; ++step) {
            sequence = base + (step * 17 % RM_WINDOW);
            assert(rm_rx_push(&rx, sequence, 7, &sequence, sizeof(sequence), deliver, NULL) >= 0);
            assert(rm_rx_push(&rx, sequence, 7, &sequence, sizeof(sequence), deliver, NULL) == 1);
        }
    }
    assert(received == 64002);
    assert(rm_ack_valid(0, 10) && rm_ack_valid(10, 10) && !rm_ack_valid(11, 10));
    assert(!rm_retry_due(109, 100, 10, 0) && rm_retry_due(110, 100, 10, 0));
    assert(!rm_retry_due(119, 100, 10, 1) && rm_retry_due(120, 100, 10, 1));
    assert(!rm_retry_due(99, 100, 10, 0));
    assert(!rm_retry_due(1000, 0, UINT64_MAX, 7));
    h.magic = htonl(RM_MAGIC); h.version = RM_VERSION; h.type = RM_DATA;
    h.destination = htobe64(9); h.stream = htobe64(1);
    h.source.magic = htonl(RM_MAGIC); h.source.version = htons(RM_VERSION);
    h.source.mtu = htons(RM_PACKET_MAX); h.source.cookie = htobe64(7);
    assert(rm_header_valid(&h, sizeof(h), 9));
    assert(!rm_header_valid(&h, sizeof(h)-1, 9));
    assert(!rm_header_valid(&h, sizeof(h), 8));
    assert(!rm_header_valid(&h, RM_PACKET_MAX+1, 9));
    h.type = RM_ACK; assert(!rm_header_valid(&h, sizeof(h)+1, 9));
    h.reserved = 1; assert(!rm_header_valid(&h, sizeof(h), 9));
    rm_rx_cleanup(&rx);
    /* UINT64_MAX is terminal, never wrap into an earlier stream epoch. */
    rx.delivered = UINT64_MAX; assert(rm_rx_push(&rx, 1, 7, &value, 4, deliver, NULL) == 1);
    rm_rx_cleanup(&rx);
    for (i = 0; i < 10000; ++i) {
        value = 2; assert(rm_rx_push(&rx, 2, 7, &value, 4, deliver, NULL) == 0);
        rm_rx_cleanup(&rx);
    }
    puts("PASS reliable: 64002 ordered deliveries; reorder/duplicates/ACK-loss replay/window/wire validation/backoff/cleanup");
    return 0;
}
