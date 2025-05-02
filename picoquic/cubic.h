//
// Created by Matthias Hofstätter on 12.03.24.
//

#ifndef PICOQUIC_CUBIC_H
#define PICOQUIC_CUBIC_H

#include "cc_common.h"

#define PICOQUIC_CUBIC_C 0.4
#define PICOQUIC_CUBIC_BETA_ECN (7.0 / 8.0)
#define PICOQUIC_CUBIC_BETA (3.0 / 4.0)

typedef enum {
    picoquic_cubic_alg_slow_start = 0,
    picoquic_cubic_alg_recovery,
    picoquic_cubic_alg_congestion_avoidance
} picoquic_cubic_alg_state_t;

typedef struct st_picoquic_cubic_state_t {
    picoquic_cubic_alg_state_t alg_state;
    uint64_t recovery_sequence;
    uint64_t start_of_epoch;
    uint64_t previous_start_of_epoch;
    double K;
    double W_max;
    double W_last_max;
    double C;
    double beta;
    double W_reno;
    uint64_t ssthresh;
    picoquic_min_max_rtt_t rtt_filter;
    picoquic_cr_state_t cr_state;
} picoquic_cubic_state_t;

void cubic_reset(picoquic_cubic_state_t* cubic_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x, uint64_t current_time);

void cubic_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, char const* option_string, uint64_t current_time);

/*
 * Properly implementing Cubic requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
void cubic_notify(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time);

/*
 * Define delay-based Cubic, dcubic, and alternative congestion control protocol similar to Cubic but
 * using delay measurements instead of reacting to packet losses. This is a quic hack, intended for
 * trials of a lossy satellite networks.
 */
void dcubic_notify(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time);

/* Observe the state of congestion control */
void cubic_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param);


#endif //PICOQUIC_CUBIC_H