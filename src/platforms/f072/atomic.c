/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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

/*
 * This file implements the libatomic special functions that are otherwise missing
 * for this platform. libatomic itself is not included in most compiler distributions
 * for arm-none-eabi, so we implement our own for sanity.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <libopencm3/cm3/cortex.h>

/* Use a sequential consistency barrier on Cortex-M0 (no special-case for relaxed or aquire-release */
__attribute__((always_inline, artificial)) static inline void pre_barrier(int model)
{
	(void)model;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

__attribute__((always_inline, artificial)) static inline void post_barrier(int model)
{
	(void)model;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/* Sequence barriers only issue a fence in sequential consistency mode */
__attribute__((always_inline, artificial)) static inline void pre_seq_barrier(int model)
{
	if (model == __ATOMIC_SEQ_CST)
		__atomic_thread_fence(model);
}

__attribute__((always_inline, artificial)) static inline void post_seq_barrier(int model)
{
	if (model == __ATOMIC_SEQ_CST)
		__atomic_thread_fence(model);
}

/* Begin a protected block (disables interrupts) */
__attribute__((always_inline, artificial)) static inline uint32_t protect_begin(const void *const ptr)
{
	(void)ptr;
	const uint32_t primask = cm_is_masked_interrupts() ? 1U : 0U;
	cm_disable_interrupts();
	return primask;
}

/* End a protected block (re-enables interrupts if they were enabled at the start of the critical section */
__attribute__((always_inline, artificial)) static inline void protect_end(const void *const ptr, uint32_t primask)
{
	(void)ptr;
	if (primask == 0U)
		cm_enable_interrupts();
}

uint16_t atomic_fetch_add_2(uint16_t *const atomic_value, const uint16_t add_value, const int model)
{
	/* Create a model-appropriate sync barrier to start */
	pre_barrier(model);
	/* Now grab the current value of the atomic to be modified */
	uint16_t new_value;
	uint16_t current_value = *atomic_value;
	/* Try, in a loop, doing the addition to the value */
	do {
		new_value = current_value + add_value;
		/*
		 * Try to replace the value store by the atomic by the updated value computed here - if this fails
		 * then we get the new value returned in current_value and can try again.
		 */
	} while (!atomic_compare_exchange_weak_explicit(
		atomic_value, &current_value, new_value, memory_order_relaxed, memory_order_relaxed));
	/* Create a model-appropriate sync barrier to finish */
	post_barrier(model);
	/* Finally, return the value that was in the atomic to complete the operation's contract */
	return current_value;
}

uint16_t atomic_fetch_sub_2(uint16_t *const atomic_value, const uint16_t sub_value, const int model)
{
	/* Create a model-appropriate sync barrier to start */
	pre_barrier(model);
	/* Now grab the current value of the atomic to be modified */
	uint16_t new_value;
	uint16_t current_value = *atomic_value;
	/* Try, in a loop, doing the addition to the value */
	do {
		new_value = current_value - sub_value;
		/*
		 * Try to replace the value store by the atomic by the updated value computed here - if this fails
		 * then we get the new value returned in current_value and can try again.
		 */
	} while (!atomic_compare_exchange_weak_explicit(
		atomic_value, &current_value, new_value, memory_order_relaxed, memory_order_relaxed));
	/* Create a model-appropriate sync barrier to finish */
	post_barrier(model);
	/* Finally, return the value that was in the atomic to complete the operation's contract */
	return current_value;
}

bool atomic_compare_exchange_2(uint16_t *const atomic_value, uint16_t *const expected_value, const uint16_t new_value,
	const bool weak, const int success_model, const int failure_model)
{
	(void)weak;
	(void)failure_model;
	/* Create a model-appropriate sequence barrier to start, and begin a protected block */
	pre_seq_barrier(success_model);
	const uint32_t protect_state = protect_begin(atomic_value);

	/* Read out the current value of the atomic, compare it to the expected */
	const uint16_t old_value = *atomic_value;
	const bool result = old_value == *expected_value;
	/* If it's the expected value, write the new value to complete the RMW cycle */
	if (result)
		*atomic_value = new_value;
	/* Otherwise, uphold the contract required and write the current value to the expected value pointer */
	else
		*expected_value = old_value;

	/* Finish up with a model-appropriate sequence barrier having ended the protected block */
	protect_end(atomic_value, protect_state);
	post_seq_barrier(success_model);
	return result;
}

/* Alias the functions defined to their special names to satisfy the compiler */
/* NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming) */
uint16_t __atomic_fetch_add_2(volatile void *atomic_value, uint16_t add_value, int swap_model)
	__attribute__((alias("atomic_fetch_add_2")));
uint16_t __atomic_fetch_sub_2(volatile void *atomic_value, uint16_t add_value, int swap_model)
	__attribute__((alias("atomic_fetch_sub_2")));
bool __atomic_compare_exchange_2(volatile void *atomic_value, void *expected_value, uint16_t new_value, bool weak,
	int success_model, int failure_model) __attribute__((alias("atomic_compare_exchange_2")));
/* NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming) */
