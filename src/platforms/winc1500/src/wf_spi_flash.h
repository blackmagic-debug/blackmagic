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

#ifndef __WF_SPI_FLASH_H
#define __WF_SPI_FLASH_H

//int8_t spi_flash_enable(uint8_t enable);

// To enable all SPI FLASH functions, define M2M_ENABLE_SPI_FLASH

#if defined(M2M_ENABLE_SPI_FLASH)
// Should not be used by application, only used for firmware update utility


/** \defgroup SPIFLASH Spi Flash
 * @file           wf_spi_flash.h
 * @brief          This file describe SPI flash APIs, how to use it and limitations with each one. 
 * @section      Example
 *                 This example illustrates a complete guide of how to use these APIs.
 * @code{.c}
                    #include "wf_spi_flash.h"

                    #define DATA_TO_REPLACE    "THIS IS A NEW SECTOR IN FLASH"

                    int main()
                    {
                        uint8_t    flashContent[FLASH_SECTOR_SZ] = {0};
                        uint32_t    flashTotalSize = 0;
                        uint32_t    flashOffset = 0;
    
                        ret = DownloadMode();
                        if(M2M_SUCCESS != ret)
                        {
                            printf("Unable to enter download mode\n");
                        }
                        else
                        {
                            flashTotalSize = spi_flash_get_size();
                        }

                        while((flashTotalSize > flashOffset) && (M2M_SUCCESS == ret))
                        {
                            ret = spi_flash_read(flashContent, flashOffset, FLASH_SECTOR_SZ);
                            if(M2M_SUCCESS != ret)
                            {
                                printf("Unable to read SPI sector\n");
                                break;
                            }
                            memcpy(flashContent, DATA_TO_REPLACE, strlen(DATA_TO_REPLACE));
        
                            ret = spi_flash_erase(flashOffset, FLASH_SECTOR_SZ);
                            if(M2M_SUCCESS != ret)
                            {
                                printf("Unable to erase SPI sector\n");
                                break;
                            }
        
                            ret = spi_flash_write(flashContent, flashOffset, FLASH_SECTOR_SZ);
                            if(M2M_SUCCESS != ret)
                            {
                                printf("Unable to write SPI sector\n");
                                break;
                            }
                            flashOffset += FLASH_SECTOR_SZ;
                        }
    
                        if(M2M_SUCCESS == ret)
                        {
                            printf("Successful operations\n");
                        }
                        else
                        {
                            printf("Failed operations\n");
                        }
    
                        while(1);
                        return M2M_SUCCESS;
                    }
 * @endcode  
 */


/**
 *    @fn        spi_flash_enable
 *    @brief    Enable spi flash operations
 *    @version    1.0
 */
int8_t spi_flash_enable(uint8_t enable);
/** \defgroup SPIFLASHAPI Function
 *   @ingroup SPIFLASH
 */

 /** @defgroup SPiFlashGetFn spi_flash_get_size
 *  @ingroup SPIFLASHAPI
 */
  /**@{*/
/*!
 * @fn             uint32_t spi_flash_get_size(void);
 * @brief         Returns with \ref uint32_t value which is total flash size\n
 * @note         Returned value in Mb (Mega Bit).
 * @return      SPI flash size in case of success and a ZERO value in case of failure.
 */
uint32_t spi_flash_get_size(void);
 /**@}*/

  /** @defgroup SPiFlashRead spi_flash_read
 *  @ingroup SPIFLASHAPI
 */
  /**@{*/
/*!
 * @fn             int8_t spi_flash_read(uint8_t *, uint32_t, uint32_t);
 * @brief          Read a specified portion of data from SPI Flash.\r\n
 * @param [out]    p_buf
 *                 Pointer to data buffer which will fill in with data in case of successful operation.
 * @param [in]     address
 *                 Address (Offset) to read from at the SPI flash.
 * @param [in]     u32Sz
 *                 Total size of data to be read in bytes
 * @warning           
 *                 - Address (offset) plus size of data must not exceed flash size.\r\n
 *                 - No firmware is required for reading from SPI flash.\r\n
 *                 - In case of there is a running firmware, it is required to pause your firmware first 
 *                   before any trial to access SPI flash to avoid any racing between host and running firmware on bus using 
 *                   @ref DownloadMode
 * @note           
 *                 - It is blocking function\n
 * @sa             DownloadMode, spi_flash_get_size
 * @return        The function returns @ref M2M_SUCCESS for successful operations  and a negative value otherwise.
 */
int8_t spi_flash_read(uint8_t *p_buf, uint32_t address, uint32_t u32Sz);
 /**@}*/

  /** @defgroup SPiFlashWrite spi_flash_write
 *  @ingroup SPIFLASHAPI
 */
  /**@{*/
/*!
 * @fn             int8_t spi_flash_write(uint8_t *, uint32_t, uint32_t);
 * @brief          Write a specified portion of data to SPI Flash.\r\n
 * @param [in]     p_buf
 *                 Pointer to data buffer which contains the required to be written.
 * @param [in]     u32Offset
 *                 Address (Offset) to write at the SPI flash.
 * @param [in]     u32Sz
 *                 Total number of size of data bytes
 * @note           
 *                 - It is blocking function\n
 *                 - It is user's responsibility to verify that data has been written successfully 
 *                   by reading data again and compare it with the original.
 * @warning           
 *                 - Address (offset) plus size of data must not exceed flash size.\r\n
 *                 - No firmware is required for writing to SPI flash.\r\n
 *                 - In case of there is a running firmware, it is required to pause your firmware first 
 *                   before any trial to access SPI flash to avoid any racing between host and running firmware on bus using 
 *                   @ref DownloadMode.
 *                 - Before writing to any section, it is required to erase it first.
 * @sa             DownloadMode, spi_flash_get_size, spi_flash_erase
 * @return       The function returns @ref M2M_SUCCESS for successful operations  and a negative value otherwise.
 
 */
int8_t spi_flash_write(uint8_t* p_buf, uint32_t u32Offset, uint32_t u32Sz);
 /**@}*/

  /** @defgroup SPiFlashErase spi_flash_erase
 *  @ingroup SPIFLASHAPI
 */
  /**@{*/
/*!
 * @fn             int8_t spi_flash_erase(uint32_t, uint32_t);
 * @brief          Erase a specified portion of SPI Flash.\r\n
 * @param [in]     u32Offset
 *                 Address (Offset) to erase from the SPI flash.
 * @param [in]     u32Sz
 *                 Size of SPI flash required to be erased.
 * @note         It is blocking function \r\n  
* @warning           
*                 - Address (offset) plus size of data must not exceed flash size.\r\n
*                 - No firmware is required for writing to SPI flash.\r\n
 *                 - In case of there is a running firmware, it is required to pause your firmware first 
 *                   before any trial to access SPI flash to avoid any racing between host and running firmware on bus using 
 *                   @ref DownloadMode
 *                 - It is blocking function\n
 * @sa             DownloadMode, spi_flash_get_size
 * @return       The function returns @ref M2M_SUCCESS for successful operations  and a negative value otherwise.

 */
int8_t spi_flash_erase(uint32_t u32Offset, uint32_t u32Sz);

void  DownloadMode(void);
 /**@}*/

#endif // M2M_ENABLE_SPI_FLASH

#endif    //__WF_SPI_FLASH_H
