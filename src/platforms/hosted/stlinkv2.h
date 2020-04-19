/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 Uwe Bonnes
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
#if !defined(__STLINKV2_H_)

#define STLINK_ERROR_FAIL -1
#define STLINK_ERROR_OK 0
#define STLINK_ERROR_WAIT 1

#define STLINK_DEBUG_PORT_ACCESS            0xffff

void stlink_init(int argc, char **argv);
int stlink_hwversion(void);
void stlink_leave_state(void);
const char *stlink_target_voltage(void);
void stlink_srst_set_val(bool assert);
int stlink_enter_debug_swd(void);
int stlink_enter_debug_jtag(void);
int stlink_read_idcodes(uint32_t *);
uint32_t stlink_read_coreid(void);
int stlink_read_dp_register(uint16_t port, uint16_t addr, uint32_t *res);
int stlink_write_dp_register(uint16_t port, uint16_t addr, uint32_t val);

uint32_t stlink_dp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
				      uint16_t addr, uint32_t value);
uint32_t stlink_dp_read(ADIv5_DP_t *dp, uint16_t addr);
uint32_t stlink_dp_error(ADIv5_DP_t *dp);
void stlink_dp_abort(ADIv5_DP_t *dp, uint32_t abort);
int stlink_open_ap(uint8_t ap);
void stlink_close_ap(uint8_t ap);
void stlink_regs_read(ADIv5_AP_t *ap, void *data);
uint32_t stlink_reg_read(ADIv5_AP_t *ap, int idx);
void stlink_reg_write(ADIv5_AP_t *ap, int num, uint32_t val);
extern  int debug_level;
# define DEBUG_STLINK if (debug_level > 0) printf
# define DEBUG_USB    if (debug_level > 1) printf
#endif
