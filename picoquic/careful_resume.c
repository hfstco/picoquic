/*
* Author: Christian Huitema
* Copyright (c) 2025, Private Octopus, Inc.
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

/*
 * Careful Resume congestion control algorithm.
 * Implements draft-ietf-tsvwg-careful-resume.
 *
 * Reuses saved bandwidth parameters (cwnd, RTT) from a prior connection to the
 * same endpoint, safely skipping the slow-start ramp-up on reconnection.
 *
 * State machine:
 *
 *  RECONNAISSANCE ─── seed_cwin ──► UNVALIDATED ─── 1st ACK ──► VALIDATING
 *        │                               │                           │
 *        │ (no seed: stays here,         │ congestion                │ congestion
 *        │  use standard SS+CA)          ▼                           ▼
 *        │                         SAFE_RETREAT ◄──────────────────┘
 *        │                               │
 *        │                               │ all unvalidated resolved
 *        │                               ▼
 *        └───────────────────────► NORMAL (standard newreno CA)
 *
 * The picoquic_seed_bandwidth() / picoquic_validate_bdp_seed() pathway in
 * timing.c fires picoquic_congestion_notification_seed_cwin once, carrying
 * the saved cwnd in ack_state->nb_bytes_acknowledged.  That notification is
 * the sole trigger to leave RECONNAISSANCE.  No changes to timing.c are needed.
 */

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"
#include "careful_resume.h"

/* -------------------------------------------------------------------------
 * Per-connection algorithm state
 * ---------------------------------------------------------------------- */

typedef struct st_picoquic_careful_resume_state_t {
    picoquic_newreno_sim_state_t nrss;  /* newreno sim: RECONNAISSANCE and NORMAL phases */
    picoquic_cr_state_t cr;             /* Careful Resume phase tracking */
    char const* option_string;          /* kept across resets */
} picoquic_careful_resume_state_t;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static uint64_t cr_max(uint64_t a, uint64_t b) { return (a > b) ? a : b; }

/*
 * Compute ssthresh from pipe_size using Beta = PICOQUIC_CR_BETA_NUM / PICOQUIC_CR_BETA_DEN.
 * Clamped to PICOQUIC_CWIN_MINIMUM.
 */
static uint64_t cr_compute_ssthresh(uint64_t pipe_size)
{
    uint64_t ssthresh = (pipe_size * PICOQUIC_CR_BETA_NUM) / PICOQUIC_CR_BETA_DEN;
    if (ssthresh < PICOQUIC_CWIN_MINIMUM) {
        ssthresh = PICOQUIC_CWIN_MINIMUM;
    }
    return ssthresh;
}

/*
 * Transition to NORMAL after Careful Resume completes (either successfully via
 * VALIDATING, or after draining in SAFE_RETREAT).
 *
 * Sets ssthresh in nrss, sets nrss.cwin to the already-adjusted path_x->cwin,
 * and picks the correct CA or SS state.
 */
static void cr_enter_normal(
    picoquic_careful_resume_state_t* state,
    picoquic_path_t* path_x,
    uint64_t ssthresh)
{
    picoquic_newreno_sim_state_t* nrss = &state->nrss;
    picoquic_cr_state_t* cr = &state->cr;

    nrss->ssthresh = ssthresh;
    nrss->cwin = path_x->cwin;
    nrss->residual_ack = 0;

    if (nrss->cwin < ssthresh) {
        nrss->alg_state = picoquic_newreno_alg_slow_start;
    } else {
        nrss->alg_state = picoquic_newreno_alg_congestion_avoidance;
    }
    path_x->is_ssthresh_initialized = 1;
    cr->cr_state = picoquic_cr_alg_normal;
}

/*
 * Enter SAFE_RETREAT: reduce cwnd to max(IW, pipe_size/2).
 * Also update pipe_size one last time from current bytes_in_transit.
 */
