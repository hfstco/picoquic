/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <string.h>
#include "picoquic_internal.h"
#include "cc_common.h"
#include "newreno.h"

#define NB_RTT_RENO 4

/* Many congestion control algorithms run a parallel version of new reno in order
 * to provide a lower bound estimate of either the congestion window or the
 * the minimal bandwidth. This implementation of new reno does not directly
 * refer to the connection and path variables (e.g. cwin) but instead sets
 * its entire state in memory.
 */

void picoquic_newreno_sim_reset(picoquic_newreno_sim_state_t *nrss, picoquic_path_t *path_x, uint64_t current_time) {
    CC_DEBUG_DUMP("picoquic_newreno_sim_reset()\n");
    /* Initialize the state of the congestion control algorithm */
    memset(nrss, 0, sizeof(picoquic_newreno_sim_state_t));
    nrss->alg_state = picoquic_newreno_alg_slow_start;
    nrss->ssthresh = UINT64_MAX;
    nrss->cwin = PICOQUIC_CWIN_INITIAL;
    picoquic_cr_reset(&nrss->cr_state, path_x, current_time);
    CC_DEBUG_DUMP("ssthresh=%" PRIu64 ", cwin=%" PRIu64 "\n", nrss->ssthresh, nrss->cwin);
}

/* The recovery state last 1 RTT, during which parameters will be frozen
 */
static void picoquic_newreno_sim_enter_recovery(
    picoquic_newreno_sim_state_t *nr_state,
    picoquic_cnx_t *cnx,
    picoquic_path_t *path_x,
    picoquic_congestion_notification_t notification,
    uint64_t current_time) {
    CC_DEBUG_PRINTF(path_x, "picoquic_newreno_sim_enter_recovery(unique_path_id=%" PRIu64 ", notification=%d)\n",
                    path_x->unique_path_id, notification);
    nr_state->ssthresh = nr_state->cwin / 2;
    if (nr_state->ssthresh < PICOQUIC_CWIN_MINIMUM) {
        nr_state->ssthresh = PICOQUIC_CWIN_MINIMUM;
        CC_DEBUG_DUMP("ssthresh=%" PRIu64 "\n", nr_state->ssthresh);
    }

    if (notification == picoquic_congestion_notification_timeout) {
        nr_state->cwin = PICOQUIC_CWIN_MINIMUM;
        nr_state->alg_state = picoquic_newreno_alg_slow_start;
        CC_DEBUG_DUMP("cwin=%" PRIu64 ", alg_state=%d\n", nr_state->cwin, nr_state->alg_state);
    } else {
        nr_state->cwin = nr_state->ssthresh;
        nr_state->alg_state = picoquic_newreno_alg_congestion_avoidance;
        CC_DEBUG_DUMP("cwin=%" PRIu64 ", alg_state=%d\n", nr_state->cwin, nr_state->alg_state);
    }

    nr_state->recovery_start = current_time;
    nr_state->recovery_sequence = picoquic_cc_get_sequence_number(cnx, path_x);
    nr_state->residual_ack = 0;
}

/* Update cwin per signaled bandwidth
 */
/*static void picoquic_newreno_sim_seed_cwin(picoquic_newreno_sim_state_t* nr_state,
    picoquic_path_t* path_x, uint64_t bytes_in_flight)
{
    if (nr_state->alg_state == picoquic_newreno_alg_slow_start &&
        nr_state->ssthresh == UINT64_MAX) {
        if (bytes_in_flight > nr_state->cwin) {
            nr_state->cwin = bytes_in_flight;
            nr_state->ssthresh = bytes_in_flight;
            nr_state->alg_state = picoquic_newreno_alg_congestion_avoidance;
        }
    }
}*/


/* Notification API for new Reno simulations.
 */
