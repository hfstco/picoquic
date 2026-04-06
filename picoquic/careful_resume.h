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

#ifndef CAREFUL_RESUME_H
#define CAREFUL_RESUME_H

#include "picoquic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Careful Resume congestion control algorithm.
 *
 * Implements draft-ietf-tsvwg-careful-resume: on reconnection to a known
 * endpoint, jumps to half the previously observed cwnd ("jump_cwnd"), paces
 * the probing burst, validates that no congestion occurs, and falls back to
 * conservative parameters if it does.
 *
 * Seeded parameters are provided via picoquic_seed_bandwidth() before the
 * connection starts. The algorithm picks them up through the existing
 * picoquic_congestion_notification_seed_cwin notification fired by timing.c
 * once the first RTT measurement confirms the path matches.
 *
 * State machine: RECONNAISSANCE → UNVALIDATED → VALIDATING → NORMAL
 *                                      └→ SAFE_RETREAT → NORMAL
 *                                                  └→ SAFE_RETREAT → NORMAL
 */
extern picoquic_congestion_algorithm_t* picoquic_careful_resume_algorithm;

#ifdef __cplusplus
}
#endif
#endif
