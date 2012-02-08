.global _start

_start:
	ldr r0, _flashbase
	ldr r1, _addr
	mov r2, pc
	add r2, #(_data - . - 2)
	ldr r3, _size
	ldr r5, _cr
_next:
	cbz r3, _done
	@ Write PG command to FLASH_CR
	str r5, [r0, #0x10]
	@ Write data to flash (word)
	ldr r4, [r2]
	str r4, [r1]

_wait:	@ Wait for BSY bit to clear
	ldrh r4, [r0, #0x0E]
	mov r6, #1
	tst r4, r6
	bne _wait

	sub r3, #4
	add r1, #4
	add r2, #4
	b _next
_done:
	bkpt

@.align 4
.org 0x28
_cr:
	.word 0x00000201
_flashbase:
	.word 0x40023C00
_addr:
	.word 0x0800bf78
_size:
	.word 8
_data:
	.word 0xAAAAAAAA
	.word 0xBBBBBBBB
	.word 0xCCCCCCCC
