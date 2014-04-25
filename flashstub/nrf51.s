.global _start

_start:
	ldr r0, _ready
	ldr r1, _addr
	mov r2, pc
	add r2, #(_data - . - 2)
	ldr r3, _size
_next:
	cmp r3, #0
	beq _done
	@ Write data to flash
	ldr r4, [r2]
	str r4, [r1]

_wait:	@ Wait for READY bit
	ldr r4, [r0]
	mov r6, #1
	tst r4, r6
	beq _wait

	sub r3, #4
	add r1, #4
	add r2, #4
	b _next
_done:
	bkpt

@.align 4
.org 0x24
_ready:
	.word 0x4001E400
_addr:
	.word 0
_size:
	.word 12
_data:
	.word 0xAAAAAAAA
	.word 0xBBBBBBBB
	.word 0xCCCCCCCC
