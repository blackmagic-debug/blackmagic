/*
 * This file is part of the Black Magic Debug project.
 *
 * Written by Kat Mitchell <kat@northernpaws.io>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"
#include "gdb_packet.h"

// Values, registers, etc. are from nrf5340 Specification v1.6:
// https://docs.nordicsemi.com/bundle/ps_nrf5340/page/keyfeatures_html5.html 


// ====================
// General Defines

#define NRF5340_DESIGNER                     0x244U
// NOTE: If checking the part number on the DP, it shows as 0xBD for some reason.
#define NRF5340_PARTNO                       0x70U
#define NRF5340_CTRL_AP_IDR              0x12880000U

#define NRF5340_AHB_AP_APP_NUM 0x0U
#define NRF5340_AHB_AP_NET_NUM 0x1U
#define NRF5340_CTRL_AP_APP_NUM 0x2U
#define NRF5340_CTRL_AP_NET_NUM 0x3U

// ====================
// CTRL AP (Access Port) Registers and Values

// Register addresses are from nRF5340 Specification v1.6 P.g. 829.

// System reset request.
// This register is automatically deactivated during an ERASEALL operation.
#define NRF5340_CTRL_AP_RESET                    ADIV5_AP_REG(0x00U)
// Perform a secure erase of the device, where flash, SRAM, and UICR will be erased in sequence.
// The device will be returned to factory default settings upon next reset.
#define NRF5340_CTRL_AP_REG_ERASEALL                 ADIV5_AP_REG(0x004U)
// This is the status register for the ERASEALL operation
#define NRF5340_CTRL_AP_REG_ERASEALLSTATUS           ADIV5_AP_REG(0x008U)
// This register disables APPROTECT and enables debug access to non-secure mode
#define NRF5340_CTRL_AP_REG_APPROTECT_DISABLE        ADIV5_AP_REG(0x010U)
// This register disables SECUREAPPROTECT and enables debug access to secure mode.
#define NRF5340_CTRL_AP_REG_SECURE_APPROTECT_DISABLE ADIV5_AP_REG(0x014U)
// This is the status register for the UICR ERASEPROTECT configuration.
#define NRF5340_CTRL_AP_REG_ERASEPROTECT_STATUS      ADIV5_AP_REG(0x018U)
// This register disables ERASEPROTECT and performs ERASEALL.
#define NRF5340_CTRL_AP_REG_ERASEPROTECT_DISABLE     ADIV5_AP_REG(0x01CU)
// CTRL-AP Identification Register, IDR.
#define NRF5340_CTRL_AP_REG_IDR                      ADIV5_AP_REG(0x0FCU)

// Status bits for the RESET register.
#define NRF5340_CTRL_AP_RESET_NORESET 0x0U
#define NRF5340_CTRL_AP_RESET_RESET 0x1U

// Status bits for the ERASEALL register.
#define NRF5340_CTRL_AP_ERASEALL_NOOPERATION 0x0U
#define NRF5340_CTRL_AP_ERASEALL_ERASE 0x1U

// Status bits for the ERASEALLSTATUS register.
#define NRF5340_CTRL_AP_ERASEALLSTATUS_READY 0x0U
#define NRF5340_CTRL_AP_ERASEALLSTATUS_BUSY 0x1U

static bool nrf5340_ctrl_ap_mass_erase(target_s *const target, platform_timeout_s *const print_progess) 
{
	return true;
}

/* Handles unlocking/erasing the codes if they've been protected. */
static bool nrf5340_do_unlock(target_s *target)
{
	DEBUG_ERROR("nrf5340_do_unlock not implemented");

	return false;
}


static bool ap_is_protected(adiv5_access_port_s * ap)
{
	// Checks if we can read from the core to indicate it's not protected.
	return (adiv5_ap_read(ap, ADIV5_AP_CSW) & ADIV5_AP_CSW_DBGSWENABLE) == 0;
}

static bool dp_is_protected(adiv5_debug_port_s *const dp, uint8_t apsel)
{
	adiv5_access_port_s ap = {0};
	ap.dp = dp;
	ap.apsel = apsel;
	return ap_is_protected(&ap);
}

/* Log to inform the user that the core needs to be erased to release protection. */
static bool nrf5340_app_protected_message(target_s *target)
{
	tc_printf(target, "Attached in protected mode, please issue 'monitor erase_mass' to regain chip access\n");
			
	/* Patch back in the normal cortexm attach for next time */
	target->attach = cortexm_attach;
	
	return false;
}

