//
// Created by Matthias Hofstätter on 06.03.24.
//

#include <stdlib.h>
#include <string.h>
#include "picoquictest_internal.h"
#include "picoquic.h"
#include "picoquic_internal.h"
#include "autoqlog.h"

/** @name               cwnd_larger_than_jump
 *
 *  @brief              Checks if CR transitions to Normal phase when the congestion window (cwnd)/2 is larger than the
 *                      jump window.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 100,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • cwnd = 45,000 Bytes
 *                      • the connection is not app limited
 *
 *  @results_analysis   When the pevious cwnd is 80,000 Bytes, the jump window becomes 40,000. As 45,000 < 40,000 the
 *                      jump never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the cwnd to 45,000 Bytes;
 *                        ensure the rtt sample matches previous RTT and app_limited is false.
 */
/* TODO implement */
int careful_resume_cwnd_larger_than_jump_test() {
    return -1;
}

/** @name               rtt_less_than_half
 *
 *  @brief              Checks if CR transitions to Normal phase when the round-trip time (rtt_sample) is less than
 *                      half of the previous RTT.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 100,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 10 ms
 *                      • the connection is not app limited
 *
 *  @results_analysis   The RTT sample of 10ms is smaller than half the previous RTT, or 50 ms/2. Therefore the jump
 *                      never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 10 ms;
 *                        ensure the cwnd is set smaller than 40,000 Bytes and app_limited is false.
 */
/* TODO implement */
int careful_resume_rtt_less_than_half_test() {
    return -1;
}

/** @name               rtt_greater_than_10
 *
 *  @brief              Checks if CR transitions to Normal phase when the RTT is greater than 10 times the previous RTT.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 100,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 600 ms
 *                      • the connection is not app limited
 *
 *  @results_analysis   The sample RTT  of 600 ms is greater than 10 * previous RTT, or 10 * 50 = 500ms . Therefore the
 *                      jump never realizes and cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 600 ms;
 *                        ensure the cwnd is set smaller than 40,000 Bytes and app_limited is false.
 */
/* TODO implement */
int careful_resume_rtt_greater_than_10_test() {
    return -1;
}

/** @name               valid_rtt
 *
 *  @brief              Tests the transition to Unvalidated phase with valid RTT conditions.
 *
 *  @assertation        • cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20).
 *                      • the jump window is previous cwnd/2 - current cwnd = 19,500 Bytes
 *                      • pipesize is initialised to the current flightsize = 20,500 Bytes
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      • previous cwnd = 100,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • RTT sample = 60 ms
 *                      • cwnd = 20,500 Bytes
 *                      • the connection is not app limited
 *
 *  @results_analysis   The RTT sample of 60 ms is neither smaller than half the previous RTT (50ms/2), nor greater
 *                      than 10 * previous RTT = 10*50 = 500ms. As the connection is not app limited and the jump
 *                      window of 80,000/2 is greater than the cwnd of 20,500, all conditions are met and cr_state
 *                      transitions to CrState::Unvalidated.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Use the send_packet method from Resume to send a packet setting the RTT sample to 60 ms, the
 *                        cwnd to 20,500 Bytes, largest packet number sent to 20, and the app_limited parameter to
 *                        false.
 */
/* TODO implement */
int careful_resume_valid_rtt_test() {
    return -1;
}

/** @name               congestion_recon
 *
 *  @brief              Checks if CR transitions to Normal phase when a packet loss is encountered in the
 *                      Reconnaissance phase.
 *
 *  @assertation        cr_state transitions to CrState::Normal.
 *
 *  @phase              Reconnaissance
 *
 *  @prerequisites      None
 *
 *  @results_analysis   The congestion_event method signals a loss. Therefore cr_state transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms
 *                      • Call the congestion_event method from Resume, setting the largest packet number sent to 20.
 */
/* TODO implement */
int careful_resume_congestion_recon_test() {
    return -1;
}

