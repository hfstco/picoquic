//
// Created by Matthias Hofstätter on 14.03.24.
//

#ifndef PICOQUIC_NEWRENO_H
#define PICOQUIC_NEWRENO_H

#include "cc_common.h"

/* Actual implementation of New Reno, when used as a stand alone algorithm
 */

typedef struct st_picoquic_newreno_state_t {
    picoquic_newreno_sim_state_t nrss;
    picoquic_min_max_rtt_t rtt_filter;
} picoquic_newreno_state_t;

void picoquic_newreno_reset(picoquic_newreno_state_t* nr_state, picoquic_path_t* path_x, uint64_t current_time);
void picoquic_newreno_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, uint64_t current_time);

/*
 * Properly implementing New Reno requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
void picoquic_newreno_notify(
    picoquic_cnx_t * cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time);

/* Release the state of the congestion control algorithm */
void picoquic_newreno_delete(picoquic_path_t* path_x);

/* Observe the state of congestion control */
void picoquic_newreno_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param);

#endif //PICOQUIC_NEWRENO_H
