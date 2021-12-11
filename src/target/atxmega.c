#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "avr.h"

#define IDCODE_XMEGA256A3U	0x9842U

bool atxmega_probe(target *t)
{
	switch (t->idcode) {
		case IDCODE_XMEGA256A3U:
			t->core = "ATXMega256A3U";
			target_add_ram(t, 0x01002000, 0x01002800);
			avr_add_flash(t, 0x00800000, 0x40000);
			avr_add_flash(t, 0x00840000, 0x2000);
			return true;
	}
	return false;
}