static void cr_enter_safe_retreat(
    picoquic_careful_resume_state_t* state,
    picoquic_path_t* path_x)
{
    picoquic_cr_state_t* cr = &state->cr;

    /* One final pipe_size sample before retreating */
    cr->pipe_size = cr_max(cr->pipe_size, path_x->bytes_in_transit);

    uint64_t retreat_cwin = cr_max(PICOQUIC_CWIN_INITIAL, cr->pipe_size / 2);
    path_x->cwin = retreat_cwin;
    cr->cr_state = picoquic_cr_alg_safe_retreat;
}

/* -------------------------------------------------------------------------
 * Reset / init helpers
 * ---------------------------------------------------------------------- */

static void careful_resume_reset(
    picoquic_careful_resume_state_t* state,
    picoquic_path_t* path_x)
{
    char const* saved_option = state->option_string;
    memset(state, 0, sizeof(picoquic_careful_resume_state_t));
    state->option_string = saved_option;

    /* Start in RECONNAISSANCE: use standard newreno slow start */
    picoquic_newreno_sim_reset(&state->nrss);
    state->cr.cr_state = picoquic_cr_alg_reconnaissance;

    path_x->cwin = state->nrss.cwin;
}

/* -------------------------------------------------------------------------
 * Init
 * ---------------------------------------------------------------------- */

static void careful_resume_init(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    char const* option_string,
    uint64_t current_time)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(cnx);
    UNREFERENCED_PARAMETER(current_time);
