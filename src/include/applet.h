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
#include <stdbool.h>

/* Applet is allowed to handle or override GDB RSP commands.  This can
 * e.g. be used to implement symbol lookup.  Return true to indicate
 * the packet was handled. */
bool applet_handle_packet(char *packet, int len);

/* Called just before polling target halt status.  The Applet can
 * perform target interaction at this point, read/write memory,
 * registers, ... */
void applet_poll(target *);

/* The applet can define commands.  The structure is the same as
 * cmd_list in command.c */
extern const struct command_s applet_cmd_list[];

/* The applet name shows up in the 'help' command. */
extern const char applet_name[];

/* The applet is allowed to take over the main ttyACM, e.g. to
 * implement a different protocol, or a user command console.
 *
 * When gdb_getpacket() receives data that it doesn't recognize, it
 * would normally drop the character.  Instead, the character is
 * passed to the applet through this function.  The applet can then
 * keep calling gdb_if_getchar() to get more input.  For smooth
 * auto-switch operation back to normal BMP operation, the function
 * should return the character if it is one of 0x04, '$' or '!'.
 *
 * If this functionality is not used, the function should return a new
 * character obtained by gdb_if_getchar(). */
char applet_switch_protocol(char c);

#endif
