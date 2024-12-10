/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file implements the GDB Remote Serial Debugging protocol packet
 * reception and transmission as well as some convenience functions.
 */

#include "general.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "hex_utils.h"
#include "remote.h"

#include <stdarg.h>

typedef enum packet_state {
	PACKET_IDLE,
	PACKET_GDB_CAPTURE,
	PACKET_GDB_ESCAPE,
	PACKET_GDB_CHECKSUM_UPPER,
	PACKET_GDB_CHECKSUM_LOWER,
} packet_state_e;

static bool noackmode = false;

/* https://sourceware.org/gdb/onlinedocs/gdb/Packet-Acknowledgment.html */
void gdb_set_noackmode(bool enable)
{
	/*
	 * If we were asked to disable NoAckMode, and it was previously enabled,
	 * it might mean we got a packet we determined to be the first of a new
	 * GDB session, and as such it was not acknowledged (before GDB enabled NoAckMode),
	 * better late than never.
	 *
	 * If we were asked after the connection was terminated, sending the ack will have no effect.
	 */
	if (!enable && noackmode)
		gdb_if_putchar(GDB_PACKET_ACK, true);

	/* Log only changes */
	if (noackmode != enable)
		DEBUG_GDB("%s NoAckMode\n", enable ? "Enabling" : "Disabling");

	noackmode = enable;
}

packet_state_e consume_remote_packet(char *const packet, const size_t size)
{
#if CONFIG_BMDA == 0
	/* We got what looks like probably a remote control packet */
	size_t offset = 0;
	while (true) {
		/* Consume bytes until we either have a complete remote control packet or have to leave this mode */
		const char rx_char = gdb_if_getchar();

		switch (rx_char) {
		case '\x04':
			packet[0] = rx_char;
			/* EOT (end of transmission) - connection was closed */
			return PACKET_IDLE;

		case REMOTE_SOM:
			/* Oh dear, restart remote packet capture */
			offset = 0;
			break;

		case REMOTE_EOM:
			/* Complete packet for processing */

			/* Null terminate packet */
			packet[offset] = '\0';
			/* Handle packet */
			remote_packet_process(packet, offset);

			/* Restart packet capture */
			packet[0] = '\0';
			return PACKET_IDLE;

		case GDB_PACKET_START:
			/* A 'real' gdb packet, best stop squatting now */
			return PACKET_GDB_CAPTURE;

		default:
			if (offset < size)
				packet[offset++] = rx_char;
			else {
				packet[0] = '\0';
				/* Buffer overflow, restart packet capture */
				return PACKET_IDLE;
			}
		}
	}
#else
	(void)packet;
	(void)size;

	/* Hosted builds ignore remote control packets */
	return PACKET_IDLE;
#endif
}

