/*
* Author: Christian Huitema
* Copyright (c) 2019, Private Octopus, Inc.
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
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"
#include <sys/param.h>

uint64_t picoquic_cc_get_sequence_number(picoquic_cnx_t* cnx, picoquic_path_t* path_x)
{
    uint64_t ret = path_x->path_packet_number;

    return ret;
}

uint64_t picoquic_cc_get_ack_number(picoquic_cnx_t* cnx, picoquic_path_t* path_x)
{
    uint64_t ret = path_x->path_packet_acked_number;

    return ret;
}

uint64_t picoquic_cc_get_ack_sent_time(picoquic_cnx_t* cnx, picoquic_path_t* path_x)
{
    uint64_t ret = path_x->path_packet_acked_time_sent;
    return ret;
}


void picoquic_filter_rtt_min_max(picoquic_min_max_rtt_t * rtt_track, uint64_t rtt)
{
    int x = rtt_track->sample_current;
    int x_max;


    rtt_track->samples[x] = rtt;

    rtt_track->sample_current = x + 1;
    if (rtt_track->sample_current >= PICOQUIC_MIN_MAX_RTT_SCOPE) {
        rtt_track->is_init = 1;
        rtt_track->sample_current = 0;
    }
    
    x_max = (rtt_track->is_init) ? PICOQUIC_MIN_MAX_RTT_SCOPE : x + 1;

    rtt_track->sample_min = rtt_track->samples[0];
    rtt_track->sample_max = rtt_track->samples[0];

    for (int i = 1; i < x_max; i++) {
        if (rtt_track->samples[i] < rtt_track->sample_min) {
            rtt_track->sample_min = rtt_track->samples[i];
        } else if (rtt_track->samples[i] > rtt_track->sample_max) {
            rtt_track->sample_max = rtt_track->samples[i];
        }
    }
}

int picoquic_hystart_loss_test(picoquic_min_max_rtt_t* rtt_track, picoquic_congestion_notification_t event,
    uint64_t lost_packet_number, double error_rate_max)
{
    int ret = 0;
    uint64_t next_number = rtt_track->last_lost_packet_number;

    if (lost_packet_number > next_number) {
        if (next_number + PICOQUIC_SMOOTHED_LOSS_SCOPE < lost_packet_number) {
            next_number = lost_packet_number - PICOQUIC_SMOOTHED_LOSS_SCOPE;
        }

        while (next_number < lost_packet_number) {
            rtt_track->smoothed_drop_rate *= (1.0 - PICOQUIC_SMOOTHED_LOSS_FACTOR);
            next_number++;
        }

        rtt_track->smoothed_drop_rate += (1.0 - rtt_track->smoothed_drop_rate) * PICOQUIC_SMOOTHED_LOSS_FACTOR;
        rtt_track->last_lost_packet_number = lost_packet_number;

        switch (event) {
        case picoquic_congestion_notification_repeat:
            ret = rtt_track->smoothed_drop_rate > error_rate_max;
            break;
        case picoquic_congestion_notification_timeout:
            ret = 1;
        default:
            break;
        }
    }

    return ret;
}

int picoquic_hystart_loss_volume_test(picoquic_min_max_rtt_t* rtt_track, picoquic_congestion_notification_t event,  uint64_t nb_bytes_newly_acked, uint64_t nb_bytes_newly_lost)
{
    int ret = 0;

    rtt_track->smoothed_bytes_lost_16 -= rtt_track->smoothed_bytes_lost_16 / 16;
    rtt_track->smoothed_bytes_lost_16 += nb_bytes_newly_lost;
    rtt_track->smoothed_bytes_sent_16 -= rtt_track->smoothed_bytes_sent_16 / 16;
    rtt_track->smoothed_bytes_sent_16 += nb_bytes_newly_acked + nb_bytes_newly_lost;

    if (rtt_track->smoothed_bytes_sent_16 > 0) {
        rtt_track->smoothed_drop_rate = ((double)rtt_track->smoothed_bytes_lost_16) / ((double)rtt_track->smoothed_bytes_sent_16);
    }
    else {
        rtt_track->smoothed_drop_rate = 0;
    }

    switch (event) {
    case picoquic_congestion_notification_acknowledgement:
        ret = rtt_track->smoothed_drop_rate > PICOQUIC_SMOOTHED_LOSS_THRESHOLD;
        break;
    case picoquic_congestion_notification_timeout:
        ret = 1;
    default:
        break;
    }

    return ret;
}

int picoquic_hystart_test(picoquic_min_max_rtt_t* rtt_track, uint64_t rtt_measurement, uint64_t packet_time, uint64_t current_time, int is_one_way_delay_enabled)
{
    int ret = 0;

    if(current_time > rtt_track->last_rtt_sample_time + 1000) {
        picoquic_filter_rtt_min_max(rtt_track, rtt_measurement);
        rtt_track->last_rtt_sample_time = current_time;

        if (rtt_track->is_init) {
            uint64_t delta_max;

            if (rtt_track->rtt_filtered_min == 0 ||
                rtt_track->rtt_filtered_min > rtt_track->sample_max) {
                rtt_track->rtt_filtered_min = rtt_track->sample_max;
            }
            delta_max = rtt_track->rtt_filtered_min / 4;

            if (rtt_track->sample_min > rtt_track->rtt_filtered_min) {
                if (rtt_track->sample_min > rtt_track->rtt_filtered_min + delta_max) {
                    rtt_track->nb_rtt_excess++;
                    if (rtt_track->nb_rtt_excess >= PICOQUIC_MIN_MAX_RTT_SCOPE) {
                        /* RTT increased too much, get out of slow start! */
                        ret = 1;
                    }
                }
            }
            else {
                rtt_track->nb_rtt_excess = 0;
            }
        }
    }

    return ret;
}

