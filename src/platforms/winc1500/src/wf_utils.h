/*==============================================================================
Copyright 2016 Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef __WF_UTILS_H
#define __WF_UTILS_H

#ifdef __cplusplus
     extern "C" {
#endif

// Converts, if necessary, little-endian values from WINC1500 to big-endian values
// for host MCU if host MCU is big-endian        
#ifdef HOST_MCU_BIG_ENDIAN
    #define FIX_ENDIAN_32(x) ((((uint32_t)(x) & 0xff000000) >> 24) |    \
                              (((uint32_t)(x) & 0x00ff0000) >> 8)  |    \
                              (((uint32_t)(x) & 0x0000ff00) << 8)  |    \
                              (((uint32_t)(x) & 0x000000ff) << 24))

    #define FIX_ENDIAN_16(x) ((((uint16_t)(x) & 0xff00) >> 8) |     \
                              (((uint16_t)(x) & 0x00ff) << 8))
#else
    #define FIX_ENDIAN_32(x)  (x)
    #define FIX_ENDIAN_16(x)  (x)
#endif
       
#define BSP_MIN(x,y) ((x)>(y)?(y):(x))
         
void DelayMs(uint16_t ms);
void GenerateErrorEvent(uint32_t errorCode);

#ifdef __cplusplus
     }
#endif         
     
     
#endif // __WF_UTILS_H     