#pragma once

#ifdef __cplusplus
extern "C" {
#endif
	void APP_Initialize(void);
	void APP_Task(void);
	void TCPServer(void);
	bool isClientConnected(void);
	void WiFi_putchar(unsigned char c, int flush);
	bool WiFi_GotClient( void );
	unsigned char WiFi_GetNext(void);
	unsigned char WiFi_GetNext_to(uint32_t timeout);	
	#ifdef  __cplusplus
}
#endif /*  __cplusplus */

