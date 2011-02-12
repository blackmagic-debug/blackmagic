
_start:
	ldr r0, _flashbase
	ldr r1, _addr
	mov r2, pc
	add r2, #(_data - . - 2)
	ldr r3, _size
	ldr r5, _flash_write_cmd
_next:
	cbz r3, _done
	@ Write address to FMA
	str r1, [r0]
	@ Write data to FMD
	ldr r4, [r2]
	str r4, [r0, #4]
	@ Write WRITE bit to FMC
	str r5, [r0, #8]
_wait:	@ Wait for WRITE bit to clear
	ldr r4, [r0, #8]
	mov r6, #1
	tst r4, r6
	bne _wait

	sub r3, #1
	add r1, #4
	add r2, #4
	b _next
_done:
	bkpt

@.align 4
.org 0x28
_flashbase:
	.word 0x400FD000
_flash_write_cmd:
	.word 0xA4420001
_addr:
	.word 0
_size:
	.word 4
_data:
	.string "Hello World!\n\0\0\0"
