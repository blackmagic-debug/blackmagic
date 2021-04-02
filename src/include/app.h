#ifndef __APP_H
#define __APP_H

#ifdef ENABLE_APP

#include "target_internal.h"

/* Return value indicates whether packet was handled. */
int app_handle_packet(char *packet, int len);
/* Called just before polling target halt status. */
void app_poll(target *);
/* Same behavior as cmd_list */
extern const struct command_s app_cmd_list[];
extern const char app_name[];
#endif

#endif