/** @name               no_rtt_sample
 *
 *  @brief              Checks if CR stays in Reconnaisance in the absence of an RTT sample, even when the application
 *                      is cwnd limited.
 *
 *  @assertation        cr_state is CrState::Reconnaisance.
 *
 *  @phase              Reconnaisance - Integration
 *
 *  @prerequisites      • previous cwnd = 100,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • no RTT sample
 *                      • cwnd = 20,500 Bytes
 *                      • the connection is not app limited
 *
 *  @results_analysis   10 packets of 1200 Bytes are sent, for a total of 12,000B which is equal to the initial window.
 *                      As no acknowledgements have been seen there is no RTT sample for resume, although all other
 *                      jump conditions are met. Therefore the CR state does not change.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 100,000 Bytes and a previous RTT
 *                        of 50 ms.
 *                      • Initialise a Recovery instance using Reno CC, Hystart++ enabled, and Resume enabled.
 *                      • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets
 *                        with numbers 0 through 9 of 1200 Bytes in size
 */
/* TODO prerequisites define a cwnd of 20,500 Bytes, but in the result analysis we expect a initial cwin of 12,000 Bytes. */
/* TODO implement */
int careful_resume_no_rtt_sample_test() {
    return -1;
}

/* TODO implement */
int careful_resume_valid_rtt_full_reno_test() {
    return -1;
}

/** @name               valid_rtt_full_cubic
 *
 *  @brief              Checks if the cwnd has the expected value when transitioning to CrState::Unvalidated under
 *                      valid RTT conditions with Reno and Hystart++ is enabled. This is an integration test relying on
 *                      methods from Recovery to simulate sending packets and receiving acknowledgements.
 *
 *  @assertation        • cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20)
 *                      • cwnd = 40,000
 *                      • pipesize is initialised to the current flightsize = 15,000 Bytes
 *
 *  @phase              Reconnaisance - Integration
 *
 *  @prerequisites      • previous cwnd = 80,000 Bytes
 *                      • previous RTT = 50 ms
 *                      • Cubic CC enabled
 *                      • Hystart++ enabled
 *                      • IW of 12,000 Bytes
 *
 *  @results_analysis   5 packets are sent and acknowledged after 50ms, providing a sample RTT 50 ms. 16 packets of
 *                      1000 Bytes in size are further sent to make flightsize = 16 *1000 = 16,000 Bytes. The cwnd is
 *                      12 000 (initial) + 4000 = 16,000. 16,000 < 80,000/2  and therefore the jump is realised,
 *                      cr_state transitions to CrState::Unvalidated, storing the largest packet number sent, 20. cwnd
 *                      becomes 80,000/2 = 40,000 Bytes. Flightsize of 16,000 is recorded in pipesize.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms.
 *                      • Initialise a Recovery instance using Cubic CC, Hystart++ enabled, and Resume enabled.
 *                      • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets
 *                        with numbers 0 through 4 and insert acknowledgements for these packets after 50 miliseconds.
 *                      • Simulate the sending of packet with numbers 5-20, each of 1000 Bytes in size.
 */
/* TODO use hystart++ */
/* Why is the IW still 12,000 Bytes after receiving 5 ACKs? */
/* Why is the jump not realised after the 12th packet we are sending? Cause the FS=12000 after the 12th packet is sent. */
/* TODO implement */
int careful_resume_valid_rtt_full_cubic_test() {
    return -1;
}

/* TODO implement */
int careful_resume_invalid_rtt_full_test() {
    return -1;
}

/* TODO implement */
int careful_resume_cwnd_larger_than_jump_full_test() {
    return -1;
}

/* TODO implement */
int careful_resume_packet_loss_recon_full_reno_test() {
    return 0;
}

