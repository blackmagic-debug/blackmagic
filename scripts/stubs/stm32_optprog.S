@; Assembler sequence to program option bytes on STM32
@; Takes option address in r0 and value in r1.
@; Ends with BKPT instruction
.global _start

.equ FLASHBASE, 0x40022000

.equ KEY1, 0x45670123
.equ KEY2, 0xCDEF89AB

.equ FLASH_KEY, 0x04
.equ FLASH_OPTKEY, 0x08
.equ FLASH_CR, 0x10
.equ FLASH_SR, 0x0C

.equ OPTPG, 0x10

.equ BSY, 0x01

.syntax unified

_start:
	@; Load FLASH controller base address
	ldr r2, =FLASHBASE

	@; Do unlocking sequence
	ldr r3, =KEY1
	str r3, [r2, #FLASH_KEY]
	ldr r3, =KEY2
	str r3, [r2, #FLASH_KEY]

	@; Same for option bytes
	ldr r3, =KEY1
	str r3, [r2, #FLASH_OPTKEY]
	ldr r3, =KEY2
	str r3, [r2, #FLASH_OPTKEY]

	@; Set OPTPG bit in FLASH_CR
	ldr r3, [r2, #FLASH_CR]
	orr r3, r3, #OPTPG
	str r3, [r2, #FLASH_CR]
	@; Write data at address
	strh r1, [r0]

_wait:	@; Wait for BSY bit to clear
	ldr r4, [r2, #FLASH_SR]
	mov r6, #BSY
	tst r4, r6
	bne _wait

	bkpt

