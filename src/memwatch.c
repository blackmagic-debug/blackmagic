#include "general.h"
#include "gdb_packet.h"
#include "memwatch.h"
#if PC_HOSTED == 1
#include <unistd.h>
#else
#include "usb_serial.h"
#endif

memwatch_s memwatch_table[MEMWATCH_NUM];
uint32_t memwatch_cnt = 0;

void poll_memwatch(target_s *cur_target)
{
	uint32_t val;
	char buf[64];
	uint32_t len;
	if (!cur_target || (memwatch_cnt == 0))
		return;

	for (uint32_t i = 0; i < memwatch_cnt; i++) {
		if (!target_mem_read(cur_target, &val, memwatch_table[i].addr, sizeof(val)) &&
			(val != memwatch_table[i].value)) {
			switch (memwatch_table[i].format) {
			case MEMWATCH_FMT_SIGNED:
				len = snprintf(buf, sizeof(buf), "%s: %" PRId32 "\r\n", memwatch_table[i].name, val);
				break;
			case MEMWATCH_FMT_UNSIGNED:
				len = snprintf(buf, sizeof(buf), "%s: %" PRIu32 "\r\n", memwatch_table[i].name, val);
				break;
			case MEMWATCH_FMT_HEX:
			default:
				len = snprintf(buf, sizeof(buf), "%s: 0x%" PRIx32 "\r\n", memwatch_table[i].name, val);
				break;
			}
#if PC_HOSTED == 1
			int l = write(1, buf, len);
			(void)l;
#else
			debug_serial_fifo_send(buf, 0, len);
#endif
			memwatch_table[i].value = val;
		}
	}
	return;
}
