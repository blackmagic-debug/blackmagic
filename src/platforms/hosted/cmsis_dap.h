/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 - 2021 Uwe Bonnes(bon@elektron.ikp.physik.tu-darmstadt.de)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */
#if !defined(__CMSIS_DAP_H_)
#define __CMSIS_DAP_H_

#include "adiv5.h"
#include "cl_utils.h"

#if defined(CMSIS_DAP)
int dap_init(bmp_info_t *info);
void dap_exit_function(void);
void dap_adiv5_dp_defaults(ADIv5_DP_t *dp);
int cmsis_dap_jtagtap_init(jtag_proc_t *jtag_proc);
int dap_swdptap_init(ADIv5_DP_t *dp);
int dap_jtag_dp_init(ADIv5_DP_t *dp);
uint32_t dap_swj_clock(uint32_t clock);
void dap_swd_configure(uint8_t cfg);
void dap_srst_set_val(bool assert);
#else
int dap_init(bmp_info_t *info)
{
	DEBUG_WARN("FATAL: Missing hidapi-libusb\n");
	(void)info;
	return -1;
}
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
uint32_t dap_swj_clock(uint32_t clock) {return 0;}
void dap_exit_function(void) {};
void dap_adiv5_dp_defaults(ADIv5_DP_t *dp) {};
int cmsis_dap_jtagtap_init(jtag_proc_t *jtag_proc) {return -1;}
int dap_swdptap_init(ADIv5_DP_t *dp) {return -1;}
int dap_jtag_dp_init(ADIv5_DP_t *dp) {return -1;}
void dap_swd_configure(uint8_t cfg) {};
void dap_srst_set_val(bool assert) {};
# pragma GCC diagnostic pop

#endif

#endif
