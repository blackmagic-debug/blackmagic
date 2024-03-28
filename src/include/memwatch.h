#ifndef MEMWATCH_H
#define MEMWATCH_H

#include <stdint.h>
#include <stdbool.h>
#include <target.h>

#define MEMWATCH_NUM 8
/* string length has to be long enough to store an address 0x20000000 */
#define MEMWATCH_STRLEN 12

typedef enum memwatch_format {
	MEMWATCH_FMT_SIGNED,
	MEMWATCH_FMT_UNSIGNED,
	MEMWATCH_FMT_FLOAT,
	MEMWATCH_FMT_HEX
} memwatch_format_e;

typedef struct {
	uint32_t addr;
	uint32_t value;
	char name[MEMWATCH_STRLEN];
	memwatch_format_e format;
	int32_t precision;
} memwatch_s;

extern memwatch_s memwatch_table[MEMWATCH_NUM];
extern uint32_t memwatch_cnt;
extern bool memwatch_timestamp;
extern void poll_memwatch(target_s *cur_target);

#endif
