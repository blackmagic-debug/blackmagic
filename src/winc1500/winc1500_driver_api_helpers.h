/**
  WINC1500 Driver Stub API helpers header File 

  @Company:
    Microchip Technology Inc.

  @File Name:
    winc1500_driver_api_helpers.h

  @Summary:
    Contains helper functions (mostly status callbacks) for WINC1500 driver

  @Description:
    These functions and type definitions are used by the stub functions as well 
	as main application to write application state machine.
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

#ifndef WINC1500_DRIVER_API_HELPERS_H
#define	WINC1500_DRIVER_API_HELPERS_H


#ifdef	__cplusplus
extern "C" {
#endif

    // type definitions
    typedef void (*tpfAppWifiCb) (uint8_t u8MsgType, void *pvMsg); // to implement WiFi callback function
    
    // helper functions to read and clear application flags set in the event handlers.
    void ClearWiFiEventStates(void);
    bool isDriverInitComplete(void);
    bool isWifiConnected(void);
    bool isScanResultReady(void);
    bool isConnectionStateChanged(void);
    bool isIpAddressAssigned(void);
    bool isScanComplete(void);
    bool isRssiReady(void);
    bool isProvisionInfoReady(void);
    bool isWpsReady(void);
    bool isPrngReady(void);

    void ClearSocketEventStates(void);
    bool isSocketBindOccurred(void);
    bool isSocketListenOccurred(void);
    bool isSocketAcceptOccurred(void);
    bool isSocketConnectOccurred(void);
    bool isSocketRecvOccurred(void);
    bool isSocketRecvFromOccurred(void);
    bool isSocketSendOccurred(void);
    bool isSocketSendToOccurred(void);
    bool isPingReplyOccurred(void);
    bool isDnsResolved(void);

    void registerWifiCallback(tpfAppWifiCb pfAppWifiCb);
    void registerSocketCallback(tpfAppSocketCb pfAppSocketCb);

#ifdef	__cplusplus
}
#endif

#endif	/* WINC1500_DRIVER_API_HELPERS_H */

