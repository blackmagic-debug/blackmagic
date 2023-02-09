/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TARGET_JEP106_H
#define TARGET_JEP106_H

/*
 * JEP-106 code list
 * JEP-106 is a JEDEC standard assigning IDs to different manufacturers
 * the codes in this list are encoded as 16 bit values,
 * with the first bit marking a legacy code (ASCII, not JEP106), the following 3 bits being NULL/unused
 * the following 4 bits the number of continuation codes (see JEP106 continuation scheme),
 * and the last 8 bits being the code itself (without parity, bit 7 is always 0).
 *
 * |15     |11     |7|6           0|
 * | | | | | | | | |0| | | | | | | |
 *  |\____/ \______/|\_____________/
 *  |  V        V   |       V
 *  | Unused   Cont	|      code
 *  |          Code |
 *  \_ Legacy flag  \_ Parity bit (always 0)
 */
#define ASCII_CODE_FLAG (1U << 15U) /* flag the code as legacy ASCII */

#define JEP106_MANUFACTURER_ARM          0x43bU /* ARM Ltd. */
#define JEP106_MANUFACTURER_FREESCALE    0x00eU /* Freescale */
#define JEP106_MANUFACTURER_NXP          0x015U /* NXP */
#define JEP106_MANUFACTURER_TEXAS        0x017U /* Texas Instruments */
#define JEP106_MANUFACTURER_ATMEL        0x01fU /* Atmel */
#define JEP106_MANUFACTURER_STM          0x020U /* STMicroelectronics */
#define JEP106_MANUFACTURER_CYPRESS      0x034U /* Cypress Semiconductor */
#define JEP106_MANUFACTURER_INFINEON     0x041U /* Infineon Technologies */
#define JEP106_MANUFACTURER_NORDIC       0x244U /* Nordic Semiconductor */
#define JEP106_MANUFACTURER_SPECULAR     0x501U /* LPC845 with code 501. Strange!? Specular Networks */
#define JEP106_MANUFACTURER_ARM_CHINA    0xa75U /* Arm China */
#define JEP106_MANUFACTURER_ENERGY_MICRO 0x673U /* Energy Micro */
#define JEP106_MANUFACTURER_GIGADEVICE   0x751U /* GigaDevice */
#define JEP106_MANUFACTURER_RASPBERRY    0x913U /* Raspberry Pi */
#define JEP106_MANUFACTURER_RENESAS      0x423U /* Renesas */
#define JEP106_MANUFACTURER_XILINX       0x309U /* Xilinx */
/*
 * This JEP code should belong to "Andes Technology Corporation", but is used on RISC-V by GigaDevice,
 * so in the unlikely event we need to support chips by them, here be dragons.
 */
#define JEP106_MANUFACTURER_RV_GIGADEVICE 0x61eU

/*
 * This code is not listed in the JEP106 standard, but is used by some stm32f1 clones
 * since we're not using this code elsewhere let's switch to the stm code.
 */
#define JEP106_MANUFACTURER_ERRATA_CS 0x555U

/*
 * CPU2 for STM32W(L|B) uses ARM's JEP-106 continuation code (4) instead of
 * STM's JEP-106 continuation code (0) like expected, CPU1 behaves as expected.
 *
 * See RM0453
 * https://www.st.com/resource/en/reference_manual/rm0453-stm32wl5x-advanced-armbased-32bit-mcus-with-subghz-radio-solution-stmicroelectronics.pdf :
 * 38.8.2 CPU1 ROM CoreSight peripheral identity register 4 (ROM_PIDR4)
 * vs
 * 38.13.2 CPU2 ROM1 CoreSight peripheral identity register 4 (C2ROM1_PIDR4)
 *
 * let's call this an errata and switch to the "correct" continuation scheme.
 *
 * Note: the JEP code 0x420 would belong to "Legend Silicon Corp." so in
 * the unlikely event we need to support chips by them, here be dragons.
 */
#define JEP106_MANUFACTURER_ERRATA_STM32WX 0x420U

/* MindMotion MM32F5 uses the forbidden continuation code */
#define JEP106_MANUFACTURER_ERRATA_ARM_CHINA 0xc7fU

#endif /*TARGET_JEP106_H*/