/** @name               packet_loss_recon_full_cubic
 *
 *  @brief              Checks if CR transitions to Normal phase when a packet loss is encountered in the
 *                      Reconnaissance phase, with Cubic and Hystart++  enabled. This is an  integration test relying
 *                      on methods from Recovery to simulate sending packets and receiving acknowledgements.
 *
 *  @assertation        • cr_state transitions to CrState::Normal
 *
 *  @phase              Reconnaisance - Integration
 *
 *  @prerequisites      • Cubic CC enabled
 *                      • Hystart++ enabled
 *
 *  @results_analysis   10 packets are sent in total before an RTT sample is calculated from any acknowledgements. An
 *                      acknowledgement comes missing Packet Number 5. This signals a loss, therefore cr_state
 *                      transitions to CrState::Normal.
 *
 *  @steps              • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT
 *                        of 50 ms.
 *                      • Initialise a Recovery instance using Cubic CC, Hystart++ enabled, and Resume enabled.
 *                      • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets
 *                        with numbers 0 through 9 and insert acknowledgements for packets 0-4 and 6-9 after 50 ms.
 */
/* TODO implement */
int careful_resume_packet_loss_recon_full_cubic_test() {
    return -1;
}

/** valid_rtt_full_cubic
 *
 *  Checks if the cwnd has the expected value when transitioning to CrState::Unvalidated under valid RTT conditions
 *  with Reno and Hystart++ is enabled. This is an integration test relying on methods from Recovery to simulate
 *  sending packets and receiving acknowledgements.
 *
 *  - cr_state transitions to CrState::Unvalidated with the correct cr_mark value (20)
 *  - cwnd = 40,000
 *  - pipesize is initialised to the current flightsize = 15,000 Bytes
 *
 *  Reconnaisance - Integration
 *
 *  - previous cwnd = 80,000 Bytes
 *  - previous RTT = 50 ms
 *  - Cubic CC enabled
 *  - Hystart++ enabled
 *  - IW of 12,000 Bytes
 *
 *  5 packets are sent and acknowledged after 50ms, providing a sample RTT 50 ms. 16 packets of 1000 Bytes in size are
 *  further sent to make flightsize = 16 * 1000 = 16,000 Bytes. The cwnd is still 12 000 (initial). 12,000 < 80,000/2
 *  and therefore the jump is realised, cr_state transitions to CrState::Unvalidated, storing the largest packet number
 *  sent at the time when flightsize=cwnd, 16. cwnd becomes 80,000/2 = 40,000 Bytes. Flightsize of 12,000 is recorded
 *  in pipesize.
 *
 *  • Initialise a careful resume instance with a previous cwnd of 80,000 Bytes and a previous RTT of 50 ms.
 *  • Initialise a Recovery instance using Cubic CC, Hystart++ enabled, and Resume enabled.
 *  • Call the on_packet_sent and on_ack_received methods from Recovery to simulate sending packets with numbers 0
 *    through 4 and insert acknowledgements for these packets after 50 miliseconds.
 *  • Simulate the sending of packet with numbers 5-20, each of 1000 Bytes in size.
 */

/* TODO implement */
int careful_resume_pipesize_update_unval_test() {
    return 0;
}

/* TODO implement */
int careful_resume_flighsize_smaller_than_pipesize_test() {
    return 0;
}

