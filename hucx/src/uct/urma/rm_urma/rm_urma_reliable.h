/* RM+CTP experimental transport. See LICENSE for terms. */
#ifndef UCT_RM_URMA_RELIABLE_H
#define UCT_RM_URMA_RELIABLE_H
#include <stdint.h>
#include <stddef.h>
#define RM_WINDOW 64u
#define RM_PACKET_MAX 4096u
#define RM_MAGIC 0x524d4354u
#define RM_VERSION 1u
#define RM_CAP_RMA 1u
#define RM_DATA 1u
#define RM_ACK 2u
/* Explicit network byte order; never transmit compiler bitfields or pointers. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version, mtu;
    uint8_t eid[16];
    uint32_t uasid, jetty, token, caps;
    uint64_t cookie;
} rm_address_t;
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version, type, am_id, reserved;
    uint64_t stream, seq, ack, destination;
    rm_address_t source;
} rm_header_t;
_Static_assert(sizeof(rm_address_t) == 48, "RM wire address size");
_Static_assert(sizeof(rm_header_t) == 88, "RM wire header size");
#define RM_PAYLOAD_MAX (RM_PACKET_MAX - sizeof(rm_header_t))
typedef struct {
    uint64_t seq;
    uint8_t am_id;
    size_t length;
    void *data;
} rm_rx_slot_t;
typedef struct {
    uint64_t delivered;
    size_t buffered;
    rm_rx_slot_t slots[RM_WINDOW];
} rm_rx_state_t;
typedef void (*rm_deliver_cb_t)(void *, uint8_t, void *, size_t);
int rm_rx_push(rm_rx_state_t *, uint64_t, uint8_t, const void *, size_t,
               rm_deliver_cb_t, void *);
void rm_rx_cleanup(rm_rx_state_t *);
int rm_ack_valid(uint64_t ack, uint64_t issued);
int rm_retry_due(uint64_t now, uint64_t sent, uint64_t base, unsigned attempts);
int rm_header_valid(const rm_header_t *, size_t length, uint64_t destination);
#endif
