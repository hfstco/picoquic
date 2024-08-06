//
// Created by Matthias Hofstätter on 05.03.24.
//


#include <stdlib.h>
#include "picoquic.h"
#include "cc_common.h"

/*
 * Verify increase function.
 */
int hystart_pp_increase_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    /* SS */

    picoquic_hystart_pp_reset(hystart_pp_state);

    picoquic_per_ack_state_t ack_state = { 0 };
    /* nb_bytes_acknowledged set to packet_number here. */
    /* TODO maybe refactor ack_state to include packet_number. */
    ack_state.nb_bytes_acknowledged = 1262;

    if (picoquic_hystart_pp_keep_track_and_increase(hystart_pp_state, &ack_state) != ack_state.nb_bytes_acknowledged) {
        ret = 1;
    }

    /* CSS */

    /* Set css_baseline_min_rtt to signal be in CSS. */
    hystart_pp_state->css_baseline_min_rtt = 100000;

    if (picoquic_hystart_pp_keep_track_and_increase(hystart_pp_state, &ack_state) != ack_state.nb_bytes_acknowledged / PICOQUIC_HYSTART_PP_CSS_GROWTH_DIVISOR) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_ss_set_rtt_thresh_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    /* Verify PICOQUIC_HYSTART_PP_MIN_RTT_THRESH. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 4000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR - 1;
    hystart_pp_state->current_round.last_round_min_rtt = 4000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR - 1;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 4000 || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /* Verify PICOQUIC_HYSTART_PP_MAX_RTT_THRESH. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 16000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR + 1;
    hystart_pp_state->current_round.last_round_min_rtt = 16000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR + 1;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 16000 || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /* Verify threshold. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 10000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR;
    hystart_pp_state->current_round.last_round_min_rtt = 10000 * PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 10000 || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_ss_transition_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    /* No transition.
     * current_round_min_rtt is inf
     * and
     * last_round_min_rtt is inf.
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = UINT64_MAX;
    hystart_pp_state->current_round.last_round_min_rtt = UINT64_MAX;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /* No transition.
     * current_round_min_rtt is set
     * and
     * last_round_min_rtt is inf.
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.last_round_min_rtt = UINT64_MAX;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /* No transition.
     * current_round_min_rtt is inf
     * and
     * last_round_min_rtt is set.
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = UINT64_MAX;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /* No transition.
     * current_round_min_rtt is set
     * and
     * last_round_min_rtt is set
     * but not enough RTT samples.
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE - 1;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    /*
     * Verify transition to baseline.
     * Last round a min RTT of 40 ms is recorded. This results in a rtt_thresh of 5 ms. (40 / PICOQUIC_HYSTART_PP_MIN_RTT_DIVISOR)
     * Current RTT is 50 ms. Which is greater than 40 ms (last min RTT) + 5 ms (RTT thresh).
     * Transition to CSS should happen and css_baseline_min_rtt should be set to current RTT (50 ms).
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 50000;
    hystart_pp_state->current_round.last_round_min_rtt = 40000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 5000 || hystart_pp_state->css_baseline_min_rtt != 50000) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_css_transition_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    /* Stay in CSS. Not enough RTT samples collected in current RTT round. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->css_baseline_min_rtt = 20000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE - 1;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->css_baseline_min_rtt != 20000) {
        ret = 1;
    }

    /* Stay in CSS. Enough RTT samples but current RTT didn't drop below baseline. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->css_baseline_min_rtt = 20000;
    hystart_pp_state->current_round.current_round_min_rtt = 22000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->css_baseline_min_rtt != 20000) {
        ret = 1;
    }

    /* Transition to SS from CSS, because RTT dropped below baseline. */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->css_baseline_min_rtt = 20000;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_start_round_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    /* Start new round and expect last_round_min_rtt is set to current_round_min_rtt and current_round_min_rtt and
     * rtt_sample_count is reseted.
     */

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.current_round_min_rtt = 16000;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;

    picoquic_hystart_pp_start_round(&hystart_pp_state->current_round);

    if (hystart_pp_state->current_round.current_round_min_rtt != UINT64_MAX || hystart_pp_state->current_round.last_round_min_rtt != 16000 || hystart_pp_state->current_round.rtt_sample_count != 0) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test()
{
    int ret = 0;

    picoquic_hystart_pp_state_t* hystart_pp_state = (picoquic_hystart_pp_state_t*)malloc(sizeof(picoquic_hystart_pp_state_t));

    ret = hystart_pp_increase_test(hystart_pp_state);
    if (ret == 0) {
        ret = hystart_pp_test_ss_set_rtt_thresh_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_ss_transition_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_css_transition_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_start_round_test(hystart_pp_state);
    }

    free(hystart_pp_state);

    return ret;
}