/* TODO implement */
int careful_resume_congestion_unval_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_unval_test() {
    return 0;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_reno_hystart_test() {
    return 0;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_reno_no_hystart_test() {
    return 0;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_cubic_hystart_test() {
    return 0;
}

/* TODO implement */
int careful_resume_pipesize_update_unval_full_rcubic_no_hystart_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_rto_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_reno_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_cubic_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_unval_full_gap_small_pipe_test() {
    return 0;
}

/* TODO implement */
int careful_resume_packet_loss_validating_test() {
    return 0;
}

/* TODO implement */
int careful_resume_flightsize_less_than_cwnd_test() {
    return 0;
}

/* TODO implement */
int careful_resume_pacing_unvalidated_test() {
    return 0;
}

int careful_resume_unit_test() {
    int ret = 0;

    ret = careful_resume_cwnd_larger_than_jump_test();
    if (ret == 0) {
        ret = careful_resume_rtt_less_than_half_test();
    }
    if (ret == 0) {
        ret = careful_resume_rtt_greater_than_10_test();
    }
    if (ret == 0) {
        ret = careful_resume_valid_rtt_test();
    }
    if (ret == 0) {
        ret = careful_resume_congestion_recon_test();
    }
    if (ret == 0) {
        ret = careful_resume_no_rtt_sample_test();
     }
    if (ret == 0) {
        ret = careful_resume_valid_rtt_full_reno_test();
    }
    if (ret == 0) {
        ret = careful_resume_valid_rtt_full_cubic_test();
    }
    if (ret == 0) {
        ret = careful_resume_invalid_rtt_full_test();
    }
    if (ret == 0) {
        ret = careful_resume_cwnd_larger_than_jump_full_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_recon_full_reno_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_recon_full_cubic_test();
    }
    if (ret == 0) {
        ret = careful_resume_pipesize_update_unval_test();
    }
    if (ret == 0) {
        ret = careful_resume_flighsize_smaller_than_pipesize_test();
    }
    if (ret == 0) {
        ret = careful_resume_congestion_unval_test();
    }
    if (ret == 0) {
        ret = careful_resume_pipesize_update_unval_full_reno_hystart_test();
    }
    if (ret == 0) {
        ret = careful_resume_pipesize_update_unval_full_reno_no_hystart_test();
    }
    if (ret == 0) {
        ret = careful_resume_pipesize_update_unval_full_cubic_hystart_test();
    }
    if (ret == 0) {
        ret = careful_resume_pipesize_update_unval_full_rcubic_no_hystart_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_unval_full_rto_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_unval_full_gap_reno_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_unval_full_gap_cubic_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_unval_full_gap_small_pipe_test();
    }
    if (ret == 0) {
        ret = careful_resume_packet_loss_validating_test();
    }
    if (ret == 0) {
        ret = careful_resume_flightsize_less_than_cwnd_test();
    }
    if (ret == 0) {
        ret = careful_resume_pacing_unvalidated_test();
    }

    return ret;
}

static int careful_resume_test_one_ex(picoquic_congestion_algorithm_t* ccalgo, size_t data_size, uint64_t saved_cwnd, uint64_t saved_rtt,
    uint64_t mbps_down, uint64_t mbps_up, uint64_t latency, uint64_t jitter, uint64_t loss_mask, uint64_t max_completion_time)
{
    int ret = 0;

    uint64_t simulated_time = 0;
    uint64_t picoseq_per_byte_up = (1000000ull * 8) / mbps_up;
    uint64_t picoseq_per_byte_down = (1000000ull * 8) / mbps_down;
    picoquic_connection_id_t initial_cid = { {0x5e, 0xed, 0, 0, 0, 0, 0, 0}, 8 };
    picoquic_test_tls_api_ctx_t* test_ctx = NULL;

    static test_api_stream_desc_t test_scenario[] = {
        { 4, 0, 0, 0 },
    };
    test_scenario[0].r_len = data_size; /* TODO */

    initial_cid.id[2] = ccalgo->congestion_algorithm_number;
    initial_cid.id[3] = (mbps_up > 0xff) ? 0xff : (uint8_t)mbps_up;
    initial_cid.id[4] = (mbps_down > 0xff) ? 0xff : (uint8_t)mbps_down;
    initial_cid.id[5] = (latency > 2550000) ? 0xff : (uint8_t)(latency / 10000);
    initial_cid.id[6] = (jitter > 255000) ? 0xff : (uint8_t)(jitter / 1000);
    initial_cid.id[7] = (loss_mask) ? 0x30 : 0x00;

    /* Define max transport parameters. */
    picoquic_tp_t transport_parameters_max;
    memset(&transport_parameters_max, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&transport_parameters_max, 0);
    transport_parameters_max.initial_max_data = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_bidi_local = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_bidi_remote = UINT64_MAX;
    transport_parameters_max.initial_max_stream_data_uni = UINT64_MAX;

    ret = tls_api_one_scenario_init_ex(&test_ctx, &simulated_time, PICOQUIC_INTERNAL_TEST_VERSION_1, &transport_parameters_max, &transport_parameters_max, &initial_cid, 0);

    if (ret == 0 && test_ctx == NULL) {
        ret = -1;
    }

    if (ret == 0) {
        picoquic_set_default_congestion_algorithm(test_ctx->qserver, ccalgo);
        picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);

        test_ctx->c_to_s_link->jitter = jitter;
        test_ctx->c_to_s_link->microsec_latency = latency;
        test_ctx->c_to_s_link->picosec_per_byte = picoseq_per_byte_up;
        test_ctx->s_to_c_link->microsec_latency = latency;
        test_ctx->s_to_c_link->picosec_per_byte = picoseq_per_byte_down;
        test_ctx->s_to_c_link->jitter = jitter;

        picoquic_set_qlog(test_ctx->qserver, ".");
        picoquic_set_qlog(test_ctx->qclient, ".");

        if (ret == 0) {
            ret = picoquic_start_client_cnx(test_ctx->cnx_client);

            if (ret == 0) {
                ret = tls_api_connection_loop(test_ctx, &loss_mask, 2 * latency, &simulated_time);
            }

            /* Disable flow control. */
            if (ret == 0) {
                test_ctx->cnx_client->maxdata_local = UINT64_MAX;
                test_ctx->cnx_client->maxdata_remote = UINT64_MAX;
                test_ctx->cnx_server->maxdata_local = UINT64_MAX;
                test_ctx->cnx_server->maxdata_remote = UINT64_MAX;
            }

            uint8_t* ip_addr;
            uint8_t ip_addr_length;
            uint64_t estimated_rtt = 2 * latency;
            uint64_t estimated_bdp = (125000ull * mbps_down) * estimated_rtt / 1000000ull;
            picoquic_get_ip_addr((struct sockaddr*)&test_ctx->client_addr, &ip_addr, &ip_addr_length);

            /* Seed cwnd & rtt */
            picoquic_seed_bandwidth(test_ctx->cnx_server, (saved_rtt == 0) ? estimated_rtt : saved_rtt, (saved_cwnd == 0) ? estimated_bdp : saved_cwnd,
                ip_addr, ip_addr_length);
            /* Enable careful resume. */
            picoquic_set_careful_resume(test_ctx->qserver, 1);

            /* Prepare to send data */
            if (ret == 0) {
                ret = test_api_init_send_recv_scenario(test_ctx, test_scenario, sizeof(test_scenario));
            }

            /* Perform a data sending loop */
            if (ret == 0) {
                ret = tls_api_data_sending_loop(test_ctx, &loss_mask, &simulated_time, 0);
            }

            if (ret == 0) {
                ret = tls_api_one_scenario_body_verify(test_ctx, &simulated_time, max_completion_time);
            }
        }
    }

    /* Free the resource, which will close the log file.
     */

    if (test_ctx != NULL) {
        tls_api_delete_ctx(test_ctx);
        test_ctx = NULL;
    }

    return ret;
}

static int careful_resume_test_one(picoquic_congestion_algorithm_t* ccalgo, size_t data_size, uint64_t mbps_down,
    uint64_t mbps_up, uint64_t latency, uint64_t jitter, uint64_t loss_mask, uint64_t max_completion_time) {
    return careful_resume_test_one_ex(ccalgo, data_size, 0, 0, mbps_down, mbps_up, latency, jitter, loss_mask,
        max_completion_time);
}

/** @name               cubic
 *
 *  @brief              Simple jump.
 */
int careful_resume_cubic_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 50000000, 50, 5, 300000, 0, 0x0, 30000000000);
}

