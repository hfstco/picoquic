//
// Created by Matthias Hofstätter on 12.03.24.
//

#ifndef PICOQUIC_CUBIC_H
#define PICOQUIC_CUBIC_H

#include "cc_common.h"

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

void picoquic_cubic_reset(picoquic_cubic_state_t* cubic_state, picoquic_path_t* path_x, uint64_t current_time);

void picoquic_cubic_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, uint64_t current_time);

double picoquic_cubic_root(double x);

/* Compute W_cubic(t) = C * (t - K) ^ 3 + W_max */
double picoquic_cubic_W_cubic(
    picoquic_cubic_state_t* cubic_state,
    uint64_t current_time);

/* On entering congestion avoidance, need to compute the new coefficients
 * of the cubit curve */
void picoquic_cubic_enter_avoidance(
    picoquic_cubic_state_t* cubic_state,
    uint64_t current_time);

/* The recovery state last 1 RTT, during which parameters will be frozen
 */
void picoquic_cubic_enter_recovery(picoquic_cnx_t * cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_cubic_state_t* cubic_state,
    uint64_t current_time);

/* On spurious repeat notification, restore the previous congestion control.
 * Assume that K is still valid -- we only update it after exiting recovery.
 * Set cwin to the value of W_max before the recovery event
 * Set W_max to W_max_last, i.e. the value before the recovery event
 * Set the epoch back to where it was, by computing the inverse of the
 * W_cubic formula */
void picoquic_cubic_correct_spurious(picoquic_path_t* path_x,
    picoquic_cubic_state_t* cubic_state,
    uint64_t current_time);

void cubic_update_bandwidth(picoquic_path_t* path_x);

/*
 * Properly implementing Cubic requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
void picoquic_cubic_notify(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time);

/* Exit slow start on either long delay of high loss
 */
void dcubic_exit_slow_start(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_cubic_state_t* cubic_state,
    uint64_t current_time);

/*
 * Define delay-based Cubic, dcubic, and alternative congestion control protocol similar to Cubic but
 * using delay measurements instead of reacting to packet losses. This is a quic hack, intended for
 * trials of a lossy satellite networks.
 */
void picoquic_dcubic_notify(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time);


/* Release the state of the congestion control algorithm */
void picoquic_cubic_delete(picoquic_path_t* path_x);

/* Observe the state of congestion control */
void picoquic_cubic_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param);


#endif //PICOQUIC_CUBIC_H
