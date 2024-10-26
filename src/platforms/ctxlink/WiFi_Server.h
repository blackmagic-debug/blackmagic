#pragma once

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
void send_uart_data(uint8_t *lpBuffer, uint8_t length);

bool swo_trace_server_active(void);
void wifi_setup_swo_trace_server(void);
bool is_swo_trace_client_connected(void);
void send_swo_trace_data(uint8_t *buffer, uint8_t length);

void wifi_gdb_putchar(unsigned char ch, int flush);
bool wifi_got_client(void);
unsigned char wifi_get_next(void);
unsigned char wifi_get_next_to(uint32_t timeout);
#ifdef __cplusplus
}
#endif /*  __cplusplus */
