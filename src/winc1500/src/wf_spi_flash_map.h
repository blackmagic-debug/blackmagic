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

/**
*  @file        wf_spi_flash_map.h
*  @brief        This module contains spi flash CONTENT
*/
#ifndef __WF_SPI_FLASH_MAP_H
#define __WF_SPI_FLASH_MAP_H

#define FLASH_MAP_VER_0        (0)
#define FLAAH_MAP_VER_1        (1)

#define FLASH_MAP_VERSION    FLAAH_MAP_VER_1    

//#define DOWNLOAD_ROLLBACK
//#define OTA_GEN
#define _PROGRAM_POWER_SAVE_

/* =======*=======*=======*=======*=======
 *       General Sizes for Flash Memory
 * =======*=======*=======*=======*=======
 */

#define FLASH_START_ADDR                    (0UL)
/*!<Starting Address of Flash Memory
 *
 */
#define FLASH_BLOCK_SIZE                    (32 * 1024UL)
/*!<Block Size in Flash Memory
 */
#define FLASH_SECTOR_SZ                     (4 * 1024UL)
/*!<Sector Size in Flash Memory
 */
#define FLASH_PAGE_SZ                       (256)
/*!<Page Size in Flash Memory
 */
#define FLASH_2M_TOTAL_SZ                   (256 * 1024UL)
/*!<Total Size of 2M Flash Memory
 */
#define FLASH_4M_TOTAL_SZ                   (512 * 1024UL)
/*!<Total Size of 4M Flash Memory
 */
#define FLASH_8M_TOTAL_SZ                   (1024 * 1024UL)
/*!<Total Size of 8M Flash Memory
 */

/*
 * Detailed Sizes and locations for Flash Memory:
 *  _______________________________ ___________________________ _______________________________________________
 * | Starting Address   |    Size    |    Location's Name       |       Description                                       |
 * |____________________|___________|___________________________|_______________________________________________|
 * |      0K            |     4K    |    Boot Firmware          |    Firmware to select which version to run        |
 * |      4K            |     8K    |    Control Section        |    Structured data used by Boot firmware        |
 * |     12K            |     4K    |    PLL+GAIN :             |    LookUp Table for PLL and Gain calculations    |
 * |                    |           |    PLL  Size = 1K         |        PLL                                         |
 * |                    |           |    GAIN Size = 3K         |        Gain configuration                         |
 * |     16K            |     4K    |    CERTIFICATE            |    X.509 Certificate storage                    |
 * |     20K            |     4K    |    Scratch Section        |    Empty Section                                |
 * |     24K            |     4K    |    Reserved TLS Server    |    Reserved                                    |
 * |     28K            |     8K    |    HTTP Files             |    Files used with Provisioning Mode            |
 * |     36K            |     4K    |    Connection Parameters  |    Parameters for success connection to AP        |
 * |     40K            |   236K    |    Main Firmware/program  |    Main Firmware to run WiFi Chip                |
 * |    276K            |   236K    |    OTA Firmware           |    OTA firmware                                |
 * |    512 K                                                       Total flash size                            |
 * |____________________|___________|___________________________|_______________________________________________|
 *
 *
 * *Keys for Comments with each MACRO:
 *         "L:xxxK" -means-> location   :xxxK
 *        "S:xxxK" -means-> Size is     :xxxK
 */

/*
 * Boot Firmware: which used to select which firmware to run
 *
 */
#define M2M_BOOT_FIRMWARE_STARTING_ADDR     (FLASH_START_ADDR)
#define M2M_BOOT_FIRMWARE_FLASH_SZ          (FLASH_SECTOR_SZ)

/*
 * Control Section: which used by Boot firmware
 *
 */
#define M2M_CONTROL_FLASH_OFFSET            (M2M_BOOT_FIRMWARE_STARTING_ADDR + M2M_BOOT_FIRMWARE_FLASH_SZ)
#define M2M_CONTROL_FLASH_BKP_OFFSET        (M2M_CONTROL_FLASH_OFFSET + FLASH_SECTOR_SZ)
#define M2M_CONTROL_FLASH_SEC_SZ            (FLASH_SECTOR_SZ)
#define M2M_CONTROL_FLASH_TOTAL_SZ          (FLASH_SECTOR_SZ * 2)

/*
 * LUT for PLL and TX Gain settings:
 *
 */
#define M2M_PLL_FLASH_OFFSET                (M2M_CONTROL_FLASH_OFFSET + M2M_CONTROL_FLASH_TOTAL_SZ)
#define M2M_PLL_FLASH_SZ                    (1024 * 1)
#define M2M_GAIN_FLASH_OFFSET               (M2M_PLL_FLASH_OFFSET + M2M_PLL_FLASH_SZ)
#define M2M_GAIN_FLASH_SZ                   (M2M_CONFIG_SECT_TOTAL_SZ - M2M_PLL_FLASH_SZ)
#define M2M_CONFIG_SECT_TOTAL_SZ            (FLASH_SECTOR_SZ)

