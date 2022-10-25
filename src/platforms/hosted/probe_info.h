/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

#ifndef PLATFORMS_HOSTED_PROBE_INFO_H
#define PLATFORMS_HOSTED_PROBE_INFO_H

#include <stddef.h>
#include "platform.h"
#include "bmp_hosted.h"

typedef struct probe_info {
	bmp_type_t type;
	uint16_t vid;
	uint16_t pid;
	const char *manufacturer;
	const char *product;
	const char *serial;
	const char *version;

	struct probe_info *next;
} probe_info_s;

probe_info_s *probe_info_add_by_serial(
	probe_info_s *list, bmp_type_t type, const char *mfr, const char *product, const char *serial, const char *version);
probe_info_s *probe_info_add_by_id(probe_info_s *const list, const bmp_type_t type, uint16_t vid, uint16_t pid,
	const char *const mfr, const char *const product, const char *const serial, const char *const version);
size_t probe_info_count(const probe_info_s *list);
void probe_info_list_free(const probe_info_s *list);

const probe_info_s *probe_info_correct_order(probe_info_s *list);
const probe_info_s *probe_info_filter(const probe_info_s *list, const char *serial, size_t position);
void probe_info_to_bmp_info(const probe_info_s *probe, bmp_info_s *info);

#endif /* PLATFORMS_HOSTED_PROBE_INFO_H */
