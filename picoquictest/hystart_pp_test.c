//
// Created by Matthias Hofstätter on 05.03.24.
//


#include <stdlib.h>
#include "picoquic.h"
#include "cc_common.h"

int hystart_pp_increase_in_ss_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    picoquic_per_ack_state_t ack_state = { 0 };
    /* nb_bytes_acknowledged set to packet_number here. */
    /* TODO maybe refactor ack_state to include packet_number. */
    ack_state.nb_bytes_acknowledged = 1262;

    if (picoquic_hystart_pp_increase(hystart_pp_state, &ack_state) != ack_state.nb_bytes_acknowledged) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_increase_in_css_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    picoquic_per_ack_state_t ack_state = { 0 };
    /* nb_bytes_acknowledged set to packet_number here. */
    /* TODO maybe refactor ack_state to include packet_number. */
    ack_state.nb_bytes_acknowledged = 1262;

    hystart_pp_state->css_baseline_min_rtt = 100000;

    if (picoquic_hystart_pp_increase(hystart_pp_state, &ack_state) != ack_state.nb_bytes_acknowledged / PICOQUIC_HYSTART_PP_CSS_GROWTH_DIVISOR) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_ss_do_nothing_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = UINT64_MAX;
    hystart_pp_state->current_round.last_round_min_rtt = UINT64_MAX;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.last_round_min_rtt = UINT64_MAX;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = UINT64_MAX;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE - 1;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != UINT64_MAX || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_ss_set_rtt_thresh_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 15000;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 4000 || hystart_pp_state->css_baseline_min_rtt != UINT64_MAX) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_ss_set_baseline_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;
    hystart_pp_state->current_round.current_round_min_rtt = 20000;
    hystart_pp_state->current_round.last_round_min_rtt = 15000;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->rtt_thresh != 4000 || hystart_pp_state->css_baseline_min_rtt != 20000) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_css_do_nothing_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->css_baseline_min_rtt = 20000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE - 1;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->css_baseline_min_rtt != 20000) {
        ret = 1;
    }

    picoquic_hystart_pp_reset(hystart_pp_state);

    hystart_pp_state->css_baseline_min_rtt = 20000;
    hystart_pp_state->current_round.current_round_min_rtt = 22000;
    hystart_pp_state->current_round.rtt_sample_count = PICOQUIC_HYSTART_PP_N_RTT_SAMPLE;

    picoquic_hystart_pp_test(hystart_pp_state);

    if (hystart_pp_state->css_baseline_min_rtt != 20000) {
        ret = 1;
    }

    return ret;
}

int hystart_pp_test_css_set_baseline_test(picoquic_hystart_pp_state_t* hystart_pp_state) {
    int ret = 0;

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

    ret = hystart_pp_increase_in_ss_test(hystart_pp_state);
    if (ret == 0) {
        ret = hystart_pp_increase_in_css_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_ss_do_nothing_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_ss_set_rtt_thresh_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_ss_set_baseline_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_css_do_nothing_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_css_set_baseline_test(hystart_pp_state);
    }
    if (ret == 0) {
        ret = hystart_pp_test_start_round_test(hystart_pp_state);
    }

    free(hystart_pp_state);

    return ret;
}
