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
void picoquic_careful_resume_reset(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time)
{
    fprintf(stdout, "picoquic_cr_reset()\n");
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));

    cr_state->saved_congestion_window = UINT64_MAX;
    cr_state->saved_rtt = UINT64_MAX;

    cr_state->first_unvalidated_packet = UINT64_MAX;
    cr_state->last_unvalidated_packet = UINT64_MAX;

    cr_state->pipesize = 0;

    picoquic_careful_resume_enter_reconnaissance(cr_state, path_x, current_time);
}

uint64_t picoquic_careful_resume_acknowledgement(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                     picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    switch (cr_state->alg_state)
    {
        case picoquic_careful_resume_alg_unvalidated:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (current_time - cr_state->start_of_epoch > path_x->rtt_min)
            {
                cr_state->trigger = picoquic_careful_resume_trigger_rtt_exceeded;
                picoquic_careful_resume_enter_validating(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_careful_resume_alg_validating:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state
                ->last_unvalidated_packet)
            {
                cr_state->trigger = picoquic_careful_resume_trigger_last_unvalidated_packet_acknowledged;
                picoquic_careful_resume_enter_normal(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_careful_resume_alg_failed:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state
                ->last_unvalidated_packet)
            {
                cr_state->trigger = picoquic_careful_resume_trigger_exit_recovery;
                picoquic_careful_resume_enter_normal(cr_state, cnx, path_x, current_time);
                return cr_state->pipesize * PICOQUIC_CAREFUL_RESUME_BETA;
            }
            break;
        default:
            break;
    }

    return UINT64_MAX;
}

void picoquic_careful_resume_congestion(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                         picoquic_congestion_notification_t notification, uint64_t current_time)
{
    cr_state->trigger = (notification == picoquic_congestion_notification_ecn_ec)
                            ? picoquic_careful_resume_trigger_ECN_CE
                            : picoquic_careful_resume_trigger_packet_loss;
    switch (cr_state->alg_state)
    {
    case picoquic_careful_resume_alg_reconnaissance:
        picoquic_careful_resume_enter_normal(cr_state, cnx, path_x, current_time);
        break;
    case picoquic_careful_resume_alg_unvalidated:
    case picoquic_careful_resume_alg_validating:
        picoquic_careful_resume_enter_safe_retreat(cr_state, cnx, path_x, current_time);
        break;
    default:
        break;
    }
}

void picoquic_careful_resume_cwin_blocked(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                          picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    switch (cr_state->alg_state)
    {
    case picoquic_careful_resume_alg_reconnaissance:
        if (cr_state->saved_congestion_window != UINT64_MAX &&
            picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >=
            10)
        {
            cr_state->trigger = picoquic_careful_resume_trigger_connection_timeout;
            picoquic_careful_resume_enter_unvalidated(cr_state, cnx, path_x, current_time);
            /* Reset cwin blocked state. */
            cnx->cwin_blocked = 0;
            picoquic_set_app_wake_time(cnx, current_time);
        }
        break;
    case picoquic_careful_resume_alg_unvalidated:
        /* UNVAL: If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
        cr_state->trigger = picoquic_careful_resume_trigger_last_unvalidated_packet_sent;
        picoquic_careful_resume_enter_validating(cr_state, cnx, path_x, current_time);
        break;
    default:
        break;
    }
}

void picoquic_careful_resume_seed_cwin(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                  picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    if (cr_state->alg_state == picoquic_careful_resume_alg_reconnaissance)
    {
        cr_state->saved_congestion_window = ack_state->nb_bytes_acknowledged; /* saved_cwnd */
        cr_state->saved_rtt = ack_state->rtt_measurement; /* saved_rtt */

        /* Jump instantly instead of waiting for picoquic_congestion_notification_cwin_blocked notification. */
        if (cr_state->saved_congestion_window != UINT64_MAX && path_x->bytes_in_transit >= path_x->cwin &&
            picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= 10)
        {
            cr_state->trigger = picoquic_careful_resume_trigger_connection_timeout;
            picoquic_careful_resume_enter_unvalidated(cr_state, cnx, path_x, current_time);
        }
    }
}

/* Enter RECON phase. */
void picoquic_careful_resume_enter_reconnaissance(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time)
{
    cr_state->alg_state = picoquic_careful_resume_alg_reconnaissance;

    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter UNVAL phase. */
void picoquic_careful_resume_enter_unvalidated(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                               picoquic_path_t* path_x,
                                               uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_careful_resume_alg_unvalidated;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    cr_state->first_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);
    cr_state->pipesize = path_x->bytes_in_transit;
    path_x->cwin = cr_state->saved_congestion_window / 2;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter VALIDATING phase. */
void picoquic_careful_resume_enter_validating(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                              picoquic_path_t* path_x,
                                              uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_careful_resume_alg_validating;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    if (path_x->bytes_in_transit > cr_state->pipesize)
    {
        path_x->cwin = path_x->bytes_in_transit;
    }
    else
    {
        path_x->cwin = cr_state->pipesize;
        cr_state->trigger = picoquic_careful_resume_trigger_generic_event;
        picoquic_careful_resume_enter_normal(cr_state, cnx, path_x, current_time);
        return;
    }

    cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter RETREAT phase. */
void picoquic_careful_resume_enter_safe_retreat(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                                picoquic_path_t* path_x,
                                                uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_careful_resume_alg_failed;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    path_x->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_MINIMUM)
                       ? cr_state->pipesize / 2
                       : PICOQUIC_CWIN_MINIMUM;
    if (cr_state->last_unvalidated_packet == UINT64_MAX)
    {
        cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x) - 1;
    }

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Enter NORMAL phase. */
void picoquic_careful_resume_enter_normal(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                          uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_careful_resume_alg_normal;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}
