.global _start

_start:
	ldr r0, _flashbase
	ldr r1, _addr
	mov r2, pc
	add r2, #(_data - . - 2)
	ldr r3, _size
	mov r5, #1
_next:
	cbz r3, _done
	@ Write PG command to FLASH_CR
	str r5, [r0, #0x10]
	@ Write data to flash (half-word)
	ldrh r4, [r2]
	strh r4, [r1]

_wait:	@ Wait for BSY bit to clear
	ldr r4, [r0, #0x0C]
	mov r6, #1
	tst r4, r6
	bne _wait

	sub r3, #2
	add r1, #2
	add r2, #2
	b _next
_done:
	bkpt

@.align 4
.org 0x28
_flashbase:
	.word 0x40022000
_addr:
	.word 0
_size:
	.word 12
_data:
	.word 0xAAAAAAAA
	.word 0xBBBBBBBB
	.word 0xCCCCCCCC
