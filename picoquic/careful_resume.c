/*
* Author: Matthias Hofstätter
* Copyright (c) 2025, Matthias Hofstätter
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

#include "picoquic_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

/* Reset careful resume context & enter RECON phase. */
void picoquic_cr_reset(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time)
{
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));

    cr_state->saved_congestion_window = UINT64_MAX;

    cr_state->first_unvalidated_packet = UINT64_MAX;
    cr_state->last_unvalidated_packet = UINT64_MAX;

    cr_state->pipesize = 0;

    picoquic_cr_enter_reconnaissance(cr_state, path_x, current_time);
}

uint64_t picoquic_cr_acknowledgement(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                     picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    switch (cr_state->alg_state)
    {
        case picoquic_cr_alg_unvalidated:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (current_time - cr_state->start_of_epoch > path_x->rtt_min)
            {
                cr_state->trigger = picoquic_cr_trigger_rtt_exceeded;
                picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_cr_alg_validating:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state
                ->last_unvalidated_packet)
            {
                cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_acknowledged;
                picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
            }
            break;
        case picoquic_cr_alg_safe_retreat:
            cr_state->pipesize += ack_state->nb_bytes_acknowledged;

            if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state
                ->last_unvalidated_packet)
            {
                cr_state->trigger = picoquic_cr_trigger_exit_recovery;
                picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
                return cr_state->pipesize * PICOQUIC_CR_BETA;
            }
            break;
        default:
            break;
    }

    return UINT64_MAX;
}

void picoquic_cr_congestion(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                         picoquic_congestion_notification_t notification, uint64_t current_time)
{
    cr_state->trigger = (notification == picoquic_congestion_notification_ecn_ec)
                            ? picoquic_cr_trigger_ECN_CE
                            : picoquic_cr_trigger_packet_loss;
    switch (cr_state->alg_state)
    {
    case picoquic_cr_alg_reconnaissance:
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
        break;
    case picoquic_cr_alg_unvalidated:
    case picoquic_cr_alg_validating:
        picoquic_cr_enter_safe_retreat(cr_state, cnx, path_x, current_time);
        break;
    default:
        break;
    }
}

void picoquic_cr_cwin_blocked(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                          picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    switch (cr_state->alg_state)
    {
    case picoquic_cr_alg_reconnaissance:
        if (cr_state->saved_congestion_window != UINT64_MAX &&
            picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= 10)
        {
            cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
            picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
            /* Reset cwin blocked state. */
            cnx->cwin_blocked = 0;
            picoquic_set_app_wake_time(cnx, current_time);
        }
        break;
    case picoquic_cr_alg_unvalidated:
        cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_sent;
        picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
        break;
    default:
        break;
    }
}

void picoquic_cr_seed_cwin(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                  uint64_t saved_congestion_window, uint64_t current_time)
{
    if (cr_state->alg_state == picoquic_cr_alg_reconnaissance)
    {
        cr_state->saved_congestion_window = saved_congestion_window;

        /* Jump instantly instead of waiting for picoquic_congestion_notification_cwin_blocked notification. */
        if (cr_state->saved_congestion_window != UINT64_MAX && path_x->bytes_in_transit >= path_x->cwin &&
            picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= 10)
        {
            cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
            picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
        }
    }
}

/* Enter Reconnaissance Phase. */
void picoquic_cr_enter_reconnaissance(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time)
{
    cr_state->alg_state = picoquic_cr_alg_reconnaissance;

    cr_state->start_of_epoch = current_time;
}

/* Enter Unvalidated Phase. */
void picoquic_cr_enter_unvalidated(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                               picoquic_path_t* path_x,
                                               uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_unvalidated;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    cr_state->first_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);
    cr_state->pipesize = path_x->bytes_in_transit;
    path_x->cwin = cr_state->saved_congestion_window / 2;
}

/* Enter Validating Phase. */
void picoquic_cr_enter_validating(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                              picoquic_path_t* path_x,
                                              uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_validating;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    if (path_x->bytes_in_transit > cr_state->pipesize)
    {
        path_x->cwin = path_x->bytes_in_transit;
    }
    else
    {
        path_x->cwin = cr_state->pipesize;
        cr_state->trigger = picoquic_cr_trigger_rate_limited;
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
        return;
    }

    cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);
}

/* Enter Safe Retreat Phase. */
void picoquic_cr_enter_safe_retreat(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx,
                                                picoquic_path_t* path_x,
                                                uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_safe_retreat;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    path_x->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_MINIMUM)
                       ? cr_state->pipesize / 2
                       : PICOQUIC_CWIN_MINIMUM;
    if (cr_state->last_unvalidated_packet == UINT64_MAX)
    {
        cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x) - 1;
    }
}

/* Enter Normal Phase. */
void picoquic_cr_enter_normal(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                          uint64_t current_time)
{
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_normal;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;
}