void picoquic_newreno_sim_notify(
    picoquic_newreno_sim_state_t *nr_state,
    picoquic_cnx_t *cnx,
    picoquic_path_t *path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t *ack_state,
    uint64_t current_time) {
    switch (notification) {
        case picoquic_congestion_notification_acknowledgement: {
            switch (nr_state->alg_state) {
                case picoquic_newreno_alg_slow_start:
                    /* +------+---------+---------+------------+-----------+------------+
                     * |Phase |Normal   |Recon.   |Unvalidated |Validating |Safe Retreat|
                     * +------+---------+---------+------------+-----------+------------+
                     * |CWND: |When in  |CWND     |CWND is not |CWND can   |CWND is not |
                     * |      |observe, |increases|increased   |increase   |increased   |
                     * |      |measure  |using SS |            |using SS   |            |
                     * |      |sav_cwnd |         |            |           |            |
                     * +------+---------+---------+------------+-----------+------------+
                     */
                    if (nr_state->cr_state.alg_state == picoquic_cr_alg_observe ||
                        nr_state->cr_state.alg_state == picoquic_cr_alg_recon ||
                        nr_state->cr_state.alg_state == picoquic_cr_alg_validating ||
                        nr_state->cr_state.alg_state == picoquic_cr_alg_normal) {
                        nr_state->cwin += ack_state->nb_bytes_acknowledged;
                        nr_state->cr_state.cwin = nr_state->cwin;
                        CC_DEBUG_DUMP("cwin=%" PRIu64 "\n", nr_state->cwin);

                        /* if cnx->cwin exceeds SSTHRESH, exit and go to CA */
                        if (nr_state->cwin >= nr_state->ssthresh) {
                            CC_DEBUG_DUMP("SLOW START -> AVOIDANCE");
                            nr_state->alg_state = picoquic_newreno_alg_congestion_avoidance;
                            CC_DEBUG_DUMP("alg_state=%d\n", nr_state->alg_state);
                        }
                    }

                /* move up into path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time */
                    picoquic_cr_notify(&nr_state->cr_state, cnx, path_x, notification, ack_state, current_time);
                /* *Safe Retreat Phase (Tracking PipeSize): The sender continues to
                 *  update the PipeSize after processing each ACK. This value is used
                 *  to reset the ssthresh when leaving this phase, it does not modify
                 *  CWND.
                 */
                /* *Safe Retreat Phase (Receiving acknowledgement for all unvalidated
                 *  packets): The sender enters Normal Phase when the last packet (or
                 *  later) sent during the Unvalidated Phase has been acknowledged.
                 *  The sender MUST set the ssthresh to no more than the PipeSize.
                 */
                /* If (last unvalidated packet is ACKed}, ssthresh=PS and then enter Normal */
                    nr_state->ssthresh = nr_state->cr_state.ssthresh;
                    nr_state->cwin = nr_state->cr_state.cwin;
                    break;
                case picoquic_newreno_alg_congestion_avoidance:
                default: {
                    uint64_t complete_delta = ack_state->nb_bytes_acknowledged * path_x->send_mtu + nr_state->
                                              residual_ack;
                    nr_state->residual_ack = complete_delta % nr_state->cwin;
                    nr_state->cwin += complete_delta / nr_state->cwin;
                    nr_state->cr_state.cwin = nr_state->cwin;
                    CC_DEBUG_DUMP("residual_ack=%" PRIu64 ", cwin=%" PRIu64 "\n", nr_state->residual_ack,
                                  nr_state->cwin);

                    picoquic_cr_notify(&nr_state->cr_state, cnx, path_x, notification, ack_state, current_time);
                    nr_state->ssthresh = nr_state->cr_state.ssthresh;
                    nr_state->cwin = nr_state->cr_state.cwin;
                    break;
                }
            }
            break;
        }
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_timeout:
            /* +------+---------+---------+------------+-----------+------------+
             * |Phase |Normal   |Recon.   |Unvalidated |Validating |Safe Retreat|
             * +------+---------+---------+------------+-----------+------------+
             * |If    |Normal   |Normal   |          Enter         |      -     |
             * |loss  |CC method|CC method|          Safe          |            |
             * |or    |         |CR is not|          Retreat       |            |
             * |ECNCE:|         |allowed  |                        |            |
             * +------+---------+---------+------------+-----------+------------+
             */
            if (nr_state->cr_state.alg_state == picoquic_cr_alg_observe ||
                nr_state->cr_state.alg_state == picoquic_cr_alg_recon ||
                nr_state->cr_state.alg_state == picoquic_cr_alg_normal) {
                /* if the loss happened in this period, enter recovery */
                if (nr_state->recovery_sequence <= ack_state->lost_packet_number) {
                    picoquic_newreno_sim_enter_recovery(nr_state, cnx, path_x, notification, current_time);
                }
            }

            picoquic_cr_notify(&nr_state->cr_state, cnx, path_x, notification, ack_state, current_time);
            nr_state->ssthresh = nr_state->cr_state.ssthresh;
            nr_state->cwin = nr_state->cr_state.cwin;
            break;
        case picoquic_congestion_notification_spurious_repeat:
            if (nr_state->cr_state.alg_state == picoquic_cr_alg_observe ||
                nr_state->cr_state.alg_state == picoquic_cr_alg_recon ||
                nr_state->cr_state.alg_state == picoquic_cr_alg_normal) {
                if (!cnx->is_multipath_enabled) {
                    if (current_time - nr_state->recovery_start < path_x->smoothed_rtt &&
                        nr_state->recovery_sequence > picoquic_cc_get_ack_number(cnx, path_x)) {
                        /* If spurious repeat of initial loss detected,
                         * exit recovery and reset threshold to pre-entry cwin.
                         */
                        if (nr_state->ssthresh != UINT64_MAX &&
                            nr_state->cwin < 2 * nr_state->ssthresh) {
                            nr_state->cwin = 2 * nr_state->ssthresh;
                            nr_state->cr_state.cwin = nr_state->cwin;
                            nr_state->alg_state = picoquic_newreno_alg_congestion_avoidance;
                            CC_DEBUG_DUMP("cwin=%" PRIu64 ", alg_state=%d\n", nr_state->cwin, nr_state->alg_state);
                        }
                    }
                } else {
                    if (current_time - nr_state->recovery_start < path_x->smoothed_rtt &&
                        nr_state->recovery_start > picoquic_cc_get_ack_sent_time(cnx, path_x)) {
                        /* If spurious repeat of initial loss detected,
                         * exit recovery and reset threshold to pre-entry cwin.
                         */
                        if (nr_state->ssthresh != UINT64_MAX &&
                            nr_state->cwin < 2 * nr_state->ssthresh) {
                            nr_state->cwin = 2 * nr_state->ssthresh;
                            nr_state->cr_state.cwin = nr_state->cwin;
                            nr_state->alg_state = picoquic_newreno_alg_congestion_avoidance;
                            CC_DEBUG_DUMP("cwin=%" PRIu64 ", alg_state=%d\n", nr_state->cwin, nr_state->alg_state);
                        }
                    }
                }
            }
            break;
        case picoquic_congestion_notification_reset:
            picoquic_newreno_sim_reset(nr_state, path_x, current_time);
            break;
        case picoquic_congestion_notification_seed_cwin:
            /*picoquic_newreno_sim_seed_cwin(nr_state, path_x, ack_state->nb_bytes_acknowledged);*/
            picoquic_cr_notify(&nr_state->cr_state, cnx, path_x, notification, ack_state, current_time);
            break;
        case picoquic_congestion_notification_cwin_blocked:
            picoquic_cr_notify(&nr_state->cr_state, cnx, path_x, notification, ack_state, current_time);
            nr_state->cwin = nr_state->cr_state.cwin;
            break;
        default:
            /* ignore */
            break;
    }
}


