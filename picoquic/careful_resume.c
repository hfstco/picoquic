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
    fprintf(stdout, "picoquic_cr_reset()\n");
    memset(cr_state, 0, sizeof(picoquic_cr_state_t));

    cr_state->saved_congestion_window = UINT64_MAX;
    cr_state->saved_rtt = UINT64_MAX;

    cr_state->first_unvalidated_packet = UINT64_MAX;
    cr_state->last_unvalidated_packet = UINT64_MAX;

    cr_state->pipesize = 0;

    cr_state->ssthresh = UINT64_MAX;

    picoquic_cr_enter_reconnaissance(cr_state, cnx, path_x, current_time);
}

/* Notify careful resume context. */
void picoquic_cr_notify(
    picoquic_cr_state_t* cr_state,
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t* ack_state,
    uint64_t current_time) {

    switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_unvalidated:
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
                    /* We react to the first ACK of the jump, which we expect after ~1RTT. In case the ACK is delayed
                     * due to buffering/reasons, the notification is delayed accordingly. This should not impact the overall
                     * behavior/performance. A more immediate approach could be to react on sending a packet, but at the
                     * moment there exists no send notification.
                     */
                    if (current_time - cr_state->start_of_epoch > path_x->rtt_min) {
                        cr_state->trigger = picoquic_cr_trigger_rtt_exceeded;
                        picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
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
                    if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state->last_unvalidated_packet) {
                        cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_acknowledged;
                        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
                    }
                    break;
                case picoquic_cr_alg_safe_retreat:
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
                    if (picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= cr_state->last_unvalidated_packet) {
                        cr_state->ssthresh = cr_state->pipesize * PICOQUIC_CR_BETA;
                        cr_state->trigger = picoquic_cr_trigger_exit_recovery;
                        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
                    }
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_timeout:
            fprintf(stdout, "packet #%" PRIu64 " sent at %" PRIu64 " lost. notification=%i \n", ack_state->lost_packet_number, ack_state->lost_packet_sent_time, notification);
            cr_state->trigger = (notification == picoquic_congestion_notification_ecn_ec) ? picoquic_cr_trigger_ECN_CE : picoquic_cr_trigger_packet_loss;
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_reconnaissance:
                    /* RECON: Normal CC method CR is not allowed */
                    /* *Reconnaissance Phase (Detected congestion): If the sender detects
                        congestion (e.g., packet loss or ECN-CE marking), the sender does
                        not use the Careful Resume method and MUST enter the Normal Phase
                        to respond to the detected congestion. */
                    picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
                    break;
                case picoquic_cr_alg_unvalidated:
                case picoquic_cr_alg_validating:
                    /* UNVAL, VALIDATING: Enter Safe Retreat */
                    /* *Validating Phase (Congestion indication): If a sender determines
                        that congestion was experienced (e.g., packet loss or ECN-CE
                        marking), Careful Resume enters the Safe Retreat Phase. */
                    picoquic_cr_enter_safe_retreat(cr_state, cnx, path_x, current_time);
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
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_reconnaissance:
                    if (cr_state->saved_congestion_window != UINT64_MAX &&
                        picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= 10) {
                        cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
                        picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
                        /* Reset cwin blocked state. */
                        cnx->cwin_blocked = 0;
                        picoquic_set_app_wake_time(cnx, current_time);
                    }
                    break;
                case picoquic_cr_alg_unvalidated:
                    /* UNVAL: If( >1 RTT has passed or FS=CWND or first unvalidated packet is ACKed), enter Validating */
                    cr_state->trigger = picoquic_cr_trigger_last_unvalidated_packet_sent;
                    picoquic_cr_enter_validating(cr_state, cnx, path_x, current_time);
                    break;
                default:
                    break;
            }
            break;
        case picoquic_congestion_notification_reset:
            picoquic_cr_reset(cr_state, cnx, path_x, current_time);
            break;
        case picoquic_congestion_notification_seed_cwin:
            switch (cr_state->alg_state) {
                case picoquic_cr_alg_reconnaissance:
                    cr_state->saved_congestion_window = ack_state->nb_bytes_acknowledged; /* saved_cwnd */
                    cr_state->saved_rtt = ack_state->rtt_measurement; /* saved_rtt */
                    fprintf(stdout, "%-30" PRIu64 "picoquic_congestion_notification_seed_cwin saved_congestion_window=%" PRIu64 ", saved_rtt=%" PRIu64 "\n",
                        current_time - cnx->start_time, cr_state->saved_congestion_window, cr_state->saved_rtt);

                    /* Jump instantly instead of waiting for picoquic_congestion_notification_cwin_blocked notification. */
                    if (cr_state->saved_congestion_window != UINT64_MAX && path_x->bytes_in_transit >= path_x->cwin &&
                        picoquic_cc_get_ack_number(cnx, path_x) != UINT64_MAX && picoquic_cc_get_ack_number(cnx, path_x) >= 10) {
                        cr_state->trigger = picoquic_cr_trigger_cwnd_limited;
                        picoquic_cr_enter_unvalidated(cr_state, cnx, path_x, current_time);
                    }
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
void picoquic_cr_enter_reconnaissance(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                 uint64_t current_time) {
    cr_state->previous_alg_state = picoquic_cr_alg_reconnaissance;
    cr_state->alg_state = picoquic_cr_alg_reconnaissance;

    cr_state->previous_start_of_epoch = cnx->start_time;
    cr_state->start_of_epoch = current_time;

    /* RECON: CWND=IW */
    path_x->cwin = PICOQUIC_CWIN_INITIAL;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    fprintf(stdout, "%-30" PRIu64 "%s", current_time - cnx->start_time, "picoquic_cr_enter_recon()\n");
}

/* Enter UNVAL phase. */
void picoquic_cr_enter_unvalidated(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                             uint64_t current_time) {
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_unvalidated;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    cr_state->first_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);

    /* UNVAL: PS=CWND */
    /* *Unvalidated Phase (Initialising PipeSize): The variable PipeSize is initialised to the flight_size on entry to
     * the Unvalidated Phase. This records the window before a jump is applied.
     */
    cr_state->pipesize = path_x->bytes_in_transit;

    /* UNVAL: CWND=jump_cwnd */
    /* *Unvalidated Phase (Setting the jump_cwnd): To avoid starving
        other flows that could have either started or increased their
        used capacity after the Observation Phase, the jump_cwnd MUST be
        no more than half of the saved_cwnd. Hence, jump_cwnd is less
        than or equal to the (saved_cwnd/2). CWND = jump_cwnd. */
    path_x->cwin = cr_state->saved_congestion_window / 2;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_unval(cwin=%" PRIu64 ", first_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
            current_time - cnx->start_time, cr_state->saved_congestion_window / 2, picoquic_cc_get_sequence_number(cnx, path_x), cr_state->trigger);
}

