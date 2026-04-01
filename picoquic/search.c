/*
 * Author: Christian Huitema (picoquic integration)
 * SEARCH algorithm: draft-chung-ccwg-search (Chung, Li, Claypool et al.)
 *
 * SEARCH -- Slow start Exit At Right CHokepoint
 *
 * During slow start the congestion window doubles every RTT. When link capacity
 * is reached, the actual delivery rate stops doubling. SEARCH detects this by
 * comparing curr_delv (bytes delivered in the last W bins) with prev_delv
 * (bytes delivered in the W bins ending one RTT ago). If delivery has not
 * approximately doubled (norm_diff >= THRESH), slow start exits and cwnd is
 * corrected for the ~2-RTT detection delay.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

/*
 * Ensure that bin[curr_idx % NUM_BINS] can hold the current scaled value
 * without overflowing MAX_BIN_VALUE. If needed, right-shift all bins and
 * increment scale_factor.
 */
static void search_check_scale(picoquic_search_state_t* s)
{
    while ((s->delivered >> s->scale_factor) > PICOQUIC_SEARCH_MAX_BIN_VALUE) {
        s->scale_factor++;
        for (int i = 0; i < PICOQUIC_SEARCH_NUM_BINS; i++) {
            s->bins[i] >>= 1;
        }
    }
}

/*
 * reset_search: re-initialize delivery state while preserving bin_duration and
 * current_rtt. Called when too many bin boundaries were missed.
 */
static void search_reset(picoquic_search_state_t* s, uint64_t current_time)
{
    /* If we have a valid current_rtt, recompute bin_duration */
    if (s->current_rtt > 0) {
        /* WINDOW_SIZE = 3.5 * rtt; BIN_DURATION = WINDOW_SIZE / W */
        uint64_t window_size = s->current_rtt * 7 / 2;
        s->bin_duration = window_size / PICOQUIC_SEARCH_W;
        if (s->bin_duration == 0) {
            s->bin_duration = 1;
        }
        s->missed_bin_limit = PICOQUIC_SEARCH_ALPHA *
            ((int)(s->current_rtt / s->bin_duration) + 1);
    }
    memset(s->bins, 0, sizeof(s->bins));
    s->curr_idx = -1;
    s->bin_end = 0;
    s->scale_factor = 0;
    s->delivered = 0;
}

/*
 * Advance the bin ring-buffer forward to account for elapsed time.
 * New (empty) bins inherit the last cumulative value (no new deliveries
 * during the gap). Returns 1 if a reset occurred, 0 otherwise.
 */
static int search_update_bins(picoquic_search_state_t* s, uint64_t current_time)
{
    int passed_bins = (int)((current_time - s->bin_end) / s->bin_duration) + 1;

    if (passed_bins > s->missed_bin_limit) {
        search_reset(s, current_time);
        return 1;
    }

    /* Propagate last cumulative value into each skipped / new bin */
    uint32_t last_val = (s->curr_idx >= 0) ?
        s->bins[s->curr_idx % PICOQUIC_SEARCH_NUM_BINS] : 0;

    for (int i = 0; i < passed_bins; i++) {
        s->curr_idx++;
        s->bins[s->curr_idx % PICOQUIC_SEARCH_NUM_BINS] = last_val;
    }
    s->bin_end += (uint64_t)passed_bins * s->bin_duration;
    return 0;
}

/*
 * compute_delv: delivered bytes between bin idx1 and bin idx2 (both inclusive
 * as start-of-window indices). frac in [0,1) interpolates between idx2 and
 * idx2+1 to align with sub-bin RTT remainders.
 *
 * Returns scaled delivered bytes (actual bytes = result << scale_factor).
 */
