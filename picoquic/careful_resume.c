//
// Created by Matthias Hofstätter on 16.03.24.
//


#include <inttypes.h>
#include <picoquic_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

/* Reset careful resume context. */
void picoquic_cr_reset(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time) {
    CR_DEBUG_PRINTF(path_x, "picoquic_cr_reset(unique_path_id=%" PRIu64 ")\n", path_x->unique_path_id);
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));
    cr_state->previous_alg_state = picoquic_cr_alg_recon;
    /* Start in recon phase. */
    cr_state->alg_state = picoquic_cr_alg_recon;

    cr_state->saved_cwnd = UINT64_MAX;
    /* saved_rtt is not part of the careful resume state because it is part of the ticket. */
    /*cr_state->saved_rtt = UINT64_MAX; */

    cr_state->cr_mark = 0;
    cr_state->jump_cwnd = 0;

    cr_state->pipesize = 0;

    cr_state->start_of_epoch = current_time;
    cr_state->previous_start_of_epoch = 0;

    cr_state->cwin = PICOQUIC_CWIN_INITIAL;
    cr_state->ssthresh = UINT64_MAX;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}

/* Notify careful resume context. */
void picoquic_cr_notify(
    picoquic_cr_state_t* cr_state,
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t* ack_state,
    uint64_t current_time) {

    /* TODO move to cc code? */
    /* Process notification only if careful resume enabled. */
    if (!cnx->quic->use_careful_resume) {
        return;
    }

    switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_unval:
                    /* UNVAL: PS+=ACked */
                    /* *Unvalidated Phase (Receiving acknowledgements for reconnaisance
                        packets): The variable PipeSize if increased by the amount of
                        data that is acknowledged by each acknowledgment (in bytes). This
                        indicated a previously unvalidated packet has been succesfuly
                        sent over the path. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /*  UNVAL If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
                    /* *Unvalidated Phase (Receiving acknowledgements for an unvalidated
                        packet): The sender enters the Validating Phase when the first
                        acknowledgement is received for the first packet number (or
                        higher) that was sent in the Unvalidated Phase. */
                    /*  first unvalidated packet is ACKed */
                    /*  ... sender initialises the PipeSize to to the CWND (the same as the flight size, 29 packets) ...
                        ... When the first unvalidated packet is acknowledged (packet number 30) the sender enters the Validating Phase. */
                    /* TODO move (current_time - cr_state->start_of_epoch) > path_x->rtt_min anywhere else? */
                    /* We react to the first ACK of the jump, which we expect after ~1RTT. In case the ACK is delayed
                     * due to buffering/reasons, the notification is delayed accordingly. This should not impact the overall
                     * behavior/performance. A more immediate approach could be to react on sending a packet, but at the
                     * moment there exists no send notification.
                     */
                    if ((current_time - cr_state->start_of_epoch) > path_x->rtt_min || path_x->delivered > cr_state->cr_mark) {
                        if (path_x->delivered > cr_state->cr_mark) {
                            cr_state->trigger = picoquic_cr_trigger_congestion_window_limited;
                        }
                        CR_DEBUG_DUMP("(current_time - cr_state->start_of_epoch)=%" PRIu64 " > path_x->rtt_min=%" PRIu64 " || delivered=%" PRIu64 " > cr_mark=%" PRIu64 ", > 1 RTT OR first unvalidated packet ACKed\n", (current_time - cr_state->start_of_epoch), path_x->rtt_min, path_x->delivered, cr_state->cr_mark);
                        picoquic_cr_enter_validating(cr_state, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_validating:
                    /* VALIDATING: PS+=ACked */
                    /* *Validating Phase (Receiving acknowledgements for unvalidated
                        packets): The variable PipeSize if increased upon each
                        acknowledgment that indicates a packet has been successfuly sent
                        over the path. This records the validated PipeSize in bytes. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /* VALIDATING: If (last unvalidated packet is ACKed) enter Normal */
                    /* *Validating Phase (Receiving acknowledgement for all unvalidated
                        packets): The sender enters the Normal Phase when an
                        acknowledgement is received for the last packet number (or
                        higher) that was sent in the Unvalidated Phase. */
                    if (path_x->delivered >= cr_state->jump_cwnd) {
                        CR_DEBUG_DUMP("delivered=%" PRIu64 " > cr_mark=%" PRIu64 ", last packet that was sent in the "
                            "unvalidated phase ACKed\n", path_x->delivered, cr_state->jump_cwnd);
                        cr_state->trigger = picoquic_cr_trigger_cr_mark_acknowledged;
                        picoquic_cr_enter_normal(cr_state, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_retreat:
                    /* RETREAT: PS+=ACked */
                    /* *Safe Retreat Phase (Tracking PipeSize): The sender continues to
                        update the PipeSize after processing each ACK. This value is used
                        to reset the ssthresh when leaving this phase, it does not modify
                        CWND. */
                    cr_state->pipesize += ack_state->nb_bytes_acknowledged;

                    /* RETREAT: if (last unvalidated packet is ACKed), ssthresh=PS and then enter Normal */
                    /* *Safe Retreat Phase (Receiving acknowledgement for all unvalidated
                        packets): The sender enters Normal Phase when the last packet (or
                        later) sent during the Unvalidated Phase has been acknowledged.
                        The sender MUST set the ssthresh to no morethan the PipeSize. */
                    /*  The sender leaves the Safe Retreat Phase when the last packet number
                        (or higher) sent in the Unvalidated Phase is acknowledged. If the
                        last packet number is not cumulatively acknowledged, then additional
                        packets might need to be retransmitted. */
                    if (path_x->delivered + path_x->total_bytes_lost >= cr_state->jump_cwnd) {
                        CR_DEBUG_DUMP("delivered=%" PRIu64 " > cr_mark=%" PRIu64 "\n", path_x->delivered,
                            cr_state->jump_cwnd);
                        cr_state->trigger = picoquic_cr_trigger_cr_mark_acknowledged;
                        cr_state->ssthresh = cr_state->pipesize;
                        picoquic_cr_enter_normal(cr_state, path_x, current_time);
                    }
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_timeout:
            cr_state->trigger = (notification == picoquic_congestion_notification_ecn_ec) ? picoquic_cr_trigger_ECN_CE : picoquic_cr_trigger_packet_loss;
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_recon:
                    /* RECON: Normal CC method CR is not allowed */
                    /* *Reconnaissance Phase (Detected congestion): If the sender detects
                        congestion (e.g., packet loss or ECN-CE marking), the sender does
                        not use the Careful Resume method and MUST enter the Normal Phase
                        to respond to the detected congestion. */
                    picoquic_cr_enter_normal(cr_state, path_x, current_time);
                    break;
                case picoquic_cr_alg_unval:
                case picoquic_cr_alg_validating:
                    /* UNVAL, VALIDATING: Enter Safe Retreat */
                    /* *Validating Phase (Congestion indication): If a sender determines
                        that congestion was experienced (e.g., packet loss or ECN-CE
                        marking), Careful Resume enters the Safe Retreat Phase. */
                    picoquic_cr_enter_retreat(cr_state, path_x, current_time);
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_spurious_repeat:
            /* TODO resume careful resume if congestion notification was spurious? */
            break;
        case picoquic_congestion_notification_rtt_measurement:
            break;
        case picoquic_congestion_notification_cwin_blocked:
            /* path_x->bytes_in_transit >= path_x->cwin */
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_recon:
                    CR_DEBUG_DUMP("bytes_in_transit=%" PRIu64 " >= cwin=%" PRIu64 "\n", path_x->bytes_in_transit,
                        cr_state->cwin);
                    if (cr_state->saved_cwnd != UINT64_MAX) {
                        picoquic_cr_enter_unval(cr_state, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_unval:
                    /* UNVAL: If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
                    CR_DEBUG_DUMP("(current_time - start_of_epoch)=%" PRIu64 " > rtt_min=%" PRIu64
                        " || bytes_in_transit=%" PRIu64 " >= cwin=%" PRIu64 "\n",
                        current_time - cr_state->start_of_epoch, path_x->rtt_min, path_x->bytes_in_transit,
                        cr_state->cwin);
                    picoquic_cr_enter_validating(cr_state, path_x, current_time);
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_reset:
            /* careful resume state resets with congestion control algorithm. */
            break;
        case picoquic_congestion_notification_seed_cwin:
            CR_DEBUG_PRINTF(path_x, "picoquic_congestion_notification_seed_cwin\n");
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_recon:
                    cr_state->saved_cwnd = ack_state->nb_bytes_acknowledged; /* saved_cwnd */
                    cr_state->saved_rtt = ack_state->rtt_measurement; /* saved_rtt */
                    CR_DEBUG_DUMP("saved_cwnd=%" PRIu64 "\n", cr_state->saved_cwnd);

                    picoquic_cr_enter_recon(cr_state, path_x, current_time);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

/* Enter RECON phase. */
void picoquic_cr_enter_recon(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                                 uint64_t current_time) {
    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
    CR_DEBUG_PRINTF(path_x, "%s", "picoquic_resume_enter_recon()\n");

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_recon;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* RECON: CWND=IW */
    cr_state->cwin = PICOQUIC_CWIN_INITIAL;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
}

/* Enter UNVAL phase. */
void picoquic_cr_enter_unval(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                             uint64_t current_time) {
    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
    CR_DEBUG_PRINTF(path_x, "picoquic_cr_enter_unval(unique_path_id=%" PRIu64 ")\n", path_x->unique_path_id);

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_unval;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Mark lower and upper bound of the jumpwindow/unvalidated packets in bytes. */
    cr_state->cr_mark = path_x->delivered + path_x->bytes_in_transit;
    cr_state->jump_cwnd = path_x->delivered + ((cr_state->saved_cwnd / 2));

    /* UNVAL: PS=CWND */
    /* *Unvalidated Phase (Initialising PipeSize): The variable PipeSize
        if initialised to CWND on entry to the Unvalidated Phase. This
        records the value before the jump is applied. */
    cr_state->pipesize = path_x->bytes_in_transit; /* Should be the naerly the same as CWIN */

    /* UNVAL: CWND=jump_cwnd */
    /* *Unvalidated Phase (Setting the jump_cwnd): To avoid starving
        other flows that could have either started or increased their
        used capacity after the Observation Phase, the jump_cwnd MUST be
        no more than half of the saved_cwnd. Hence, jump_cwnd is less
        than or equal to the (saved_cwnd/2). CWND = jump_cwnd. */
    cr_state->cwin = cr_state->saved_cwnd / 2;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
}

/* Enter VALIDATING phase. */
void picoquic_cr_enter_validating(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                                uint64_t current_time) {
    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
    CR_DEBUG_PRINTF(path_x, "picoquic_cr_enter_validating(unique_path_id=%" PRIu64 ")\n",
           path_x->unique_path_id);

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_validating;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* VALIDATING: If (FS>PS)
     *              {CWND=FS}
     *           else
     *              {CWND=PS; enter normal}
     */
    /* *Validating Phase (Limiting CWND on entry): On entry to the
        Validating Phase, the CWND is set to the flight size. */
    if (path_x->bytes_in_transit > cr_state->pipesize) {
        cr_state->cwin = path_x->bytes_in_transit;
    } else {
        cr_state->cwin = cr_state->pipesize;
        picoquic_cr_enter_normal(cr_state, path_x, current_time);
    }

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
}

/* Enter RETREAT phase. */
void picoquic_cr_enter_retreat(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                               uint64_t current_time) {
    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
    CR_DEBUG_PRINTF(path_x, "picoquic_cr_enter_retreat(unique_path_id=%" PRIu64 ")\n",
           path_x->unique_path_id);

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_retreat;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* RETREAT: CWND=(PS/2) */
    /* *Safe Retreat Phase (Re-initializing CC): On entry, the CWND MUST
        be reduced to no more than the (PipeSize/2). This avoids
        persistent starvation by allowing capacity for other flows to
        regain their share of the total capacity. */
    /*  Unacknowledged packets that were sent in the Unvalidated Phase can
        be lost when there is congestion. Loss recovery commences using the
        reduced CWND that was set on entry to the Safe Retreat Phase. */
    /*  NOTE: On entry to the Safe Retreat Phase, the CWND can be
        significantly reduced, when there was multiple loss, recovery of
        all lost data could require multiple RTTs to complete. */
    /*  The loss is then detected (by receiving three ACKs that do not cover
        packet number 35), the sender then enters the Safe Retreat Phase
        because the window was not validated. The PipeSize at this point is
        equal to 29 + 34 = 66 packets. Assuming IW=10. The CWND is reset to
        Max(10,ps/2) = Max(10,66/2) = 33 packets. */
    cr_state->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_INITIAL)
                       ? cr_state->pipesize / 2
                       : PICOQUIC_CWIN_INITIAL;

    /* *Safe Retreat Phase (Removing saved information): The set of saved
        CC parameters for the path are deleted, to prevent these from
        being used again by other flows. */
    path_x->cnx->seed_cwin = 0;
    path_x->cnx->seed_rtt_min = 0;
    /* TODO is_seeded, ip_addr, ticket, ... ? */

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
}

/* Enter NORMAL phase. */
void picoquic_cr_enter_normal(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x,
                              uint64_t current_time) {
    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
    CR_DEBUG_PRINTF(path_x, "picoquic_cr_enter_normal(unique_path_id=%" PRIu64 ")\n",
           path_x->unique_path_id);

    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_normal;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    CR_DEBUG_DUMP("cwin=%" PRIu64 ", bytes_in_transit=%" PRIu64 ", delivered=%" PRIu64 ", rtt_min=%" PRIu64 ", "
                  "saved_cwnd=%" PRIu64 ", cr_mark=%" PRIu64 ", jump_cwnd=%" PRIu64 ", pipesize=%" PRIu64 ", ssthresh=%" PRIu64"\n",
                    cr_state->cwin, path_x->bytes_in_transit, path_x->delivered, path_x->rtt_min,
                    cr_state->saved_cwnd, cr_state->cr_mark, cr_state->jump_cwnd, cr_state->pipesize, cr_state->ssthresh);
}

void picoquic_cr_enter_observe(picoquic_cr_state_t* cr_state, picoquic_path_t* path_x, uint64_t current_time) {
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_observe;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;
}