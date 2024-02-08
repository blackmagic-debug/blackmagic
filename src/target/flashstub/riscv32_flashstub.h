#pragma once

/*
 * exit stub, 0  means OK, else error code
 */
static inline void __attribute__((always_inline)) riscv_stub_exit(const int code)
{
	__asm__("li a0, %0 \n"
			"ebreak" ::"i"(code));
}