void picoquic_hystart_increase(picoquic_path_t * path_x, picoquic_min_max_rtt_t* rtt_filter, uint64_t nb_delivered)
{
    path_x->cwin += nb_delivered;
}

void picoquic_hystart_pp_reset(picoquic_hystart_pp_state_t* hystart_pp_state) {
    /* lastRoundMinRTT and currentRoundMinRTT are initialized to infinity at the initialization time. currRTT is
     * the RTT sampled from the latest incoming ACK and initialized to infinity.
     *      lastRoundMinRTT = infinity
     *      currentRoundMinRTT = infinity
     *      currRTT = infinity
     */
    /* init round */
    hystart_pp_state->current_round.last_round_min_rtt = UINT64_MAX;
    hystart_pp_state->current_round.current_round_min_rtt = UINT64_MAX;
    //hystart_pp_state.curr_rtt = UINT64_MAX;
    hystart_pp_state->current_round.rtt_sample_count = 0;
    /* init state */
    hystart_pp_state->rtt_thresh = UINT64_MAX;
    hystart_pp_state->css_baseline_min_rtt = UINT64_MAX;
    hystart_pp_state->css_round_count = 0;
    hystart_pp_state->window_end = UINT64_MAX;
}

/** At the start of each round during standard slow start [RFC5681] and CSS, initialize the variables used to
 * compute the last round's and current round's minimum RTT:
 *      lastRoundMinRTT = currentRoundMinRTT
 *      currentRoundMinRTT = infinity
 *      rttSampleCount = 0
 */
void picoquic_hystart_pp_start_round(picoquic_hystart_pp_round_t* hystart_pp_round) {
    hystart_pp_round->last_round_min_rtt = hystart_pp_round->current_round_min_rtt;
    hystart_pp_round->current_round_min_rtt = UINT64_MAX;
    hystart_pp_round->rtt_sample_count = 0;
}

/** For each arriving ACK in slow start, where N is the number of previously unacknowledged bytes acknowledged in
 * the arriving ACK:
 * Update the cwnd:
 *      cwnd = cwnd + min(N, L * SMSS)
 * Keep track of the minimum observed RTT:
 *      currentRoundMinRTT = min(currentRoundMinRTT, currRTT)
 *      rttSampleCount += 1
 */
/** For each arriving ACK in CSS, where N is the number of previously unacknowledged bytes acknowledged in the arriving
 * ACK:
 * Update the cwnd:
 *      cwnd = cwnd + (min(N, L * SMSS) / CSS_GROWTH_DIVISOR)
 * Keep track of the minimum observed RTT:
 *      currentRoundMinRTT = min(currentRoundMinRTT, currRTT)
 *      rttSampleCount += 1
 */
