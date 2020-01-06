/**
  Generated Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This is a sample main file generated for reference

  Description:
	This file shows how to implemet wifi and socket callback functions to be 
	used with MLA WINC1500 driver. A sample application state machine is provided 
	for reference. Example will display several messages over the console using
	standard "printf" function. It will read the MAC address of the WINC1500 chip 
	and display it to the user over console.
 */

/*
    (c) 2016 Microchip Technology Inc. and its subsidiaries. You may use this
    software and any derivatives exclusively with Microchip products.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
    PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION
    WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
    BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
    FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
    ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
    THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.

    MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE
    TERMS.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "winc1500_api.h"
#include "winc1500_driver_api_helpers.h"

typedef enum
{
    APP_STATE_WAIT_FOR_DRIVER_INIT,
	APP_STATE_READ_MAC_ADDR,
    APP_STATE_CONNECT_TO_WIFI,
    APP_STATE_WAIT_FOR_WIFI_CONNECT,
    APP_STATE_WAIT_FOR_IP,
    APP_STATE_GET_HOST_IP_BY_DNS,
    APP_STATE_WAIT_FOR_DNS,
    APP_STATE_CREATE_CLIENT_SOCKET,
    APP_STATE_CONNECT_TO_SERVER,
    APP_STATE_TRANSACT,
    APP_STATE_CLOSE_CLIENT_SOCKET,
    APP_STATE_ERROR,
    APP_STATE_SPIN
} APP_STATES;

APP_STATES appState;

static bool g_driverInitComplete = false;
static bool g_wifi_connected = false;
static bool g_ipAddressAssigned = false;
static bool g_dnsResolved = false;
static bool g_socketConnected = false;


/******************************************************************************
   Sample Wi-Fi callback function
******************************************************************************/
static void AppWifiCallback(uint8_t msgType, void *pvMsg)
{   
    switch (msgType)
    {
        case M2M_WIFI_DRIVER_INIT_EVENT:
        {
            g_driverInitComplete = true;
            break;
        }

        case M2M_WIFI_CONN_STATE_CHANGED_EVENT:
        {
            tstrM2mWifiStateChanged *pstrWifiState = (tstrM2mWifiStateChanged *) pvMsg;
            if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED)
            {
                g_wifi_connected = true;
            }
            else if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED)
            {
                g_wifi_connected = false;
            }
            else
            {
                printf("APP_WIFI_CB[%d]: Unknown WiFi state change\r\n", msgType);
            }
            break;
        }

        case M2M_WIFI_IP_ADDRESS_ASSIGNED_EVENT:
        {
            g_ipAddressAssigned = true;
            break;
        }

            /* Unused states. Can be implemented if needed  */
        case M2M_WIFI_DEFAULT_CONNNECT_EVENT:
        case M2M_WIFI_WPS_EVENT:
        case M2M_WIFI_CONN_INFO_RESPONSE_EVENT:
        case M2M_WIFI_PROVISION_INFO_EVENT:
        case M2M_WIFI_SCAN_DONE_EVENT:
        case M2M_WIFI_SCAN_RESULT_EVENT:
        case M2M_WIFI_SYS_TIME_EVENT:
        case M2M_WIFI_PRNG_EVENT:
        case M2M_WIFI_IP_CONFLICT_EVENT:
        case M2M_WIFI_INVALID_WIFI_EVENT:
        case M2M_WIFI_RSSI_EVENT:
            printf("APP_WIFI_CB[%d]: Un-implemented state\r\n", msgType);
            break;
    }
}

bool isDriverInitComplete(void)
{
    bool res = g_driverInitComplete;
    g_driverInitComplete = false;
    return res;
}

bool isWifiConnected(void)
{
    bool res = g_wifi_connected;
    // no need to reset flag "g_wifi_connected" to false. Event will do that.
    return res;
}

bool isIpAddressAssigned(void)
{
    bool res = g_ipAddressAssigned;
    g_ipAddressAssigned = false;
    return res;
}