/* Actual implementation of New Reno, when used as a stand alone algorithm
 */

void picoquic_newreno_reset(picoquic_newreno_state_t *nr_state, picoquic_path_t *path_x, uint64_t current_time) {
    CC_DEBUG_PRINTF(path_x, "picoquic_newreno_reset(unique_path_id=%" PRIu64 ")\n", path_x->unique_path_id);
    memset(nr_state, 0, sizeof(picoquic_newreno_state_t));
    picoquic_newreno_sim_reset(&nr_state->nrss, path_x, current_time);
    path_x->cwin = nr_state->nrss.cwin;
}

void picoquic_newreno_init(picoquic_cnx_t *cnx, picoquic_path_t *path_x, uint64_t current_time) {
    CC_DEBUG_PRINTF(path_x, "picoquic_newreno_init(unique_path_id=%" PRIu64 ")\n", path_x->unique_path_id);
    /* Initialize the state of the congestion control algorithm */
    picoquic_newreno_state_t *nr_state = (picoquic_newreno_state_t *) malloc(sizeof(picoquic_newreno_state_t));
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(current_time);
    UNREFERENCED_PARAMETER(cnx);
#endif

    if (nr_state != NULL) {
        picoquic_newreno_reset(nr_state, path_x, current_time);
        path_x->congestion_alg_state = nr_state;
    } else {
        path_x->congestion_alg_state = NULL;
    }
}

