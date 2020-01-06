/**
  WINC1500 Driver configuration header File 
  
  @Company:
    Microchip Technology Inc.

  @File Name:
    winc1500_driver_config.h.h

  @Summary:
    Contains configuration macros for WINC1500 driver

  @Description:
    
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
 
#ifndef __WINC1500_DRIVER_CONFIG_H
#define __WINC1500_DRIVER_CONFIG_H

// TODO: 
// Uncomment this define if Host MCU is big-endian.  
// Comment out this define if Host MCU is little-endian.
//#define HOST_MCU_BIG_ENDIAN

// TODO:
// Set the size of host MCU pointer, in bytes.  Pointer sizes vary depending on the MCU 
// architecture.  The pointer size can be determined by executing the code: 
//    int x = sizeof(int *);  

#define M2M_POINTER_SIZE_IN_BYTES     4

#ifndef  M2M_POINTER_SIZE_IN_BYTES
    #error "Need to define M2M_POINTER_SIZE_IN_BYTES"
#endif

// TODO:
// comment out those features not needed otherwise keep as default below
#define M2M_ENABLE_ERROR_CHECKING        // error checking code
#define M2M_ENABLE_PRNG                  // prng (psuedo-random number) code
#define M2M_ENABLE_SOFT_AP_MODE          // SoftAP feature
#define M2M_ENABLE_WPS                   // WPS feature
#define M2M_WIFI_ENABLE_P2P              // P2P feature
#define M2M_ENABLE_HTTP_PROVISION_MODE   // HTTP provisioning mode
#define M2M_ENABLE_SCAN_MODE             // Wi-Fi scanning
#define M2M_ENABLE_SPI_FLASH             // supports host firmware update utility


// The WINC1500, by default, outputs debug information from its UART.  Unless 
// debugging, this should be disabled to save power and increase WINC1500 efficiency.
// Comment out this define to enable the WINC1500 debug output. Do this if you know what 
// you are doing.
#define M2M_DISABLE_FIRMWARE_LOG


#endif // __WINC1500_DRIVER_CONFIG_H
