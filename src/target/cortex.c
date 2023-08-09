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

/*
 * This file implements generic support for ARM Cortex family of processors.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"
#include "cortex_internal.h"

adiv5_access_port_s *cortex_ap(target_s *t)
{
	return ((cortex_priv_s *)t->priv)->ap;
}

void cortex_priv_free(void *priv)
{
	adiv5_ap_unref(((cortex_priv_s *)priv)->ap);
	free(priv);
}

uint32_t cortex_dbg_read32(target_s *const target, const uint16_t src)
{
	/* Translate the offset given in the src parameter into an address in the debug address space and read */
	const cortex_priv_s *const priv = (cortex_priv_s *)target->priv;
	adiv5_access_port_s *const ap = cortex_ap(target);
	uint32_t result = 0;
	adiv5_mem_read(ap, &result, priv->base_addr + src, sizeof(result));
	return result;
}

void cortex_dbg_write32(target_s *const target, const uint16_t dest, const uint32_t value)
{
	/* Translate the offset given int he dest parameter into an address int he debug address space and write */
	const cortex_priv_s *const priv = (cortex_priv_s *)target->priv;
	adiv5_access_port_s *const ap = cortex_ap(target);
	adiv5_mem_write(ap, priv->base_addr + dest, &value, sizeof(value));
}