/******************************************************************************
   Sample Socket callback function
******************************************************************************/
static void AppSocketCallback(SOCKET sock, uint8_t msgType, void *pvMsg)
{
    // if multiple sockets are present, use "sock" parameter to know the
    // associated socket for this instance of callback. 

    switch (msgType)
    {
        case M2M_SOCKET_DNS_RESOLVE_EVENT:
        {
            g_dnsResolved = true;
            break;
        }

        case M2M_SOCKET_CONNECT_EVENT:
        {
            t_socketConnect *pSockConnResp = (t_socketConnect *) pvMsg;
            if (pSockConnResp && pSockConnResp->error >= SOCK_ERR_NO_ERROR)
            {
                g_socketConnected = true;
                printf("APP_SOCK_CB[%d]: Successfully connected\r\n", msgType);
            }
            else
            {
                printf("APP_SOCK_CB[%d]: Connect error! code(%d)\r\n", msgType, pSockConnResp->error);
            }
            break;
        }

        case M2M_SOCKET_BIND_EVENT:
        case M2M_SOCKET_LISTEN_EVENT:
        case M2M_SOCKET_ACCEPT_EVENT:
        case M2M_SOCKET_RECV_EVENT:
        case M2M_SOCKET_SEND_EVENT:
        case M2M_SOCKET_SENDTO_EVENT:
        case M2M_SOCKET_RECVFROM_EVENT:
        case M2M_SOCKET_PING_RESPONSE_EVENT:
            printf("APP_SOCK_CB[%d]: Un-implemented state\r\n", msgType);
            break;
    }
}

bool isDnsResolved(void)
{
    bool res = g_dnsResolved;
    g_dnsResolved = false;
    return res;
}

bool isSocketConnected(void)
{
    bool res = g_socketConnected;
    // no need to reset flag "g_socketConnected" to false. App will do that.
    return res;
}


/******************************************************************************
   Sample Application Initialize
******************************************************************************/
void APP_Initialize(void)
{   
    /* register callback functions for Wi-Fi and Socket events */
    registerWifiCallback(AppWifiCallback);
    registerSocketCallback(AppSocketCallback);

    appState = APP_STATE_WAIT_FOR_DRIVER_INIT;
	
	// TODO: Add other necessary App Init code
}

/******************************************************************************
   Sample Application task
******************************************************************************/
void APP_Task(void)
{

    switch (appState)
    {
        case APP_STATE_WAIT_FOR_DRIVER_INIT:
        {
            if (isDriverInitComplete())
            {
                printf("APP_TASK[%d]: WINC1500 driver initialized!\r\n", appState);
                appState = APP_STATE_TEST; //APP_STATE_CONNECT_TO_WIFI;
            }
            break;
        }

        case APP_STATE_READ_MAC_ADDR:
        {            
            printf("APP_TASK[%d]: Testing WINC1500 SPI comm. Read MAC address ...\r\n", appState);                        

            uint8_t result;
            static uint8_t mac_addr[M2M_MAC_ADDRESS_LEN];
            const char user_define_mac_address[] = {0xf8, 0xf0, 0x05, 0x20, 0x0b, 0x09};

            m2m_wifi_get_otp_mac_address(mac_addr, &result);
            if (!result)
            {
                printf("APP_TASK[%d]: USER MAC Address : ", appState);
                /* Cannot found MAC Address from OTP. Set user define MAC address. */
                m2m_wifi_set_mac_address((uint8_t *) user_define_mac_address);
            }
            else
            {
                printf("APP_TASK[%d]: OTP MAC Address : ", appState);
            }

            /* Get MAC Address. */
            m2m_wifi_get_mac_address(mac_addr);
            printf("%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   mac_addr[0], mac_addr[1], mac_addr[2],
                   mac_addr[3], mac_addr[4], mac_addr[5]);
            printf("APP_TASK[%d]: Done.\r\n", appState);
            appState = APP_STATE_SPIN;
            break;
        }
		
        case APP_STATE_ERROR:
        {
            /* Indicate error */
            // TODO: Turn ON LED or something..
            appState = APP_STATE_SPIN;

            /* Intentional fall */
        }
        case APP_STATE_SPIN:
        {
            break;
        }
    }
}

/******************************************************************************
   Main
******************************************************************************/
int main(void)
{
    // TODO: initialize the device, oscillator, bsp, set pins,...etc
	
	// Initialize WINC1500 application
    APP_Initialize();
	
    printf("MAIN: Starting driver initialization...\r\n");
    m2m_wifi_init();

    while (1)
    {        
        APP_Task();
        m2m_wifi_task();
    }

    return -1;
}

/******************************************************************************
   External Interrupt Handler
******************************************************************************/ 
// TODO: External interrupt ISR as per your MCU coding guide. Sample as below:
/* void __attribute__ ( ( interrupt) ) _INT3Interrupt(void) 
{        
    m2m_EintHandler(); // Call as needed by driver    
    EX_INT3_InterruptFlagClear(); // Clear interrupt
} */

/**
 End of File
 */