size_t gdb_getpacket(char *const packet, const size_t size)
{
	packet_state_e state = PACKET_IDLE; /* State of the packet capture */

	size_t offset = 0;
	uint8_t checksum = 0;
	uint8_t rx_checksum = 0;

	while (true) {
		const char rx_char = gdb_if_getchar();

		switch (state) {
		case PACKET_IDLE:
			packet[0U] = rx_char;
			if (rx_char == GDB_PACKET_START) {
				/* Start of GDB packet */
				state = PACKET_GDB_CAPTURE;
				offset = 0;
				checksum = 0;
			}
#if CONFIG_BMDA == 0
			else if (rx_char == REMOTE_SOM) {
				/* Start of BMP remote packet */
				/*
				 * Let consume_remote_packet handle this
				 * returns PACKET_IDLE or PACKET_GDB_CAPTURE if it detects the start of a GDB packet
				 */
				state = consume_remote_packet(packet, size);
				offset = 0;
				checksum = 0;
			}
#endif
			/* EOT (end of transmission) - connection was closed */
			if (packet[0U] == '\x04') {
				packet[1U] = 0;
				return 1U;
			}
			break;

		case PACKET_GDB_CAPTURE:
			if (rx_char == GDB_PACKET_START) {
				/* Restart GDB packet capture */
				offset = 0;
				checksum = 0;
				break;
			}
			if (rx_char == GDB_PACKET_END) {
				/* End of GDB packet */

				/* Move to checksum capture */
				state = PACKET_GDB_CHECKSUM_UPPER;
				break;
			}

			/* Not start or end of packet, add to checksum */
			checksum += rx_char;

			/* Add to packet buffer, unless it is an escape char */
			if (rx_char == GDB_PACKET_ESCAPE)
				/* GDB Escaped char */
				state = PACKET_GDB_ESCAPE;
			else
				/* Add to packet buffer */
				packet[offset++] = rx_char;
			break;

		case PACKET_GDB_ESCAPE:
			/* Add to checksum */
			checksum += rx_char;

			/* Resolve escaped char */
			packet[offset++] = rx_char ^ GDB_PACKET_ESCAPE_XOR;

			/* Return to normal packet capture */
			state = PACKET_GDB_CAPTURE;
			break;

		case PACKET_GDB_CHECKSUM_UPPER:
			/* Checksum upper nibble */
			if (!noackmode)
				/* As per GDB spec, checksums can be ignored in NoAckMode */
				rx_checksum = unhex_digit(rx_char) << 4U; /* This also clears the lower nibble */
			state = PACKET_GDB_CHECKSUM_LOWER;
			break;

		case PACKET_GDB_CHECKSUM_LOWER:
			/* Checksum lower nibble */
			if (!noackmode) {
				/* As per GDB spec, checksums can be ignored in NoAckMode */
				rx_checksum |= unhex_digit(rx_char); /* BITWISE OR lower nibble with upper nibble */

				/* (N)Acknowledge packet */
				gdb_if_putchar(rx_checksum == checksum ? GDB_PACKET_ACK : GDB_PACKET_NACK, true);
			}

			if (noackmode || rx_checksum == checksum) {
				/* Null terminate packet */
				packet[offset] = '\0';

				/* Log packet for debugging */
				DEBUG_GDB("%s: ", __func__);
				for (size_t j = 0; j < offset; j++) {
					const char value = packet[j];
					if (value >= ' ' && value < '\x7f')
						DEBUG_GDB("%c", value);
					else
						DEBUG_GDB("\\x%02X", (uint8_t)value);
				}
				DEBUG_GDB("\n");

				/* Return packet captured size */
				return offset;
			}

			/* Restart packet capture */
			state = PACKET_IDLE;
			break;

		default:
			/* Something is not right, restart packet capture */
			state = PACKET_IDLE;
			break;
		}

		if (offset >= size)
			/* Buffer overflow, restart packet capture */
			state = PACKET_IDLE;
	}
}

static inline bool gdb_get_ack(const uint32_t timeout)
{
	/* Return true early if NoAckMode is enabled */
	if (noackmode)
		return true;

	/* Wait for ACK/NACK */
	return gdb_if_getchar_to(timeout) == GDB_PACKET_ACK;
}

static inline void gdb_putchar(const char value, uint8_t *const csum)
{
	/* Print the character to the debug console */
	if (value >= ' ' && value < '\x7f')
		DEBUG_GDB("%c", value);
	else
		DEBUG_GDB("\\x%02X", (uint8_t)value);

	/* Add to checksum */
	if (csum != NULL)
		*csum += value;

	/* Send the character to the GDB interface */
	gdb_if_putchar(value, false);
}

static inline void gdb_putchar_escaped(const char value, uint8_t *const csum)
{
	/* Escape reserved characters */
	if (value == GDB_PACKET_START || value == GDB_PACKET_END || value == GDB_PACKET_ESCAPE ||
		value == GDB_PACKET_RUNLENGTH_START) {
		gdb_putchar(GDB_PACKET_ESCAPE, csum);
		gdb_putchar((char)((uint8_t)value ^ GDB_PACKET_ESCAPE_XOR), csum);
	} else {
		gdb_putchar(value, csum);
	}
}

static inline void gdb_putchar_hex(const char value, uint8_t *const csum)
{
	/* Hex characters don't need to be escaped as no hex digit is a reserved character */
	gdb_putchar(hex_digit(value >> 4U), csum);
	gdb_putchar(hex_digit(value & 0xffU), csum);
}

