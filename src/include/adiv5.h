/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ADIV5_H
#define __ADIV5_H

#include "general.h"
#include "jtag_scan.h"
#include "target.h"

#define DP_ABORT	0x0
#define DP_CTRLSTAT	0x4
#define DP_SELECT	0x8
#define DP_RDBUFF	0xC


/* Try to keep this somewhat absract for later adding SW-DP */
typedef struct ADIv5_DP_s {
	struct ADIv5_DP_s *next;
	uint32_t idcode;

	void (*dp_write)(struct ADIv5_DP_s *dp, uint8_t addr, uint32_t value);
	uint32_t (*dp_read)(struct ADIv5_DP_s *dp, uint8_t addr);

	void (*ap_write)(struct ADIv5_DP_s *dp, uint8_t addr, uint32_t value);
	uint32_t (*ap_read)(struct ADIv5_DP_s *dp, uint8_t addr);

	uint32_t (*error)(struct ADIv5_DP_s *dp);

	uint32_t (*low_access)(struct ADIv5_DP_s *dp, uint8_t APnDP, uint8_t RnW, 
			uint8_t addr, uint32_t value);

	union {
		jtag_dev_t *dev;
		uint8_t fault;
	};
} ADIv5_DP_t;

static inline void adiv5_dp_write(ADIv5_DP_t *dp, uint8_t addr, uint32_t value)
{
	dp->dp_write(dp, addr, value);
}

static inline uint32_t adiv5_dp_read(ADIv5_DP_t *dp, uint8_t addr)
{
	return dp->dp_read(dp, addr);
}

static inline void adiv5_dp_write_ap(ADIv5_DP_t *dp, uint8_t addr, uint32_t value)
{
	dp->ap_write(dp, addr, value);
}

static inline uint32_t adiv5_dp_read_ap(ADIv5_DP_t *dp, uint8_t addr)
{
	return dp->ap_read(dp, addr);
}

static inline uint32_t adiv5_dp_error(ADIv5_DP_t *dp)
{
	return dp->error(dp);
}

static inline uint32_t adiv5_dp_low_access(struct ADIv5_DP_s *dp, uint8_t APnDP, 
					uint8_t RnW, uint8_t addr, uint32_t value)
{
	return dp->low_access(dp, APnDP, RnW, addr, value);
}

extern ADIv5_DP_t *adiv5_dp_list;

typedef struct ADIv5_AP_s {
	ADIv5_DP_t *dp;
	uint8_t apsel;

	uint32_t idr;
	uint32_t cfg;
	uint32_t base;
} ADIv5_AP_t;

struct target_ap_s {
	target t;
	ADIv5_AP_t *ap;
};

extern ADIv5_AP_t adiv5_aps[5];
extern int adiv5_ap_count;

void adiv5_free_all(void);
void adiv5_dp_init(ADIv5_DP_t *dp);

uint32_t adiv5_ap_mem_read(ADIv5_AP_t *ap, uint32_t addr);
void adiv5_ap_mem_write(ADIv5_AP_t *ap, uint32_t addr, uint32_t value);

void adiv5_ap_write(ADIv5_AP_t *ap, uint8_t addr, uint32_t value);
uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint8_t addr);

void adiv5_jtag_dp_handler(jtag_dev_t *dev);
int adiv5_swdp_scan(void);

#endif

