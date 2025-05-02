//
// Created by Matthias Hofstätter on 16.03.24.
//

#include "picoquic_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

/* Reset careful resume context & enter RECON phase. */
void picoquic_cr_reset(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x, uint64_t current_time) {
#if 1
    fprintf(stdout, "picoquic_cr_reset()\n");
#endif
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));

    cr_state->saved_congestion_window = UINT64_MAX;
    cr_state->saved_rtt = UINT64_MAX;

    cr_state->first_unvalidated_packet = UINT64_MAX;
    cr_state->last_unvalidated_packet = UINT64_MAX;

    cr_state->pipesize = 0;

    picoquic_cr_enter_reconnaissance(cr_state, cnx, path_x, current_time);
}

uint64_t picoquic_cr_ack(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_per_ack_state_t* ack_state, uint64_t current_time) {

    switch (cr_state->alg_state) {
        case picoquic_cr_alg_unvalidated:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (current_time - cr_state->start_of_epoch > path_x->rtt_min) {
                cr_state->trigger = picoquic_cr_trigger_rtt_exceeded;
                picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_cr_alg_validating:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state->last_unvalidated_packet) {
                cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_acknowledged;
                picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_cr_alg_safe_retreat:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state->last_unvalidated_packet) {
                cr_state->trigger = picoquic_cr_trigger_exit_recovery;
                picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
                return cr_state->pipesize * PICOQUIC_CR_BETA;
            }
            break;
        default:
            break;
    }

    return 0;
}

void picoquic_cr_congestion(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification, uint64_t current_time) {

    cr_state->trigger = (notification == picoquic_congestion_notification_ecn_ec) ? picoquic_cr_trigger_ECN_CE : picoquic_cr_trigger_packet_loss;

    if (cr_state->alg_state == picoquic_cr_alg_reconnaissance) {
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
    } else if (cr_state->alg_state == picoquic_cr_alg_unvalidated || cr_state->alg_state == picoquic_cr_alg_validating) {
        picoquic_cr_enter_safe_retreat(cr_state, cnx, path_x, current_time);
    }
}

void picoquic_cr_cwin_blocked(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x, uint64_t current_time) {

    switch (cr_state->alg_state) {
        case picoquic_cr_alg_reconnaissance:
            if (cr_state->saved_congestion_window != UINT64_MAX) {
                cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
                picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_cr_alg_unvalidated:
            cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_sent;
            picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
            break;
        default:
    }
}

void picoquic_cr_seed_cwin(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_per_ack_state_t* ack_state, uint64_t current_time) {

    if (ack_state->nb_bytes_acknowledged == 0) {
        cr_state->trigger = picoquic_cr_trigger_rtt_not_validated;
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
        return;
    }

    if (path_x->cwin < ack_state->nb_bytes_acknowledged / 2) {
        cr_state->saved_congestion_window = ack_state->nb_bytes_acknowledged; /* saved_cwnd */
        cr_state->saved_rtt = ack_state->rtt_measurement; /* saved_rtt */
#if 1
        fprintf(stdout, "%-30" PRIu64 "picoquic_congestion_notification_seed_cwin saved_congestion_window=%" PRIu64 ", saved_rtt=%" PRIu64 "\n",
            current_time - cnx->start_time, cr_state->saved_congestion_window, cr_state->saved_rtt);
        fflush(stdout);
#endif

        /*if (path_x->bytes_in_transit >= path_x->cwin) {
            cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
            picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
        }*/
    }
}

/* Enter RECON phase. */
void picoquic_cr_enter_reconnaissance(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                 uint64_t current_time) {
#if 1
    fprintf(stdout, "%-30" PRIu64 "%s", current_time, "picoquic_resume_enter_recon()\n");
    fflush(stdout);
#endif

    cr_state->previous_alg_state = picoquic_cr_alg_reconnaissance;
    cr_state->alg_state = picoquic_cr_alg_reconnaissance;


    cr_state->start_of_epoch = current_time;

    path_x->cwin = PICOQUIC_CWIN_INITIAL;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter UNVAL phase. */
void picoquic_cr_enter_unvalidated(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                             uint64_t current_time) {
#if 1
    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_unval(cwin=%" PRIu64 ", first_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
            current_time, cr_state->saved_congestion_window / 2, picoquic_cc_get_sequence_number(cnx, path_x), cr_state->trigger);
    fflush(stdout);
#endif

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_unvalidated;

    cr_state->start_of_epoch = current_time;

    cr_state->first_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);

    cr_state->pipesize = path_x->bytes_in_transit;

    path_x->cwin = cr_state->saved_congestion_window / 2;
    //cnx->maxdata_remote = path_x->cwin;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter VALIDATING phase. */
void picoquic_cr_enter_validating(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                uint64_t current_time) {
#if 1
    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_validating(cwin=%" PRIu64 ", last_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
           current_time, path_x->cwin, picoquic_cc_get_sequence_number(cnx, path_x) - 1, cr_state->trigger);
    fflush(stdout);
#endif

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_validating;

    cr_state->start_of_epoch = current_time;

    if (path_x->bytes_in_transit > cr_state->pipesize) {
        path_x->cwin = path_x->bytes_in_transit;
    } else {
        path_x->cwin = cr_state->pipesize;
        cr_state->trigger = picoquic_cr_trigger_rate_limited;
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
    }

    cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x) - 1;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter RETREAT phase. */
void picoquic_cr_enter_safe_retreat(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                               uint64_t current_time) {
#if 1
    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_retreat(last_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
           current_time, cr_state->last_unvalidated_packet, cr_state->trigger);
    fflush(stdout);
#endif

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_safe_retreat;

    cr_state->start_of_epoch = current_time;

    path_x->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_INITIAL) ? cr_state->pipesize / 2 : PICOQUIC_CWIN_INITIAL;

    /* Set last unvalidated packet if enter SAFE RETREAT in UNVAL and not already set when entering VALIDATING. */
    if (cr_state->last_unvalidated_packet == UINT64_MAX) {
        cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x) - 1;
    }

    /* *Safe Retreat Phase (Removing saved information): The set of saved
        CC parameters for the path are deleted, to prevent these from
        being used again by other flows. */
    /* TODO is_seeded, ip_addr, ticket, ... ? */

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter NORMAL phase. */
void picoquic_cr_enter_normal(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                              uint64_t current_time) {
#if 1
    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_normal(trigger=%d)\n",
           current_time, cr_state->trigger);
    fflush(stdout);
#endif

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_normal;

    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}