/** @name               cubic_loss
 *
 *  @brief              Simple jump with packet loss.
 */
int careful_resume_cubic_loss_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 50000000, 50, 5, 300000, 0, 0x1, 30000000000);
}

/** @name               newreno
 *
 *  @brief              Simple jump.
 */
int careful_resume_newreno_test() {
    return careful_resume_test_one(picoquic_newreno_algorithm, 50000000, 50, 5, 300000, 0, 0x0, 30000000000);
}

/** @name               newreno_loss
 *
 *  @brief              Simple jump with packet loss.
 */
int careful_resume_newreno_loss_test() {
    return careful_resume_test_one(picoquic_newreno_algorithm, 50000000, 50, 5, 300000, 0, 0x1, 30000000000);
}

/** @name               data_limited_less_than_cwnd
 *
 *  @brief              Sender is data limited with data less than cwnd.
 */
int careful_resume_data_limited_less_than_cwnd_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 10000, 50, 5, 300000, 0, 0x0, 1850000);
}

/** @name               data_limited_in_validate
 *
 *  @brief              Sender is data limited in validating phase.
 */
int careful_resume_data_limited_in_validating_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 100000, 50, 5, 300000, 0, 0x0, 1850000);
}

/** @name               loss_in_recon
 *
 *  @brief              Packet #3 lost in recon phase.
 */
