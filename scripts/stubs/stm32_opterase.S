@; Assembler sequence to erase option bytes on STM32
@; Takes no parameters, ends with BKPT instruction
.global _start

.equ FLASHBASE, 0x40022000

.equ KEY1, 0x45670123
.equ KEY2, 0xCDEF89AB

.equ FLASH_KEY, 0x04
.equ FLASH_OPTKEY, 0x08
.equ FLASH_CR, 0x10
.equ FLASH_SR, 0x0C

.equ OPTER, 0x20
.equ STRT, 0x40

.equ BSY, 0x01

.syntax unified

_start:
	@; Load FLASH controller base address
	ldr r0, =FLASHBASE

	@; Do unlocking sequence
	ldr r1, =KEY1
	str r1, [r0, #FLASH_KEY]
	ldr r1, =KEY2
	str r1, [r0, #FLASH_KEY]

	@; Same for option bytes
	ldr r1, =KEY1
	str r1, [r0, #FLASH_OPTKEY]
	ldr r1, =KEY2
	str r1, [r0, #FLASH_OPTKEY]

	@; Set OPTER bit in FLASH_CR
	ldr r1, [r0, #FLASH_CR]
	orr r1, r1, #OPTER
	str r1, [r0, #FLASH_CR]
	@; Set STRT bit in FLASH_CR
	orr r1, r1, #STRT
	str r1, [r0, #FLASH_CR]

_wait:	@; Wait for BSY bit to clear
	ldr r4, [r0, #FLASH_SR]
	mov r6, #BSY
	tst r4, r6
	bne _wait

	bkpt