/*
 * Properly implementing New Reno requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
void picoquic_newreno_notify(
    picoquic_cnx_t *cnx,
    picoquic_path_t *path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t *ack_state,
    uint64_t current_time) {
    picoquic_newreno_state_t *nr_state = (picoquic_newreno_state_t *) path_x->congestion_alg_state;

    path_x->is_cc_data_updated = 1;

    if (nr_state != NULL) {
        switch (notification) {
            case picoquic_congestion_notification_acknowledgement:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_acknowledgement\n");
                CC_DEBUG_DUMP("nb_bytes_acknowledged=%" PRIu64 "\n", ack_state->nb_bytes_acknowledged);

            /* In slow start we estimate the bandwidth and jump. But we don't want to jump. */
            /*if (nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
                            nr_state->nrss.ssthresh == UINT64_MAX) {*/
            /* RTT measurements will happen before acknowledgement is signalled */
            /*uint64_t max_win = path_x->peak_bandwidth_estimate * path_x->smoothed_rtt / 1000000;
            uint64_t min_win = max_win /= 2;
            if (nr_state->nrss.cwin < min_win) {
                nr_state->nrss.cwin = min_win;
                path_x->cwin = min_win;
            }
        }*/

                if (path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
                    picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, ack_state, current_time);
                    path_x->cwin = nr_state->nrss.cwin;
                }
                break;
            /* TODO Three cases below can be merged together. Split only for debug log. */
            case picoquic_congestion_notification_seed_cwin:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_seed_cwin\n");
                CC_DEBUG_DUMP("seed_cwin=%" PRIu64 ", seed_rtt=%" PRIu64 "\n", ack_state->nb_bytes_acknowledged,
                              ack_state->rtt_measurement);
                picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, ack_state, current_time);
                path_x->cwin = nr_state->nrss.cwin;
                break;
            case picoquic_congestion_notification_cwin_blocked:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_cwin_blocked\n");
                picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, ack_state, current_time);
                path_x->cwin = nr_state->nrss.cwin;
                break;
            case picoquic_congestion_notification_ecn_ec:
            case picoquic_congestion_notification_repeat:
            case picoquic_congestion_notification_timeout:
                CC_DEBUG_PRINTF(
                    path_x,
                    "picoquic_congestion_notification_seed_cwin | picoquic_congestion_notification_cwin_blocked | picoquic_congestion_notification_ecn_ec | picoquic_congestion_notification_repeat | picoquic_congestion_notification_timeout\n");
                CC_DEBUG_DUMP("lost_packet_number=%" PRIu64 "\n", ack_state->lost_packet_number);
                picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, ack_state, current_time);
                path_x->cwin = nr_state->nrss.cwin;
                break;
            case picoquic_congestion_notification_spurious_repeat:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_spurious_repeat\n");
                CC_DEBUG_DUMP("lost_packet_number=%" PRIu64 "\n", ack_state->lost_packet_number);
                picoquic_newreno_sim_notify(&nr_state->nrss, cnx, path_x, notification, ack_state, current_time);
                path_x->cwin = nr_state->nrss.cwin;
                path_x->is_ssthresh_initialized = 1;
                break;
            case picoquic_congestion_notification_rtt_measurement:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_rtt_measurement\n");
                CC_DEBUG_DUMP("rtt_measurement=%" PRIu64 ", one_way_delay=%" PRIu64 "\n", ack_state->rtt_measurement,
                              ack_state->one_way_delay);
            /* Using RTT increases as signal to get out of initial slow start */
                if (nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
                    nr_state->nrss.ssthresh == UINT64_MAX) {
                    /* If our minimum rtt is larger than 100ms we calculate our min_win and jump to it. */
                    /*if (path_x->rtt_min > PICOQUIC_TARGET_RENO_RTT) {
                        uint64_t min_win;

                        if (path_x->rtt_min > PICOQUIC_TARGET_SATELLITE_RTT) {
                            min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)PICOQUIC_TARGET_SATELLITE_RTT / (double)PICOQUIC_TARGET_RENO_RTT);
                        }
                        else {*/
                    /* Increase initial CWIN for long delay links. */
                    /*min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)path_x->rtt_min / (double)PICOQUIC_TARGET_RENO_RTT);
                }
                if (min_win > nr_state->nrss.cwin) {
                    nr_state->nrss.cwin = min_win;
                    path_x->cwin = min_win;
                }
            }*/

                    if (nr_state->nrss.cr_state.alg_state == picoquic_cr_alg_observe ||
                        nr_state->nrss.cr_state.alg_state == picoquic_cr_alg_recon ||
                        nr_state->nrss.cr_state.alg_state == picoquic_cr_alg_normal) {
                        /* hystart */
                        if (picoquic_hystart_test(&nr_state->rtt_filter,
                                                  (cnx->is_time_stamp_enabled)
                                                      ? ack_state->one_way_delay
                                                      : ack_state->rtt_measurement,
                                                  cnx->path[0]->pacing.packet_time_microsec, current_time,
                                                  cnx->is_time_stamp_enabled)) {
                            /* RTT increased too much, get out of slow start! */
                            nr_state->nrss.ssthresh = nr_state->nrss.cwin;
                            nr_state->nrss.alg_state = picoquic_newreno_alg_congestion_avoidance;
                            CC_DEBUG_DUMP("ssthresh=%" PRIu64 ", alg_state=%d\n", nr_state->nrss.cwin,
                                          nr_state->nrss.alg_state);
                            path_x->cwin = nr_state->nrss.cwin;
                            path_x->is_ssthresh_initialized = 1;
                            CC_DEBUG_DUMP("hystart, cwin=%" PRIu64 "\n", path_x->cwin);
                        }
                    }
                }
                break;
            case picoquic_congestion_notification_reset:
                CC_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_reset\n");
                picoquic_newreno_reset(nr_state, path_x, current_time);
                break;
            default:
                CC_DEBUG_PRINTF(path_x, "default\n");
            /* ignore */
                break;
        }

        /* Compute pacing data */
        picoquic_update_pacing_data(cnx, path_x, nr_state->nrss.alg_state == picoquic_newreno_alg_slow_start &&
                                                 nr_state->nrss.ssthresh == UINT64_MAX);
    }
}

