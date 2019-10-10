// SPDX-License-Identifier: MIT
// Copyright © 2019 William Budd

#define RS_INCLUDE_PRODUCE_RING_MSG // See ringsocket_ring.h

#include "rs_to_app.h"

rs_ret init_inbound_producers(
    struct rs_worker * worker
) {
    RS_CALLOC(worker->inbound_producers, worker->conf->app_c);
    for (size_t i = 0; i < worker->conf->app_c; i++) {
        struct rs_ring_producer * prod = worker->inbound_producers + i;
        prod->ring_size = worker->conf->inbound_ring_buf_size;
        RS_CACHE_ALIGNED_CALLOC(prod->ring, prod->ring_size);
        prod->w = prod->ring;
        RS_ATOMIC_STORE_RELAXED(&worker->ring_pairs[i]->inbound_ring.w,
            (atomic_uintptr_t) prod->ring);
        RS_ATOMIC_STORE_RELAXED(&worker->ring_pairs[i]->inbound_ring.r,
            (atomic_uintptr_t) prod->ring);
    }
    // Outbound rings are initialized by app threads through
    // rs_init_outbound_producers() of ringsocket.h
    return RS_OK;
}

static rs_ret send_msg_to_app(
    struct rs_worker * worker,
    union rs_peer const * peer,
    uint32_t peer_i,
    uint8_t const * msg,
    uint32_t msg_size,
    enum rs_inbound_kind kind
) {
    struct rs_ring_producer * prod = worker->inbound_producers + peer->app_i;
    struct rs_inbound_msg_header header = {
        .peer_i = peer_i,
        .socket_fd = peer->socket_fd,
        .endpoint_id = worker->conf->apps[peer->app_i]
            .endpoints[peer->endpoint_i].endpoint_id,
        .is_utf8 = peer->ws.rmsg_is_utf8,
        .kind = kind
    };
    RS_GUARD(rs_produce_ring_msg(&worker->ring_pairs[peer->app_i]->inbound_ring,
        prod, worker->conf->realloc_multiplier, sizeof(header) + msg_size));
    memcpy(prod->w, &header, sizeof(header));
    prod->w += sizeof(header);
    if (msg_size) {
        memcpy(prod->w, msg, msg_size);
        prod->w += msg_size;
    }
    enqueue_ring_update(worker, prod->w, peer->app_i, true);
    return RS_OK;
}

rs_ret send_open_to_app(
    struct rs_worker * worker,
    union rs_peer const * peer,
    uint32_t peer_i
) {
    return worker->conf->apps[peer->app_i].wants_open_notification ?
        send_msg_to_app(worker, peer, peer_i, NULL, 0, RS_INBOUND_OPEN) : RS_OK;
}

rs_ret send_read_to_app(
    struct rs_worker * worker,
    union rs_peer const * peer,
    uint32_t peer_i,
    uint8_t const * msg,
    uint32_t msg_size
) {
    return send_msg_to_app(worker, peer, peer_i, msg, msg_size,
        RS_INBOUND_READ);
}

rs_ret send_close_to_app(
    struct rs_worker * worker,
    union rs_peer const * peer,
    uint32_t peer_i
) {
    return worker->conf->apps[peer->app_i].wants_close_notification ?
        send_msg_to_app(worker, peer, peer_i, NULL, 0, RS_INBOUND_CLOSE) :
        RS_OK;
}