uint64_t picoquic_hystart_pp_increase(picoquic_hystart_pp_state_t* hystart_pp_state, picoquic_per_ack_state_t* ack_state) {
    /* if css_baseline_min_rtt is NOT set (!= UINT64_MAX), then we are in SS. otherwise we are in CSS and use the
     * CSS_GROWTH_DIVISOR. We combine the two cases above in one function. The only difference between SS and CSS is the usage of the CSS_GROWTH_DIVISOR. */
    hystart_pp_state->current_round.current_round_min_rtt = MIN(hystart_pp_state->current_round.current_round_min_rtt, ack_state->rtt_measurement);
    hystart_pp_state->current_round.rtt_sample_count++;

    /* TODO check PICOQUIC_INITIAL_MTU_IPV4 */
    return MIN(ack_state->nb_bytes_acknowledged, (PICOQUIC_L == UINT64_MAX) ? UINT64_MAX : PICOQUIC_L * PICOQUIC_INITIAL_MTU_IPV4) / ((hystart_pp_state->css_baseline_min_rtt == UINT64_MAX) ? 1 : PICOQUIC_CSS_GROWTH_DIVISOR);
}

/** For rounds where at least N_RTT_SAMPLE RTT samples have been obtained and currentRoundMinRTT and lastRoundMinRTT
 * are valid, check to see if delay increase triggers slow start exit:
 *      if ((rttSampleCount >= N_RTT_SAMPLE) AND (currentRoundMinRTT != infinity) AND (lastRoundMinRTT != infinity))
 *          RttThresh = max(MIN_RTT_THRESH, min(lastRoundMinRTT / MIN_RTT_DIVISOR, MAX_RTT_THRESH))
 *          if (currentRoundMinRTT >= (lastRoundMinRTT + RttThresh))
 *              cssBaselineMinRtt = currentRoundMinRTT
 *              exit slow start and enter CSS
 */
/** For CSS rounds where at least N_RTT_SAMPLE RTT samples have been obtained, check to see if the current round's
 * minRTT drops below baseline (cssBaselineMinRtt) indicating that slow start exit was spurious:
 *      if (currentRoundMinRTT < cssBaselineMinRtt)
 *          cssBaselineMinRtt = infinity
 *          resume slow start including HyStart++
 */
void picoquic_hystart_pp_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    /* Like above we combine the two cases (SS, CSS). */
    if (hystart_pp_state->css_baseline_min_rtt == UINT64_MAX) {
        /* In slow start (SS) */
        if (hystart_pp_state->current_round.rtt_sample_count >= PICOQUIC_N_RTT_SAMPLE &&
            hystart_pp_state->current_round.current_round_min_rtt != UINT64_MAX &&
            hystart_pp_state->current_round.last_round_min_rtt != UINT64_MAX) {
            hystart_pp_state->rtt_thresh = MAX(PICOQUIC_MIN_RTT_THRESH, MIN(hystart_pp_state->current_round.last_round_min_rtt / PICOQUIC_MIN_RTT_DIVISOR, PICOQUIC_MAX_RTT_THRESH));

            if (hystart_pp_state->current_round.current_round_min_rtt >= (hystart_pp_state->current_round.last_round_min_rtt + hystart_pp_state->rtt_thresh)) {

                /* exit slow start and enter CSS */
                hystart_pp_state->css_baseline_min_rtt = hystart_pp_state->current_round.current_round_min_rtt;
                printf("exit slow start and enter CSS\n");
            }
        }
    } else {
        /* In conservative slow start (CSS) */
        if (hystart_pp_state->current_round.rtt_sample_count >= PICOQUIC_N_RTT_SAMPLE) {
            if (hystart_pp_state->current_round.current_round_min_rtt < hystart_pp_state->css_baseline_min_rtt) {

                /* resume slow start including hystart++ */
                hystart_pp_state->css_baseline_min_rtt = UINT64_MAX;
                printf("resume slow start including hystart++\n");
            }
        }
    }
}

uint64_t picoquic_cc_increased_window(picoquic_cnx_t* cnx, uint64_t previous_window)
{
    uint64_t new_window;
    if (cnx->path[0]->rtt_min <= PICOQUIC_TARGET_RENO_RTT) {
        new_window = previous_window * 2;
    }
    else {
        double w = (double)previous_window;
        w /= (double)PICOQUIC_TARGET_RENO_RTT;
        w *= (cnx->path[0]->rtt_min > PICOQUIC_TARGET_SATELLITE_RTT)? PICOQUIC_TARGET_SATELLITE_RTT: cnx->path[0]->rtt_min;
        new_window = (uint64_t)w;
    }
    return new_window;
}