/* Enter VALIDATING phase. */
void picoquic_cr_enter_validating(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                                uint64_t current_time) {
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_validating;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* UNVAL: If (FS>PS)
     *              {CWND=FS}
     *           else
     *              {CWND=PS; enter normal}
     */
    /* *Validating Phase (Limiting CWND on entry): On entry to the
        Validating Phase, the CWND is set to the flight size. */
    if (path_x->bytes_in_transit > cr_state->pipesize) {
        path_x->cwin = path_x->bytes_in_transit;
    } else {
        path_x->cwin = cr_state->pipesize;
        cr_state->trigger = picoquic_cr_trigger_rate_limited;
        picoquic_cr_enter_normal(cr_state, cnx, path_x, current_time);
        return;
    }

    cr_state->last_unvalidated_packet = picoquic_cc_get_sequence_number(cnx, path_x);

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_validating(cwin=%" PRIu64 ", last_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
           current_time - cnx->start_time, path_x->cwin, cr_state->last_unvalidated_packet, cr_state->trigger);
}

/* Enter RETREAT phase. */
void picoquic_cr_enter_safe_retreat(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                               uint64_t current_time) {
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_safe_retreat;

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
    path_x->cwin = (cr_state->pipesize / 2 >= PICOQUIC_CWIN_MINIMUM) ? cr_state->pipesize / 2 : PICOQUIC_CWIN_MINIMUM;

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

    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_retreat(cwin=%" PRIu64 ", last_unvalidated_packet=%" PRIu64 ", trigger=%d)\n",
           current_time - cnx->start_time, path_x->cwin, cr_state->last_unvalidated_packet, cr_state->trigger);
}

/* Enter NORMAL phase. */
void picoquic_cr_enter_normal(picoquic_cr_state_t* cr_state, picoquic_cnx_t* cnx, picoquic_path_t* path_x,
                              uint64_t current_time) {
    cr_state->previous_alg_state = cr_state->alg_state;
    cr_state->alg_state = picoquic_cr_alg_normal;

    cr_state->previous_start_of_epoch = cr_state->start_of_epoch;
    cr_state->start_of_epoch = current_time;

    /* Notify qlog. */
    path_x->is_cr_data_updated = 1;

    fprintf(stdout, "%-30" PRIu64 "picoquic_cr_enter_normal(trigger=%d)\n",
           current_time - cnx->start_time, cr_state->trigger);
}