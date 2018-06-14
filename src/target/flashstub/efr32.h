/* This file is derived from the Silicon Labs SDK:
 *
 *******************************************************************************
 * @section License
 * <b>Copyright 2016 Silicon Laboratories, Inc. http://www.silabs.com</b>
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Silicon Labs has no
 * obligation to support this Software. Silicon Labs is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Silicon Labs will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 ******************************************************************************/

 #include <stdint.h>

 #define EFM32_MSC_LOCK_LOCKKEY 0x1b71
 #define EFM32_FLASH_WRITE_TIMEOUT 10000000ul

 typedef struct {
   volatile uint32_t CTRL;      /**< Memory System Control Register  */
   volatile uint32_t READCTRL;  /**< Read Control Register  */
   volatile uint32_t WRITECTRL; /**< Write Control Register  */
   volatile uint32_t WRITECMD;  /**< Write Command Register  */
   volatile uint32_t ADDRB;     /**< Page Erase/Write Address Buffer  */
   uint32_t RESERVED0[1];       /**< Reserved for future use **/
   volatile uint32_t WDATA;     /**< Write Data Register  */
   volatile uint32_t STATUS;    /**< Status Register  */

   uint32_t RESERVED1[4];         /**< Reserved for future use **/
   volatile uint32_t IF;          /**< Interrupt Flag Register  */
   volatile uint32_t IFS;         /**< Interrupt Flag Set Register  */
   volatile uint32_t IFC;         /**< Interrupt Flag Clear Register  */
   volatile uint32_t IEN;         /**< Interrupt Enable Register  */
   volatile uint32_t LOCK;        /**< Configuration Lock Register  */
   volatile uint32_t CACHECMD;    /**< Flash Cache Command Register  */
   volatile uint32_t CACHEHITS;   /**< Cache Hits Performance Counter  */
   volatile uint32_t CACHEMISSES; /**< Cache Misses Performance Counter  */

   uint32_t RESERVED2[1];      /**< Reserved for future use **/
   volatile uint32_t MASSLOCK; /**< Mass Erase Lock Register  */

   uint32_t RESERVED3[1];     /**< Reserved for future use **/
   volatile uint32_t STARTUP; /**< Startup Control  */

   uint32_t RESERVED4[5];            /**< Reserved for future use **/
   volatile uint32_t CMD;            /**< Command Register  */
   volatile uint32_t BOOTLOADERCTRL; /**< Unlock writes to bootloader area */
 } MSC_TypeDef;
