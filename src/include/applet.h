#ifndef __APPLET_H
#define __APPLET_H

/* The applet API exposes hooks at key points in the firmware to make
 * it easier to build out-of-tree firmware extensions.
 *
 * See also example_applet/logger_applet.c
 *
 * To build an extension:
 *
 * - Create a .c file that contains implementations of the functions
 *   and data declared in this header.
 *
 * - Provide APPLET_SRC and (optional) APPLET_CFLAGS variables to
 *   make.  See example_applet/build.sh
 *
 */




#include "target_internal.h"

/* Return value indicates whether packet was handled. */
int applet_handle_packet(char *packet, int len);

/* Called just before polling target halt status. */
void applet_poll(target *);

/* Same behavior as cmd_list */
extern const struct command_s applet_cmd_list[];

/* Name that shows up in 'help' command. */
extern const char applet_name[];

/* This is called whenver gdb_getpacket() receives data that doesn't
   make sense, and would normally drop the character.  Instead, the
   character is passed to the app.  App can call gdb_if_getchar()
   until it decides something's wrong and return.  At that point
   gdb_getpacket() continues to look for the next '$' or '!' packet.
   If this is not used, ignore c and return gdb_if_getchar(). */
char applet_switch_protocol(char c);


#endif