/* Release the state of the congestion control algorithm */
void picoquic_newreno_delete(picoquic_path_t *path_x) {
    CC_DEBUG_PRINTF(path_x, "picoquic_newreno_delete(unique_path_id=%" PRIu64 ")\n", path_x->unique_path_id);
    if (path_x->congestion_alg_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}

/* Observe the state of congestion control */

void picoquic_newreno_observe(picoquic_path_t *path_x, uint64_t *cc_state, uint64_t *cc_param) {
    picoquic_newreno_state_t *nr_state = (picoquic_newreno_state_t *) path_x->congestion_alg_state;
    *cc_state = (uint64_t) nr_state->nrss.alg_state;
    *cc_param = (nr_state->nrss.ssthresh == UINT64_MAX) ? 0 : nr_state->nrss.ssthresh;
}

/* Definition record for the New Reno algorithm */

#define PICOQUIC_NEWRENO_ID "newreno" /* NR88 */

picoquic_congestion_algorithm_t picoquic_newreno_algorithm_struct = {
    PICOQUIC_NEWRENO_ID, PICOQUIC_CC_ALGO_NUMBER_NEW_RENO,
    picoquic_newreno_init,
    picoquic_newreno_notify,
    picoquic_newreno_delete,
    picoquic_newreno_observe
};

picoquic_congestion_algorithm_t *picoquic_newreno_algorithm = &picoquic_newreno_algorithm_struct;