void gdb_putpacket(const char *const preamble, const size_t preamble_size, const char *const data,
	const size_t data_size, const bool hexify)
{
	for (size_t attempt = 0U; attempt < GDB_PACKET_RETRIES; attempt++) {
		/* Start debug print packet */
		DEBUG_GDB("%s: ", __func__);

		uint8_t csum = 0; /* Checksum of packet data */

		/* Write start of packet */
		gdb_putchar(GDB_PACKET_START, NULL);

		/*
		 * Write packet preamble if present
		 * The preamble in the context of GDB packets is nothing more than regular data
		 * The distinction in this function is made for convenience as many packets have a constant
		 * value at the start of the packet data, followed by variable data which may need transformation
		 */
		if (preamble != NULL) {
			for (size_t i = 0; i < preamble_size; ++i)
				gdb_putchar_escaped(preamble[i], &csum);
		}

		/* Write packet data, transforming if needed */
		if (data != NULL) {
			if (hexify) {
				for (size_t i = 0; i < data_size; ++i)
					gdb_putchar_hex(data[i], &csum);
			} else {
				for (size_t i = 0; i < data_size; ++i)
					gdb_putchar_escaped(data[i], &csum);
			}
		}

		/* Write end of packet */
		gdb_if_putchar(GDB_PACKET_END, false);

		/* Write checksum and flush the buffer */
		gdb_putchar_hex(csum, NULL);
		gdb_if_flush();

		/* Terminate debug print packet */
		DEBUG_GDB("\n");

		/* Wait for ACK/NACK  */
		if (gdb_get_ack(2000U))
			break;
	}
}

void gdb_put_notification(const char *const data, const size_t size)
{
	/* Start debug print packet */
	DEBUG_GDB("%s: ", __func__);

	uint8_t csum = 0; /* Checksum of notification data */

	/* Write start of notification */
	gdb_if_putchar(GDB_PACKET_NOTIFICATION_START, false);

	/* Write notification data */
	if (data != NULL) {
		for (size_t i = 0; i < size; i++)
			gdb_putchar_escaped(data[i], &csum);
	}

	/* Write end of notification */
	gdb_if_putchar(GDB_PACKET_END, false);

	/* Write checksum and flush the buffer */
	gdb_putchar_hex(csum, NULL);
	gdb_if_flush();

	/* Terminate debug print packet */
	DEBUG_GDB("\n");
}

void gdb_putpacket_f(const char *const fmt, ...)
{
	va_list ap;
	char *buf = NULL;

	va_start(ap, fmt);
	const int size = vasprintf(&buf, fmt, ap);
	if (size < 0) {
		/* Heap exhaustion. Report with puts() elsewhere. */
		DEBUG_ERROR("gdb_putpacket_f: vasprintf failed\n");
	} else {
		gdb_putpacket(NULL, 0, buf, size, false);
		free(buf);
	}
	va_end(ap);
}

void gdb_out(const char *const str)
{
	/**
     * Program console output packet
     * See https://sourceware.org/gdb/current/onlinedocs/gdb.html/Stop-Reply-Packets.html#Stop-Reply-Packets
     * 
     * Format; ‘O XX…’
     * ‘XX…’ is hex encoding of ASCII data, to be written as the program’s console output.
     * 
     * Can happen at any time while the program is running and the debugger should continue to wait for ‘W’, ‘T’, etc.
     * This reply is not permitted in non-stop mode.
     */
	gdb_putpacket("O", 1U, str, strnlen(str, GDB_OUT_PACKET_MAX_SIZE), true);
}

void gdb_voutf(const char *const fmt, va_list ap)
{
	char *buf = NULL;
	if (vasprintf(&buf, fmt, ap) < 0) {
		/* Heap exhaustion. Report with puts() elsewhere. */
		DEBUG_ERROR("gdb_voutf: vasprintf failed\n");
		return;
	}

	gdb_out(buf);
	free(buf);
}

void gdb_outf(const char *const fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	gdb_voutf(fmt, ap);
	va_end(ap);
}