/* TODO check why seed is not set. */
int careful_resume_loss_in_recon_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 100000, 50, 5, 300000, 0, 0x2, 5750000);
}

/** @name               loss_in_validate
 *
 *  @brief              Packet #57 lost in validating phase.
 */
int careful_resume_loss_in_validating_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 100000, 50, 5, 300000, 0, 0x7, 5450000);
}

/** @name               loss_in_unval
 *
 *  @brief              Packet #4 lost in unval phase.
 */
int careful_resume_loss_in_unval_test() {
    return careful_resume_test_one(picoquic_cubic_algorithm, 100000, 50, 5, 300000, 0, 0x8, 2150000);
}

/** @name               path_changed
 *
 *  @brief              path should not be validated rtt_sample <= saved_rtt / 2 or >= saved_rtt * 10
 */
int careful_resume_path_changed_test() {
    int ret = 0;

    ret = careful_resume_test_one_ex(picoquic_cubic_algorithm, 100000, UINT64_MAX, 50000, 50, 5, 10000, 0, 0x0, 150000);
    if (ret == 0) {
        ret = careful_resume_test_one_ex(picoquic_cubic_algorithm, 100000, UINT64_MAX, 50000, 50, 5, 300000, 0, 0x0, 2850000);
    }

    return ret;
}

/* TODO */
/* careful_resume_test_one(picoquic_cubic_algorithm, 50000, 50, 5, 0, 0x40, 1000); */

/** @name               careful_resume_cubic_overshoot
 *
 *  @brief              saved_cwnd = 10 * BDP
 */
int careful_resume_cubic_overshoot_test() {
    uint64_t bdp = 750000; /* 10 Mbit/s * 600 ms = 750 kB */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm, 30000000, bdp * 10, 600000, 10, 6, 300000, 0, 0x0, 27000000);
}

/** @name               careful_resume_newreno_overshoot
 *
 *  @brief              saved_cwnd = 10 * BDP
 */
int careful_resume_newreno_overshoot_test() {
    uint64_t bdp = 750000; /* 10 Mbit/s * 600 ms = 750 kB */
    return careful_resume_test_one_ex(picoquic_newreno_algorithm, 30000000, bdp * 10, 600000, 10, 6, 300000, 0, 0x0, 27500000);
}

/** @name               careful_resume_cubic_undershoot
 *
 *  @brief              saved_cwnd = BDP / 2
 */
int careful_resume_cubic_undershoot_test() {
    uint64_t bdp = 750000; /* 10 Mbit/s * 600 ms = 750 kB */
    return careful_resume_test_one_ex(picoquic_cubic_algorithm, 30000000, bdp / 2, 600000, 10, 6, 300000, 0, 0x0, 27500000);
}

/** @name               careful_resume_cubic_undershoot
 *
 *  @brief              saved_cwnd = BDP / 2
 */
int careful_resume_newreno_undershoot_test() {
    uint64_t bdp = 750000; /* 10 Mbit/s * 600 ms = 750 kB */
    return careful_resume_test_one_ex(picoquic_newreno_algorithm, 30000000, bdp / 2, 600000, 10, 6, 300000, 0, 0x0, 27500000);
}