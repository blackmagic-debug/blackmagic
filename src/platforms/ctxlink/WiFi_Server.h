/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 Sid Price <sid@sidprice.com>
 * Written by Sid Price <sid@sidprice.com>
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

#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif
void app_initialize(void);
void app_task(void);
//
void gdb_tcp_server(void);
bool is_gdb_client_connected(void);

void data_tcp_server(void);
bool is_uart_client_connected(void);
void send_uart_data(uint8_t *buffer, uint8_t length);

bool swo_trace_server_active(void);
void wifi_setup_swo_trace_server(void);
bool is_swo_trace_client_connected(void);
void send_swo_trace_data(uint8_t *buffer, uint8_t length);

void wifi_gdb_putchar(uint8_t ch, bool flush);
void wifi_gdb_flush(bool force);
bool wifi_got_client(void);
uint8_t wifi_get_next(void);
uint8_t wifi_get_next_to(uint32_t timeout);

void wifi_get_ip_address(char *buffer, uint32_t size);
void wifi_connect(size_t argc, const char **argv, char *buffer, uint32_t size);
void app_task_wait_spin(void);
void wifi_disconnect(void);
#ifdef __cplusplus
}
#endif /*  __cplusplus */
#endif // WIFI_SERVER_H
