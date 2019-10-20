#pragma once

#ifdef __cplusplus
extern "C" {
#endif
	void APP_Initialize(void);
	void APP_Task(void);
	//
	void GDB_TCPServer(void);
	bool isGDBClientConnected(void);

	void UART_TCPServer (void);
	bool isUARTClientConnected(void) ;
	void SendUartData(uint8_t *lpBuffer, uint8_t length) ;

	void WiFi_gdb_putchar(unsigned char c, int flush);
	bool WiFi_GotClient( void );
	unsigned char WiFi_GetNext(void);
	unsigned char WiFi_GetNext_to(uint32_t timeout);	
	#ifdef  __cplusplus
}
#endif /*  __cplusplus */