/*
 * Certificate:
 *
 */
#define M2M_TLS_FLASH_ROOTCERT_CACHE_OFFSET (M2M_PLL_FLASH_OFFSET + M2M_CONFIG_SECT_TOTAL_SZ)
#define M2M_TLS_FLASH_ROOTCERT_CACHE_SIZE   (FLASH_SECTOR_SZ * 1)

/*
 * Scratch:
 *
 */
#define M2M_TLS_FLASH_SESSION_CACHE_OFFSET  (M2M_TLS_FLASH_ROOTCERT_CACHE_OFFSET + M2M_TLS_FLASH_ROOTCERT_CACHE_SIZE)
#define M2M_TLS_FLASH_SESSION_CACHE_SIZE    (FLASH_SECTOR_SZ * 1)

/*
 * reserved section
 *
 */
#define M2M_RESERVED_FLASH_OFFSET           (M2M_TLS_FLASH_SESSION_CACHE_OFFSET + M2M_TLS_FLASH_SESSION_CACHE_SIZE)
#define M2M_RESERVED_FLASH_SZ               (FLASH_SECTOR_SZ * 1)
/*
 * HTTP Files
 *
 */
#define M2M_HTTP_MEM_FLASH_OFFSET           (M2M_RESERVED_FLASH_OFFSET + M2M_RESERVED_FLASH_SZ)
#define M2M_HTTP_MEM_FLASH_SZ               (FLASH_SECTOR_SZ * 2)
/*
 * Saved Connection Parameters:
 *
 */
#define M2M_CACHED_CONNS_FLASH_OFFSET       (M2M_HTTP_MEM_FLASH_OFFSET + M2M_HTTP_MEM_FLASH_SZ)
#define M2M_CACHED_CONNS_FLASH_SZ           (FLASH_SECTOR_SZ * 1)

/*
 *
 * Common section size
 */

#define M2M_COMMON_DATA_SEC                 (M2M_BOOT_FIRMWARE_FLASH_SZ + M2M_CONTROL_FLASH_TOTAL_SZ + M2M_CONFIG_SECT_TOTAL_SZ + \
                                            M2M_TLS_FLASH_ROOTCERT_CACHE_SIZE + M2M_TLS_FLASH_SESSION_CACHE_SIZE + \
                                            M2M_HTTP_MEM_FLASH_SZ  + M2M_CACHED_CONNS_FLASH_SZ + M2M_RESERVED_FLASH_SZ)
/*
 *
 * OTA image1 Offset
 */                                

#define M2M_OTA_IMAGE1_OFFSET               (M2M_CACHED_CONNS_FLASH_OFFSET + M2M_CACHED_CONNS_FLASH_SZ)
/*
 * Firmware Offset
 *
 */
#if (defined _FIRMWARE_)||(defined OTA_GEN)
#define M2M_FIRMWARE_FLASH_OFFSET           (0UL)
#else
#if (defined DOWNLOAD_ROLLBACK)
#define M2M_FIRMWARE_FLASH_OFFSET           (M2M_OTA_IMAGE2_OFFSET)
#else
#define M2M_FIRMWARE_FLASH_OFFSET           (M2M_OTA_IMAGE1_OFFSET)
#endif
#endif
/*
 *
 * Firmware
 */
#define M2M_FIRMWARE_FLASH_SZ               (236*1024UL)
/**
 *
 * OTA image Size
 */
#define OTA_IMAGE_SIZE                      (M2M_FIRMWARE_FLASH_SZ)
/**
 *
 * Flash Total size
 */
#define FLASH_IMAGE1_CONTENT_SZ             (M2M_COMMON_DATA_SEC  +  OTA_IMAGE_SIZE)
                                    
/**
 *
 * OTA image 2 offset
 */
#define M2M_OTA_IMAGE2_OFFSET               (FLASH_IMAGE1_CONTENT_SZ)

/*
 * App(Cortus App 4M): App. which runs over firmware
 *
 */
#define M2M_APP_4M_MEM_FLASH_SZ             (FLASH_SECTOR_SZ * 10)
#define M2M_APP_4M_MEM_FLASH_OFFSET         (FLASH_4M_TOTAL_SZ - M2M_APP_4M_MEM_FLASH_SZ)

/* Check if total size of content
 *  don't exceed total size of memory allowed
 **/
#if (M2M_COMMON_DATA_SEC  +  (OTA_IMAGE_SIZE *2)> FLASH_4M_TOTAL_SZ)
#error "Excced 4M Flash Size"
#endif /* (FLASH_CONTENT_SZ > FLASH_TOTAL_SZ) */


#endif // __WF_SPI_FLASH_MAP_H
