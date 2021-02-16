#ifndef __SRTT_H
#define __SRTT_H

#include "target.h"

bool srtt_scan(target *t);
bool srtt_available(void);
int srtt_command(target *t, int argc, const char **argv);
void srtt_command_help(void);
void srtt_do_poll(void);

#endif
