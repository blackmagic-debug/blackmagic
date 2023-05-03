/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H

/* Bring in the v2 protocol definitions and undefine the ADIv5 acceleration protocol */
#include "protocol_v2_defs.h"

#undef REMOTE_ADIv5_PACKET
#undef REMOTE_DP_READ
#undef REMOTE_AP_READ
#undef REMOTE_AP_WRITE
#undef REMOTE_ADIv5_RAW_ACCESS
#undef REMOTE_MEM_READ
#undef REMOTE_MEM_WRITE

/* This version of the protocol introdces proper error reporting */
#define REMOTE_ERROR_FAULT     3
#define REMOTE_ERROR_EXCEPTION 4

/* This version of the protocol completely reimplements the ADIv5 acceleration protocol message IDs */
#define REMOTE_ADIv5_PACKET     'A'
#define REMOTE_DP_READ          'd'
#define REMOTE_AP_READ          'a'
#define REMOTE_AP_WRITE         'A'
#define REMOTE_ADIv5_RAW_ACCESS 'R'
#define REMOTE_MEM_READ         'm'
#define REMOTE_MEM_WRITE        'M'

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H*/