static int64_t search_compute_delv(const picoquic_search_state_t* s,
    int idx1, int idx2, double frac)
{
    int64_t v1 = (int64_t)s->bins[idx1 % PICOQUIC_SEARCH_NUM_BINS];
    int64_t v2 = (int64_t)s->bins[idx2 % PICOQUIC_SEARCH_NUM_BINS];
    int64_t d0 = v2 - v1;

    if (frac <= 0.0) {
        return d0;
    }

    int64_t v1f = (int64_t)s->bins[(idx1 + 1) % PICOQUIC_SEARCH_NUM_BINS];
    int64_t v2f = (int64_t)s->bins[(idx2 + 1) % PICOQUIC_SEARCH_NUM_BINS];
    int64_t d1 = v2f - v1f;

    /* Linear interpolation between d0 (frac=0) and d1 (frac=1) */
    return (int64_t)((double)d0 * (1.0 - frac) + (double)d1 * frac);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void picoquic_search_notify_rtt(picoquic_search_state_t* s, uint64_t rtt)
{
    s->current_rtt = rtt;

    if (!s->is_initialized && rtt > 0) {
        /* First RTT measurement: compute WINDOW_SIZE = 3.5 * rtt, BIN_DURATION = WINDOW_SIZE / W */
        uint64_t window_size = rtt * 7 / 2;
        s->bin_duration = window_size / PICOQUIC_SEARCH_W;
        if (s->bin_duration == 0) {
            s->bin_duration = 1;
        }
        /* MISSED_BIN_LIMIT = ALPHA * ceil(initial_rtt / bin_duration) */
        s->missed_bin_limit = PICOQUIC_SEARCH_ALPHA *
            ((int)(rtt / s->bin_duration) + 1);
        s->curr_idx = -1;
        s->bin_end = 0;
        s->is_initialized = 1;
    }
}

int picoquic_search_notify_ack(picoquic_search_state_t* s,
    uint64_t nb_bytes_acknowledged, uint64_t current_time,
    uint64_t* p_overshoot_bytes)
{
    *p_overshoot_bytes = 0;

    if (!s->is_initialized || s->bin_duration == 0) {
        return 0;
    }

    /* First ACK after initialisation: open the first bin */
    if (s->curr_idx < 0) {
        s->curr_idx = 0;
        s->bin_end = current_time + s->bin_duration;
        memset(s->bins, 0, sizeof(s->bins));
    }

    /* Accumulate delivered bytes */
    s->delivered += nb_bytes_acknowledged;
    search_check_scale(s);

    /* Advance bin ring-buffer if a boundary was crossed */
    if (current_time >= s->bin_end) {
        if (search_update_bins(s, current_time)) {
            /* Reset occurred – start fresh, no exit this cycle */
            return 0;
        }
    }

    /* Update the current bin with the latest cumulative scaled value */
    s->bins[s->curr_idx % PICOQUIC_SEARCH_NUM_BINS] =
        (uint32_t)(s->delivered >> s->scale_factor);

    /* ------------------------------------------------------------------
     * SEARCH check (lines 15–23 of the draft pseudocode)
     * ---------------------------------------------------------------- */
    if (s->current_rtt == 0 || s->curr_idx < 0) {
        return 0;
    }

    int rtt_bins = (int)(s->current_rtt / s->bin_duration);
    int prev_idx = s->curr_idx - rtt_bins;

    /* Need sufficient history: prev_idx > W and gap < EXTRA_BINS */
    if (prev_idx <= PICOQUIC_SEARCH_W ||
        (s->curr_idx - prev_idx) >= PICOQUIC_SEARCH_EXTRA_BINS) {
        return 0;
    }

    /* curr_delv: bytes delivered in the most recent W-bin window */
    int64_t curr_delv = search_compute_delv(s,
        s->curr_idx - PICOQUIC_SEARCH_W, s->curr_idx, 0.0);

    /* prev_delv: bytes delivered in the W-bin window ending one RTT ago,
     * with sub-bin fractional interpolation */
    double frac = (double)(s->current_rtt % s->bin_duration) /
                  (double)s->bin_duration;
    int64_t prev_delv = search_compute_delv(s,
        prev_idx - PICOQUIC_SEARCH_W, prev_idx, frac);

    if (curr_delv <= 0 || prev_delv <= 0) {
        return 0;
    }

    /* norm_diff = (2*prev_delv - curr_delv) / (2*prev_delv)
     * Exit if norm_diff >= THRESH  ⟺  (2*prev - curr)*DEN >= 2*prev*NUM   */
    int64_t expected = 2 * prev_delv;
    int64_t diff = expected - curr_delv;

    if (diff > 0 &&
        diff * (int64_t)PICOQUIC_SEARCH_THRESH_DEN >=
            expected * (int64_t)PICOQUIC_SEARCH_THRESH_NUM) {

        /* Compute overshoot: bytes delivered in the ~2-RTT detection lag */
        int rtt_bins2 = 2 * rtt_bins;
        int cong_idx = s->curr_idx - rtt_bins2;

        if (cong_idx >= 0 && cong_idx < s->curr_idx) {
            int64_t overshoot_scaled = search_compute_delv(s, cong_idx, s->curr_idx, 0.0);
            if (overshoot_scaled > 0) {
                *p_overshoot_bytes = (uint64_t)overshoot_scaled << s->scale_factor;
            }
        }
        return 1; /* exit slow start */
    }

    return 0;
}