/* Log to inform the user that the core needs to be erased to release protection. */
static bool nrf5340_net_protected_message(target_s *target)
{
	tc_printf(target, "Attached in protected mode, please issue 'monitor erase_mass' to regain chip access\n");
			
	/* Patch back in the normal cortexm attach for next time */
	target->attach = cortexm_attach;

	return false;
}

bool nrf5340_ctrl_ap_probe(adiv5_access_port_s *const ap)
{
	DEBUG_INFO("nrf5340_ctrl_ap_probe");

	switch (ap->idr) {
	case NRF5340_CTRL_AP_IDR:
		break;
	default:
		return false;
	}

	// Special case for injecting a dummy target to show that
	// the network core is offline if the application core is
	// protected and the network core isn't powered.
	if (ap->apsel == NRF5340_CTRL_AP_NET_NUM) {
		// If the network core is unpowered then the AP can't be configured.
		adiv5_access_port_s *ap_net = adiv5_new_ap(ap->dp, NRF5340_AHB_AP_NET_NUM);
		if (ap_net == NULL) {
			target_s *target = target_new();
			if (!target)
				return false;
			adiv5_ap_ref(ap);
			target->priv = ap;
			target->priv_free = (void (*)(void *))adiv5_ap_unref;
			gdb_out("nRF5340 Network Core: Unprotect Application Core to bring online.\n");

			target->attach = nrf5340_net_protected_message;
			target->driver = "nRF5340 Network Core (Offline)";
		}
	}

	return true;
}

// nRF5340 Specification v1.6 P.g. 121.
#define NRF5340_FICR_INFO_RAM  0x00FF0218 
#define NRF5340_FICR_INFO_FLASH 0x00FF021C

// nRF5340 Specification v1.6 P.g. 129.
#define NRF5340_UICR 0x00FF8000 
#define NRF5340_RAM  0x20000000U

static bool nrf5340_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	DEBUG_INFO("nrf5340_mass_erase");
	DEBUG_ERROR("nRF5340 mass erase not implemente yet");

	return false;
}

// When one of the cores is online on the nRF5340 it'll be
// picked up by the cortexm probe and processed here.
//
// We need to check if the core(s) are protected or not here
// before letting them be normally attached to and messed with.
bool nrf5340_probe(target_s *const target)
{
	DEBUG_INFO("nrf5340_probe");

	const adiv5_access_port_s *const ap = cortex_ap(target);

	if (ap->dp->version < 2U)
		return false;

	if (ap->designer_code != NRF5340_DESIGNER && ap->partno != NRF5340_PARTNO)
		return false;
	
	// target->target_options |= TOPT_INHIBIT_NRST;
	target->mass_erase = nrf5340_mass_erase; // empty method for now
	
	// Determine which core we're seeing.
	if (ap->apsel == NRF5340_AHB_AP_APP_NUM) {
		// Check if the core is protected so that we can
		// configure special target handling for it.
		if (ap_is_protected(ap)) {
			gdb_out("nRF5340 Application Core: Attach and issue 'monitor erase_mass' to regain chip access.\n");

			target->core = "(Protected)";
			target->driver = "nRF5340 Application Core";
			/*
		     * Overload the default cortexm attach when the nRF5340 is protected.
		     *
		     * This function allows the user to temporarily attach and run a full
		     * device erase to clear the protection on the core.
		     */
			target->attach = nrf5340_app_protected_message;
			target->regs_size = 0x0U;
		} else {
			target->driver = "nRF5340 Application Core";
		}
	} else if (ap->apsel == NRF5340_AHB_AP_NET_NUM) {
		// Check if the core is protected so that we can
		// configure special target handling for it.
		//
		// Note that if the network core is offline (as it
		// is by default), the application code needs to be
		// unprotected first to power it on. A special case
		// it handled in nrf5340_ctrl_ap_probe to detect an
		// offline network core and show a dummy target as
		// feedback to the user.
		if (ap_is_protected(ap)) {
			gdb_out("nRF5340 Network Core: Attach and issue 'monitor erase_mass' to regain chip access.\n");

			target->core = "(Protected)";
			target->driver = "nRF5340 Network Core";
			
			/*
		     * Overload the default cortexm attach when the nRF5340 is protected.
		     *
		     * This function allows the user to temporarily attach and run a full
		     * device erase to clear the protection on the core.
		     */
			target->attach = nrf5340_net_protected_message;

			target->regs_size = 0x0U;
		} else {
			target->driver = "nRF5340 Network Core";
		}
	}
	

	// const uint32_t info_ram = target_mem32_read32(target, NRF5340_FICR_INFO_RAM);
	// target_add_ram32(target, NRF5340_RAM, info_ram * 1024U);
	// add_rram(target, NRF5340_UICR, 0x1000U, 4U);

	return true;
}
