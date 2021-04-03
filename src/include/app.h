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

/* Name that shows up in 'help' command. */
extern const char app_name[];

/* This is called whenver gdb_getpacket() receives data that doesn't
   make sense, and would normally drop the character.  Instead, the
   character is passed to the app.  App can call gdb_if_getchar()
   until it decides something's wrong and return.  At that point
   gdb_getpacket() continues to look for the next '$' or '!' packet.
   If this is not used, just return. */
void app_switch_protocol(char c);


#endif

#endif
