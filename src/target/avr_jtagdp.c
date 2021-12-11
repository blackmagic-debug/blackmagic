#include "general.h"
#include "exception.h"
#include "avr.h"
#include "jtag_scan.h"
#include "jtagtap.h"

void avr_jtag_dp_handler(uint8_t jd_index, uint32_t j_idcode)
{
	AVR_DP_t *dp = calloc(1, sizeof(*dp));
	if (!dp) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}
	(void)j_idcode;

	dp->dp_jd_index = jd_index;
	dp->idcode = jtag_devs[jd_index].jd_idcode;
	if ((PC_HOSTED == 0) || (!platform_avr_jtag_dp_init(dp))) {
		//
	}
	avr_dp_init(dp);
}