#endif
    picoquic_careful_resume_state_t* state =
        (picoquic_careful_resume_state_t*)malloc(sizeof(picoquic_careful_resume_state_t));

    if (state != NULL) {
        memset(state, 0, sizeof(picoquic_careful_resume_state_t));
        state->option_string = option_string;
        careful_resume_reset(state, path_x);
        path_x->congestion_alg_state = state;
    } else {
        path_x->congestion_alg_state = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Notify
 * ---------------------------------------------------------------------- */

static void careful_resume_notify(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t* ack_state,
    uint64_t current_time)
{
    picoquic_careful_resume_state_t* state =
        (picoquic_careful_resume_state_t*)path_x->congestion_alg_state;

    if (state == NULL) {
        return;
    }

    path_x->is_cc_data_updated = 1;

    picoquic_cr_state_t* cr = &state->cr;
    picoquic_newreno_sim_state_t* nrss = &state->nrss;
    int in_slow_start = 0; /* used for pacing at the end */

    switch (notification) {

    /* ------------------------------------------------------------------
     * Seed notification: timing.c validated IP + RTT; enter UNVALIDATED
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_seed_cwin:
        if (cr->cr_state == picoquic_cr_alg_reconnaissance) {
            uint64_t saved_cwnd = ack_state->nb_bytes_acknowledged;
            /* jump_cwnd = saved_cwnd / 2, clamped to Initial Window */
            cr->jump_cwnd = saved_cwnd / 2;
            if (cr->jump_cwnd < PICOQUIC_CWIN_INITIAL) {
                cr->jump_cwnd = PICOQUIC_CWIN_INITIAL;
            }
            path_x->cwin = cr->jump_cwnd;
            path_x->is_ssthresh_initialized = 0;
            cr->pipe_size = 0;
            cr->unvalidated_end_seq = 0;
            cr->cr_state = picoquic_cr_alg_unvalidated;
        }
        break;

    /* ------------------------------------------------------------------
     * ACK received
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_acknowledgement:

        switch (cr->cr_state) {

        case picoquic_cr_alg_reconnaissance:
            /* Standard newreno slow start / CA */
            if (path_x->last_time_acked_data_frame_sent >
                path_x->last_sender_limited_time) {
                picoquic_newreno_sim_notify(nrss, cnx, path_x, notification,
                    ack_state, current_time);
                path_x->cwin = nrss->cwin;
            }
            in_slow_start = (nrss->alg_state == picoquic_newreno_alg_slow_start &&
                nrss->ssthresh == UINT64_MAX);
            break;

        case picoquic_cr_alg_unvalidated:
            /*
             * First ACK of the unvalidated window: update pipe_size, capture
             * the end of the unvalidated sequence range, and enter VALIDATING.
             * cwnd stays at jump_cwnd — do not grow.
             */
            cr->pipe_size = cr_max(cr->pipe_size, path_x->bytes_in_transit);
            cr->unvalidated_end_seq =
                picoquic_cc_get_sequence_number(cnx, path_x);
            if (cr->unvalidated_end_seq > 0) {
                cr->unvalidated_end_seq -= 1;
            }
            cr->cr_state = picoquic_cr_alg_validating;
            /* cwnd remains path_x->cwin = jump_cwnd */
            break;

        case picoquic_cr_alg_validating:
            /* Track pipe_size; check if all unvalidated packets ACK'd */
            cr->pipe_size = cr_max(cr->pipe_size, path_x->bytes_in_transit);
            if (picoquic_cc_get_ack_number(cnx, path_x) >= cr->unvalidated_end_seq) {
                /* Success: all unvalidated ACK'd without congestion */
                uint64_t ssthresh = cr_compute_ssthresh(cr->pipe_size);
                path_x->cwin = ssthresh;
                cr_enter_normal(state, path_x, ssthresh);
            }
            /* else: stay in VALIDATING, cwnd unchanged */
            break;

        case picoquic_cr_alg_safe_retreat:
            /*
             * Continue tracking pipe_size.  Once all unvalidated packets are
             * resolved (ACK'd or lost and retransmitted with new seq numbers
             * above unvalidated_end_seq), transition to NORMAL.
             */
            cr->pipe_size = cr_max(cr->pipe_size, path_x->bytes_in_transit);
            if (picoquic_cc_get_ack_number(cnx, path_x) >= cr->unvalidated_end_seq) {
                uint64_t ssthresh = cr_compute_ssthresh(cr->pipe_size);
                /* path_x->cwin is already at max(IW, pipe_size/2) from cr_enter_safe_retreat */
                cr_enter_normal(state, path_x, ssthresh);
            }
            /* else: stay in SAFE_RETREAT, cwnd unchanged */
            break;

        case picoquic_cr_alg_normal:
            /* Standard newreno CA */
            if (path_x->last_time_acked_data_frame_sent >
                path_x->last_sender_limited_time) {
                picoquic_newreno_sim_notify(nrss, cnx, path_x, notification,
                    ack_state, current_time);
                path_x->cwin = nrss->cwin;
            }
            in_slow_start = (nrss->alg_state == picoquic_newreno_alg_slow_start &&
                nrss->ssthresh == UINT64_MAX);
            break;
        }
        break;

    /* ------------------------------------------------------------------
     * Congestion: loss or ECN-CE
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_repeat:
    case picoquic_congestion_notification_timeout:
    case picoquic_congestion_notification_ecn_ec:

        switch (cr->cr_state) {

        case picoquic_cr_alg_unvalidated:
        case picoquic_cr_alg_validating:
            /*
             * Congestion during CR: enter Safe Retreat.
             * Reduce cwnd to max(IW, pipe_size/2) immediately.
             * unvalidated_end_seq stays set; we continue draining.
             */
            cr_enter_safe_retreat(state, path_x);
            break;

        case picoquic_cr_alg_safe_retreat:
            /*
             * Additional congestion during Safe Retreat: apply standard
             * newreno recovery to the already-reduced cwnd.
             */
            picoquic_newreno_sim_notify(nrss, cnx, path_x, notification,
                ack_state, current_time);
            path_x->cwin = nrss->cwin;
            break;

        case picoquic_cr_alg_reconnaissance:
        case picoquic_cr_alg_normal:
            /* Standard newreno recovery */
            picoquic_newreno_sim_notify(nrss, cnx, path_x, notification,
                ack_state, current_time);
            path_x->cwin = nrss->cwin;
            break;
        }
        break;

    /* ------------------------------------------------------------------
     * Spurious repeat: undo the congestion response
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_spurious_repeat:
        if (cr->cr_state == picoquic_cr_alg_reconnaissance ||
            cr->cr_state == picoquic_cr_alg_normal) {
            picoquic_newreno_sim_notify(nrss, cnx, path_x, notification,
                ack_state, current_time);
            path_x->cwin = nrss->cwin;
            path_x->is_ssthresh_initialized = 1;
        }
        /* In CR phases: spurious repeat doesn't undo Safe Retreat */
        break;

    /* ------------------------------------------------------------------
     * RTT measurement: use for long-RTT cwin boost in RECONNAISSANCE
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_rtt_measurement:
        if (cr->cr_state == picoquic_cr_alg_reconnaissance &&
            nrss->alg_state == picoquic_newreno_alg_slow_start &&
            nrss->ssthresh == UINT64_MAX) {
            if (path_x->rtt_min > PICOQUIC_TARGET_RENO_RTT) {
                path_x->cwin = picoquic_cc_update_cwin_for_long_rtt(path_x);
                nrss->cwin = path_x->cwin;
            }
        }
        break;

    /* ------------------------------------------------------------------
     * Reset: re-initialize to RECONNAISSANCE
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_reset:
        careful_resume_reset(state, path_x);
        break;

    /* ------------------------------------------------------------------
     * CWIN blocked / lost feedback: no action needed
     * ------------------------------------------------------------------ */
    case picoquic_congestion_notification_cwin_blocked:
    case picoquic_congestion_notification_lost_feedback:
    default:
        break;
    }

    /*
     * Pacing:
     * - RECONNAISSANCE / NORMAL: pacing follows newreno_sim state.
     * - UNVALIDATED / VALIDATING: non-SS pacing at cwin/rtt (= jump_cwnd/rtt),
     *   satisfying the draft's ITT = RTT * MPS / jump_cwnd requirement.
     * - SAFE_RETREAT: non-SS pacing at the reduced cwin.
     */
    int pacing_in_slow_start = in_slow_start;
    if (cr->cr_state == picoquic_cr_alg_unvalidated ||
        cr->cr_state == picoquic_cr_alg_validating ||
        cr->cr_state == picoquic_cr_alg_safe_retreat) {
        pacing_in_slow_start = 0;
    }
    picoquic_update_pacing_data(cnx, path_x, pacing_in_slow_start);
}

