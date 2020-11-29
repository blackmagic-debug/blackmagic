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

#if HOSTED_BMP_ONLY == 1
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
int stlink_init(bmp_info_t *info) {return -1;};
int stlink_hwversion(void) {return -1;};
const char *stlink_target_voltage(bmp_info_t *info) {return "ERROR";};
void stlink_srst_set_val(bmp_info_t *info, bool assert) {};
bool stlink_srst_get_val(void) {return true;};
int stlink_enter_debug_swd(bmp_info_t *info, ADIv5_DP_t *dp) {return -1;};
void stlink_adiv5_dp_defaults(ADIv5_DP_t *dp) {};
int stlink_jtag_dp_init(ADIv5_DP_t *dp) {return false;};
int jtag_scan_stlinkv2(bmp_info_t *info, const uint8_t *irlens) {return 0;};
void stlink_exit_function(bmp_info_t *info) {};
void stlink_max_frequency_set(bmp_info_t *info, uint32_t freq) {};
uint32_t stlink_max_frequency_get(bmp_info_t *info) {return 0;};
# pragma GCC diagnostic pop
#else
int stlink_init(bmp_info_t *info);
int stlink_hwversion(void);
const char *stlink_target_voltage(bmp_info_t *info);
void stlink_srst_set_val(bmp_info_t *info, bool assert);
bool stlink_srst_get_val(void);
int stlink_enter_debug_swd(bmp_info_t *info, ADIv5_DP_t *dp);
void stlink_adiv5_dp_defaults(ADIv5_DP_t *dp);
int stlink_jtag_dp_init(ADIv5_DP_t *dp);
int jtag_scan_stlinkv2(bmp_info_t *info, const uint8_t *irlens);
void stlink_exit_function(bmp_info_t *info);
void stlink_max_frequency_set(bmp_info_t *info, uint32_t freq);
uint32_t stlink_max_frequency_get(bmp_info_t *info);
#endif
#endif
