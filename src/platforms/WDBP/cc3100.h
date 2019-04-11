#pragma once

#ifdef __cplusplus
extern "C" {
#endif
	void cc3100_init( void );
	char *CC3100_GetMACAddress( void );
	int CC3100_ScanSSID( void );
	char * CC3100_GetSSIDByIndex( u_int32_t thisSSID );
	u_int32_t CC3100_GetSecurityTypeByIndex( u_int32_t thisSSID );
	bool CC3100_ConnectToSSID( u_int32_t thisSSID, char *key );
	int CC3100_ConnectAP( const char *ssid, const char *key, int security_type, int timeout );
	void CC3100_GetGatewayAddress( char *GatewayAddress );
	void CC3100_GetIpAddress( char *IPAddress );
	bool CC3100_APIsConnected( void );
#ifdef  __cplusplus
}
#endif /*  __cplusplus */
