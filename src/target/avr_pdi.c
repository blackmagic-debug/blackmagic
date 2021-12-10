#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "avr.h"
#include "exception.h"

static void avr_dp_unref(AVR_DP_t *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

void avr_dp_init(AVR_DP_t *dp)
{
	(void)dp;
}
