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

#ifndef __WF_SPI_H
#define __WF_SPI_H

#ifdef __cplusplus
     extern "C" {
#endif
        
void nm_spi_init(void);
void nm_spi_deinit(void);
uint32_t nm_spi_read_reg(uint32_t address);
void nm_spi_write_reg(uint32_t address, uint32_t value);
void spi_read_block(uint32_t startAddress, uint8_t *p_buf, uint16_t size);
void spi_write_block(uint32_t startAddress, uint8_t *buf, uint16_t size);
uint32_t spi_read_reg(uint32_t addr);
void spi_write_reg(uint32_t addr, uint32_t data);
uint32_t nm_read_reg(uint32_t regAddress);
void nm_write_reg(uint32_t regAddres, uint32_t value);
void nm_read_block(uint32_t startAddress, uint8_t *p_buf, uint32_t size);
void nm_write_block(uint32_t startAddress, uint8_t *p_buf, uint32_t size);
void nm_spi_reset(void);

#ifdef __cplusplus
     }
#endif

#endif // __WF_SPI_H