/* -------------------------------------------------------------------------
 * Delete
 * ---------------------------------------------------------------------- */

static void careful_resume_delete(picoquic_path_t* path_x)
{
    if (path_x->congestion_alg_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Observe
 * ---------------------------------------------------------------------- */

static void careful_resume_observe(
    picoquic_path_t* path_x,
    uint64_t* cc_state,
    uint64_t* cc_param)
{
    picoquic_careful_resume_state_t* state =
        (picoquic_careful_resume_state_t*)path_x->congestion_alg_state;
    if (state != NULL) {
        *cc_state = (uint64_t)state->cr.cr_state;
        *cc_param = state->cr.jump_cwnd;
    } else {
        *cc_state = 0;
        *cc_param = 0;
    }
}

/* -------------------------------------------------------------------------
 * Algorithm descriptor
 * ---------------------------------------------------------------------- */

#define PICOQUIC_CAREFUL_RESUME_ID "careful-resume"

static picoquic_congestion_algorithm_t careful_resume_algorithm_struct = {
    PICOQUIC_CAREFUL_RESUME_ID,
    PICOQUIC_CC_ALGO_NUMBER_CAREFUL_RESUME,
    careful_resume_init,
    careful_resume_notify,
    careful_resume_delete,
    careful_resume_observe
};

picoquic_congestion_algorithm_t* picoquic_careful_resume_algorithm =
    &careful_resume_algorithm_struct;
