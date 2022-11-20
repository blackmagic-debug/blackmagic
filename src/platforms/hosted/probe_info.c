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

#include <malloc.h>
#include "probe_info.h"
#include "general.h"

probe_info_s *probe_info_add_by_serial(probe_info_s *const list, const bmp_type_t type, const char *const mfr,
	const char *const product, const char *const serial, const char *const version)
{
	return probe_info_add_by_id(list, type, 0, 0, mfr, product, serial, version) ;
}

probe_info_s *probe_info_add_by_id(probe_info_s *const list, const bmp_type_t type, uint16_t vid, uint16_t pid,
	const char *const mfr, const char *const product, const char *const serial, const char *const version)
{
	probe_info_s *probe_info = malloc(sizeof(*probe_info));
	if (!probe_info) {
		DEBUG_INFO("Fatal: Failed to allocate memory for a probe info structure\n");
		return NULL;
	}

	probe_info->type = type;
	probe_info->vid  = vid ;
	probe_info->pid = pid ;
	probe_info->manufacturer = mfr;
	probe_info->product = product;
	probe_info->serial = serial;
	probe_info->version = version;

	probe_info->next = list;
	return probe_info;}

size_t probe_info_count(const probe_info_s *const list)
{
	size_t probes = 0;
	for (const probe_info_s *probe_info = list; probe_info; probe_info = probe_info->next)
		++probes;
	return probes;
}

void probe_info_free(probe_info_s *const probe_info)
{
	free((void *)probe_info->manufacturer);
	free((void *)probe_info->product);
	free((void *)probe_info->serial);
	free((void *)probe_info->version);
	free(probe_info);
}

void probe_info_list_free(const probe_info_s *list)
{
	while (list) {
		probe_info_s *probe_info = (probe_info_s *)list;
		list = probe_info->next;
		probe_info_free(probe_info);
	}
}

const probe_info_s *probe_info_correct_order(probe_info_s *list)
{
	probe_info_s *list_head = NULL;
	while (list) {
		probe_info_s *probe_info = list->next;
		list->next = list_head;
		list_head = list;
		list = probe_info;
	}
	return list_head;
}

const probe_info_s *probe_info_filter(const probe_info_s *const list, const char *const serial, const size_t position)
{
	const probe_info_s *probe_info = list;
	for (size_t probe = 0; probe_info; probe_info = probe_info->next, ++probe) {
		if ((serial && strstr(probe_info->serial, serial)) || (position && probe == position))
			return probe_info;
	}
	return NULL;
}

void probe_info_to_bmp_info(const probe_info_s *const probe, bmp_info_s *info)
{
	info->bmp_type = probe->type;

	const size_t serial_len = MIN(strlen(probe->serial), sizeof(info->serial) - 1U);
	memcpy(info->serial, probe->serial, serial_len);
	info->serial[serial_len] = '\0';

	const size_t version_len = MIN(strlen(probe->version), sizeof(info->version) - 1U);
	memcpy(info->version, probe->version, version_len);
	info->version[version_len] = '\0';

	const size_t product_len = strlen(probe->product);
	const size_t manufacturer_len = strlen(probe->manufacturer);
	/* + 4 as we're including two parens, a space and C's NUL character requirement */
	const size_t descriptor_len = MIN(product_len + manufacturer_len + 4, sizeof(info->manufacturer));

	if (snprintf(info->manufacturer, descriptor_len, "%s (%s)", probe->product, probe->manufacturer) !=
		(int)descriptor_len - 1) {
		DEBUG_WARN("Probe descriptor string '%s (%s)' exceeds allowable manufacturer description length\n",
			probe->product, probe->manufacturer);
	}
}
