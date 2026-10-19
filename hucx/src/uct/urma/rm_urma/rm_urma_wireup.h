#ifndef UCT_RM_URMA_WIREUP_H
#define UCT_RM_URMA_WIREUP_H
#include "rm_urma_iface.h"
int rm_address_valid(const rm_address_t *);
urma_target_jetty_t *rm_import(uct_rm_urma_iface_t *, const rm_address_t *);
void rm_receive(uct_rm_urma_iface_t *, const void *, size_t, const urma_jetty_id_t *);
void rm_progress_acks(uct_rm_urma_iface_t *);
#endif
