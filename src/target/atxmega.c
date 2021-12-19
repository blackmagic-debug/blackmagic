#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "avr.h"

#define IDCODE_XMEGA256A3U	0x9842U

static const char tdesc_xmega6[] =
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"	<architecture>avr:106</architecture>"
	"	<feature name=\"org.gnu.gdb.avr.cpu\">"
	"		<reg name=\"r0\" bitsize=\"8\" regnum=\"0\"/>"
	"		<reg name=\"r1\" bitsize=\"8\"/>"
	"		<reg name=\"r2\" bitsize=\"8\"/>"
	"		<reg name=\"r3\" bitsize=\"8\"/>"
	"		<reg name=\"r4\" bitsize=\"8\"/>"
	"		<reg name=\"r5\" bitsize=\"8\"/>"
	"		<reg name=\"r6\" bitsize=\"8\"/>"
	"		<reg name=\"r7\" bitsize=\"8\"/>"
	"		<reg name=\"r8\" bitsize=\"8\"/>"
	"		<reg name=\"r9\" bitsize=\"8\"/>"
	"		<reg name=\"r10\" bitsize=\"8\"/>"
	"		<reg name=\"r11\" bitsize=\"8\"/>"
	"		<reg name=\"r12\" bitsize=\"8\"/>"
	"		<reg name=\"r13\" bitsize=\"8\"/>"
	"		<reg name=\"r14\" bitsize=\"8\"/>"
	"		<reg name=\"r15\" bitsize=\"8\"/>"
	"		<reg name=\"r16\" bitsize=\"8\"/>"
	"		<reg name=\"r17\" bitsize=\"8\"/>"
	"		<reg name=\"r18\" bitsize=\"8\"/>"
	"		<reg name=\"r19\" bitsize=\"8\"/>"
	"		<reg name=\"r20\" bitsize=\"8\"/>"
	"		<reg name=\"r21\" bitsize=\"8\"/>"
	"		<reg name=\"r22\" bitsize=\"8\"/>"
	"		<reg name=\"r23\" bitsize=\"8\"/>"
	"		<reg name=\"r24\" bitsize=\"8\"/>"
	"		<reg name=\"r25\" bitsize=\"8\"/>"
	"		<reg name=\"r26\" bitsize=\"8\"/>"
	"		<reg name=\"r27\" bitsize=\"8\"/>"
	"		<reg name=\"r28\" bitsize=\"8\"/>"
	"		<reg name=\"r29\" bitsize=\"8\"/>"
	"		<reg name=\"r30\" bitsize=\"8\"/>"
	"		<reg name=\"r31\" bitsize=\"8\"/>"
	"		<reg name=\"sreg\" bitsize=\"8\"/>"
	"		<reg name=\"sp\" bitsize=\"16\" type=\"data_ptr\"/>"
	"		<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
	"	</feature>"
	"</target>";

bool atxmega_probe(target *t)
{
	switch (t->idcode) {
		case IDCODE_XMEGA256A3U:
			t->core = "ATXMega256A3U";
			target_add_ram(t, 0x01002000, 0x01002800);
			avr_add_flash(t, 0x00800000, 0x40000);
			avr_add_flash(t, 0x00840000, 0x2000);
			t->tdesc = tdesc_xmega6;
			return true;
	}
	return false;
}
