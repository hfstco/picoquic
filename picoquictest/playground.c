//
// Created by Hofstätter, Matthias on 06.08.24.
//

#include "picoquic_internal.h"
#include "picoquic_utils.h"
#include "tls_api.h"
#include "picoquictest_internal.h"
#ifdef _WINDOWS
#include "wincompat.h"
#endif
#include <picotls.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "picoquic_binlog.h"
#include "csv.h"
#include "qlog.h"
#include "autoqlog.h"
#include "picoquic_logger.h"
#include "performance_log.h"
#include "picoquictest.h"


static int playground_test_one(picoquic_congestion_algorithm_t* ccalgo, size_t data_size, uint64_t max_completion_time,
    uint64_t mbps_up, uint64_t mbps_down, uint64_t latency, uint64_t jitter, uint64_t buffer, int has_loss, int do_preemptive, int seed_bw, int low_flow, int flow_control)
{
    uint64_t simulated_time = 0;
    uint64_t picoseq_per_byte_up = (1000000ull * 8) / mbps_up;
    uint64_t picoseq_per_byte_down = (1000000ull * 8) / mbps_down;
    picoquic_tp_t client_parameters;
    picoquic_tp_t server_parameters;
    picoquic_connection_id_t initial_cid = { {0xca, 0xfe, 0, 0, 0, 0, 0, 0}, 8 };
    picoquic_test_tls_api_ctx_t* test_ctx = NULL;
    int ret = 0;

    initial_cid.id[2] = ccalgo->congestion_algorithm_number;
    initial_cid.id[3] = (mbps_up > 0xff) ? 0xff : (uint8_t)mbps_up;
    initial_cid.id[4] = (mbps_down > 0xff) ? 0xff : (uint8_t)mbps_down;
    initial_cid.id[5] = (latency > 2550000) ? 0xff : (uint8_t)(latency / 10000);
    initial_cid.id[6] = (jitter > 255000) ? 0xff : (uint8_t)(jitter / 1000);
    initial_cid.id[7] = (has_loss) ? 0x30 : 0x00;
    if (seed_bw) {
        initial_cid.id[7] |= 0x80;
    }
    if (do_preemptive) {
        initial_cid.id[7] |= 0x40;
    }
    if (has_loss) {
        initial_cid.id[7] |= 0x20;

    }
    if (low_flow) {
        initial_cid.id[7] |= 0x10;
    }
    if (flow_control) {
        initial_cid.id[7] |= 0x08;
    }

    memset(&client_parameters, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&client_parameters, 1);
    memset(&server_parameters, 0, sizeof(picoquic_tp_t));
    picoquic_init_transport_parameters(&server_parameters, 0);
    if (low_flow || flow_control) {
        /* For the flow control parameters to a small value */
        uint64_t bdp_s = (mbps_up * latency * 2) / 8;
        uint64_t bdp_c = (mbps_up * latency * 2) / 8;

        if (low_flow) {
            bdp_s /= 2;
            bdp_c /= 2;
        }
        else {
            bdp_s += bdp_s;
            bdp_c += bdp_c;
        }

        server_parameters.initial_max_data = bdp_s;
        client_parameters.initial_max_data = bdp_c;
    }
    ret = tls_api_one_scenario_init_ex(&test_ctx, &simulated_time, PICOQUIC_INTERNAL_TEST_VERSION_1, &client_parameters, &server_parameters, &initial_cid, 0);

    if (ret == 0 && test_ctx == NULL) {
        ret = -1;
    }

    if (ret == 0) {
        picoquic_set_default_congestion_algorithm(test_ctx->qserver, ccalgo);
        picoquic_set_congestion_algorithm(test_ctx->cnx_client, ccalgo);
        picoquic_set_preemptive_repeat_policy(test_ctx->qserver, do_preemptive);
        picoquic_set_preemptive_repeat_per_cnx(test_ctx->cnx_client, do_preemptive);


        test_ctx->c_to_s_link->jitter = jitter;
        test_ctx->c_to_s_link->microsec_latency = latency;
        test_ctx->c_to_s_link->picosec_per_byte = picoseq_per_byte_up;
        test_ctx->s_to_c_link->microsec_latency = latency;
        test_ctx->s_to_c_link->picosec_per_byte = picoseq_per_byte_down;
        test_ctx->s_to_c_link->jitter = jitter;
        test_ctx->stream0_flow_release = 1;
        test_ctx->immediate_exit = 1;

        if (seed_bw) {
            uint8_t* ip_addr;
            uint8_t ip_addr_length;
            uint64_t estimated_rtt = 2 * latency;
            uint64_t estimated_bdp = (125000ull * mbps_up) * estimated_rtt / 1000000ull;
            picoquic_get_ip_addr((struct sockaddr*)&test_ctx->server_addr, &ip_addr, &ip_addr_length);

            picoquic_seed_bandwidth(test_ctx->cnx_client, estimated_rtt, estimated_bdp,
                ip_addr, ip_addr_length);
        }

        if (low_flow || flow_control) {
            picoquic_set_max_data_control(test_ctx->qserver, server_parameters.initial_max_data);
        }

        picoquic_cnx_set_pmtud_required(test_ctx->cnx_client, 1);

        /* set the qlog on the client side */
        picoquic_set_qlog(test_ctx->qclient, ".");
        test_ctx->qclient->use_long_log = 1;
        /* Since the client connection was created before the binlog was set, force log of connection header */
        binlog_new_connection(test_ctx->cnx_client);

        /* set the qlog on the server side */
        picoquic_set_qlog(test_ctx->qserver, ".");

        if (ret == 0) {
            ret = tls_api_one_scenario_body(test_ctx, &simulated_time,
                NULL, 0, data_size, (has_loss) ? 0x10000000 : 0, 0, buffer, max_completion_time);
        }

        if (ret == 0 && do_preemptive) {
            DBG_PRINTF("Preemptive repeats: %" PRIu64, test_ctx->cnx_client->nb_preemptive_repeat);
            if (test_ctx->cnx_client->nb_preemptive_repeat == 0) {
                ret = -1;
            }
            else {
                uint64_t bdp = mbps_up * latency * 2;
                uint64_t bdp_p = bdp / (8 * test_ctx->cnx_client->path[0]->send_mtu);
                uint64_t bdp_p_plus = bdp_p + (bdp_p / 2);

                if (test_ctx->cnx_client->nb_preemptive_repeat > bdp_p_plus) {
                    DBG_PRINTF("Preemptive repeats > BDP(packets): %" PRIu64 " vs %" PRIu64,
                        test_ctx->cnx_client->nb_preemptive_repeat, bdp_p);
                    ret = -1;
                }
            }
        }

        if (ret == 0 && flow_control) {
            uint64_t bdp = mbps_up * latency * 2;
            uint64_t bdp_p = bdp/ (8 * test_ctx->cnx_client->path[0]->send_mtu);
            uint64_t nb_max = 3 * bdp_p;

            if (test_ctx->qserver->nb_data_nodes_allocated_max > nb_max){
                DBG_PRINTF("Allocated nodes: %" PRIu64 " > 3*%" PRIu64,
                    test_ctx->qserver->nb_data_nodes_allocated_max, bdp_p);
                ret = -1;
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

int playground_test()
{
    return playground_test_one(picoquic_cubic_algorithm, 100000000, UINT64_MAX, 50, 6, 300000, 0, 6000000, 0, 0, 0, 0, 0);
}