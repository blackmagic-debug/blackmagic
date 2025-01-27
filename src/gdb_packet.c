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

#ifdef EXTERNAL_PACKET_BUFFER
extern gdb_packet_s *gdb_full_packet_buffer(void);
#else
/* This has to be aligned so the remote protocol can re-use it without causing Problems */
static gdb_packet_s BMD_ALIGN_DEF(8) packet_buffer;

static inline gdb_packet_s *gdb_full_packet_buffer(void)
{
	return &packet_buffer;
}

char *gdb_packet_buffer(void)
{
	/* Return the static packet data buffer */
	return packet_buffer.data;
}
#endif /* EXTERNAL_PACKET_BUFFER */

/* https://sourceware.org/gdb/onlinedocs/gdb/Packet-Acknowledgment.html */
void gdb_set_noackmode(const bool enable)
{
	/* Log only changes */
	if (noackmode != enable)
		DEBUG_GDB("%s NoAckMode\n", enable ? "Enabling" : "Disabling");

	noackmode = enable;
}

bool gdb_noackmode(void)
{
	return noackmode;
}

#ifndef DEBUG_GDB_IS_NOOP
/*
 * To debug packets from the perspective of GDB we can use the following command:
 *     set debug remote on
 * This will print packets sent and received by the GDB client
 * 
 * This is not directly related to BMD, but as it's hard to find this information
 * and it is extremely useful for debugging, we included it here for reference.
 */
static void gdb_packet_debug(const char *const func, const gdb_packet_s *const packet)
{
	/* Log packet for debugging */
	DEBUG_GDB("%s: ", func);
	for (size_t i = 0; i < packet->size; i++) {
		const char value = packet->data[i];
		if (value >= ' ' && value < '\x7f')
			DEBUG_GDB("%c", value);
		else
			DEBUG_GDB("\\x%02X", (uint8_t)value);
	}
	DEBUG_GDB("\n");
}
#endif

static inline bool gdb_packet_is_reserved(const char character)
{
	/* Check if the character is a reserved GDB packet character */
	return character == GDB_PACKET_START || character == GDB_PACKET_END || character == GDB_PACKET_ESCAPE ||
		character == GDB_PACKET_RUNLENGTH_START;
}

static uint8_t gdb_packet_checksum(const gdb_packet_s *const packet)
{
	/* Calculate the checksum of the packet */
	uint8_t checksum = 0;
	for (size_t i = 0; i < packet->size; i++) {
		const char character = packet->data[i];
		if (gdb_packet_is_reserved(character))
			checksum += GDB_PACKET_ESCAPE + (character ^ GDB_PACKET_ESCAPE_XOR);
		else
			checksum += character;
	}
	return checksum;
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

gdb_packet_s *gdb_packet_receive(void)
{
	packet_state_e state = PACKET_IDLE; /* State of the packet capture */
	uint8_t rx_checksum = 0;
	gdb_packet_s *packet = gdb_full_packet_buffer();

	while (true) {
		const char rx_char = gdb_if_getchar();

		switch (state) {
		case PACKET_IDLE:
			packet->data[0U] = rx_char;
			if (rx_char == GDB_PACKET_START) {
				/* Start of GDB packet */
				state = PACKET_GDB_CAPTURE;
				packet->size = 0;
				packet->notification = false;
			}
#if CONFIG_BMDA == 0
			else if (rx_char == REMOTE_SOM) {
				/* Start of BMP remote packet */
				/*
				 * Let consume_remote_packet handle this
				 * returns PACKET_IDLE or PACKET_GDB_CAPTURE if it detects the start of a GDB packet
				 */
				state = consume_remote_packet(packet->data, GDB_PACKET_BUFFER_SIZE);
				packet->size = 0;
			}
#endif
			/* EOT (end of transmission) - connection was closed */
			else if (rx_char == '\x04') {
				packet->data[1U] = '\0'; /* Null terminate */
				packet->size = 1U;
				return packet;
			}
			break;

		case PACKET_GDB_CAPTURE:
			if (rx_char == GDB_PACKET_START) {
				/* Restart GDB packet capture */
				packet->size = 0;
				break;
			}
			if (rx_char == GDB_PACKET_END) {
				/* End of GDB packet */

				/* Move to checksum capture */
				state = PACKET_GDB_CHECKSUM_UPPER;
				break;
			}

			/* Add to packet buffer, unless it is an escape char */
			if (rx_char == GDB_PACKET_ESCAPE)
				/* GDB Escaped char */
				state = PACKET_GDB_ESCAPE;
			else
				/* Add to packet buffer */
				packet->data[packet->size++] = rx_char;
			break;

		case PACKET_GDB_ESCAPE:
			/* Resolve escaped char */
			packet->data[packet->size++] = rx_char ^ GDB_PACKET_ESCAPE_XOR;

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
				const bool checksum_ok = gdb_packet_checksum(packet) == rx_checksum;
				gdb_packet_ack(checksum_ok);
				if (!checksum_ok) {
					/* Checksum error, restart packet capture */
					state = PACKET_IDLE;
					break;
				}
			}

			/* Null terminate packet */
			packet->data[packet->size] = '\0';

#ifndef DEBUG_GDB_IS_NOOP
			/* Log packet for debugging */
			gdb_packet_debug(__func__, packet);
#endif

			/* Return captured packet */
			return packet;

		default:
			/* Something is not right, restart packet capture */
			state = PACKET_IDLE;
			break;
		}

		if (packet->size >= GDB_PACKET_BUFFER_SIZE)
			/* Buffer overflow, restart packet capture */
			state = PACKET_IDLE;
	}
}

void gdb_packet_ack(const bool ack)
{
	/* Send ACK/NACK */
	DEBUG_GDB("%s: %s\n", __func__, ack ? "ACK" : "NACK");
	gdb_if_putchar(ack ? GDB_PACKET_ACK : GDB_PACKET_NACK, true);
}

bool gdb_packet_get_ack(const uint32_t timeout)
{
	/* Wait for ACK/NACK */
	const bool ack = gdb_if_getchar_to(timeout) == GDB_PACKET_ACK;
	DEBUG_GDB("%s: %s\n", __func__, ack ? "ACK" : "NACK");
	return ack;
}

static inline void gdb_if_putchar_escaped(const char value)
{
	/* Escape reserved characters */
	if (gdb_packet_is_reserved(value)) {
		gdb_if_putchar(GDB_PACKET_ESCAPE, false);
		gdb_if_putchar((char)((uint8_t)value ^ GDB_PACKET_ESCAPE_XOR), false);
	} else {
		gdb_if_putchar(value, false);
	}
}

void gdb_packet_send(const gdb_packet_s *const packet)
{
	/* Calculate checksum first to avoid re-calculation */
	const uint8_t checksum = gdb_packet_checksum(packet);

	/* Attempt packet transmission up to retries */
	for (size_t attempt = 0U; attempt < GDB_PACKET_RETRIES; attempt++) {
		/* Write start of packet */
		gdb_if_putchar(packet->notification ? GDB_PACKET_NOTIFICATION_START : GDB_PACKET_START, false);

		/* Write packet data */
		for (size_t i = 0; i < packet->size; i++)
			gdb_if_putchar_escaped(packet->data[i]);

		/* Write end of packet */
		gdb_if_putchar(GDB_PACKET_END, false);

		/* Write checksum and flush the buffer */
		gdb_if_putchar(hex_digit(checksum >> 4U), false);
		gdb_if_putchar(hex_digit(checksum & 0xfU), true);

#ifndef DEBUG_GDB_IS_NOOP
		/* Log packet for debugging */
		gdb_packet_debug(__func__, packet);
#endif

		/* Wait for ACK/NACK on standard packets */
		if (packet->notification || noackmode || gdb_packet_get_ack(2000U))
			break;
	}
}

void gdb_put_packet(const char *preamble, size_t preamble_size, const char *data, size_t data_size, bool hex_data)
{
	gdb_packet_s *packet = gdb_full_packet_buffer();

	/*
	 * Create a packet using the internal packet buffer
	 * This destroys the previous packet in the buffer
	 * any packets obtained from gdb_packet_receive() will be invalidated
	 */
	packet->notification = false;
	packet->size = 0;

	/*
	 * Copy the preamble and data into the packet buffer, limited by the buffer size
	 *
	 * This considers GDB_PACKET_BUFFER_SIZE to be the maximum size of the packet
	 * But it does not take into consideration the extra space needed for escaping
	 * This is safe because the escaping is done during the actual packet transmission
	 * but it will result in a packet larger than what we told GDB we could handle
	 */
	if (preamble != NULL && preamble_size > 0) {
		preamble_size = MIN(preamble_size, GDB_PACKET_BUFFER_SIZE);
		memcpy(packet->data, preamble, preamble_size);
		packet->size = preamble_size;
	}

	/* Add the data to the packet buffer and transform it if needed */
	if (data != NULL && data_size > 0) {
		/* Hex data doubles in size */
		if (hex_data)
			data_size *= 2U;

		/* Limit the data size to the remaining space in the packet buffer */
		const size_t remaining_size = GDB_PACKET_BUFFER_SIZE - packet->size;
		data_size = MIN(data_size, remaining_size);

		/* Copy the data into the packet buffer */
		if (hex_data)
			hexify(packet->data + packet->size, data, data_size / 2U);
		else
			memcpy(packet->data + packet->size, data, data_size);
		packet->size += data_size;
	}

	/* Transmit the packet */
	gdb_packet_send(packet);
}

void gdb_putpacket_str_f(const char *const fmt, ...)
{
	gdb_packet_s *packet = gdb_full_packet_buffer();
	/*
	 * Create a packet using the internal packet buffer
	 * This destroys the previous packet in the buffer
	 * any packets obtained from gdb_packet_receive() will be invalidated
	 */
	packet->notification = false;

	/*
	 * Format the string directly into the packet buffer
	 * This considers GDB_PACKET_BUFFER_SIZE to be the maximum size of the string
	 * But it does not take into consideration the extra space needed for escaping
	 * This is safe because the escaping is done during the actual packet transmission
	 * but it will result in a packet larger than what we told GDB we could handle
	 */
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(packet->data, sizeof(packet->data), fmt, ap);
	va_end(ap);

	/* Get the size of the formatted string */
	packet->size = strnlen(packet->data, GDB_PACKET_BUFFER_SIZE);

	/* Transmit the packet */
	gdb_packet_send(packet);
}

void gdb_put_notification_str(const char *const str)
{
	gdb_packet_s *packet = gdb_full_packet_buffer();
	/*
	 * Create a packet using the internal packet buffer
	 * This destroys the previous packet in the buffer
	 * any packets obtained from gdb_packet_receive() will be invalidated
	 */
	packet->notification = true;

	packet->size = strnlen(str, GDB_PACKET_BUFFER_SIZE);
	memcpy(packet->data, str, packet->size);

	/* Transmit the packet */
	gdb_packet_send(packet);
}

void gdb_out(const char *const str)
{
	/**
     * Program console output packet
     * See https://sourceware.org/gdb/current/onlinedocs/gdb.html/Stop-Reply-Packets.html#Stop-Reply-Packets
     * 
     * Format: ‘O XX…’
     * ‘XX…’ is hex encoding of ASCII data, to be written as the program’s console output.
     * 
     * Can happen at any time while the program is running and the debugger should continue to wait for ‘W’, ‘T’, etc.
     * This reply is not permitted in non-stop mode.
     */
	gdb_put_packet("O", 1U, str, strnlen(str, GDB_OUT_PACKET_MAX_SIZE), true);
}

void gdb_voutf(const char *const fmt, va_list ap)
{
	/*
	 * We could technically do the formatting and transformation in a single buffer reducing stack usage
	 * But it is a bit more complex and likely slower, we would need to spread the characters out such
	 * that each occupies two bytes, and then we could hex them in place
	 * 
	 * If this stack usage proves to be a problem, we can revisit this
	 */
	char str_scratch[GDB_OUT_PACKET_MAX_SIZE + 1U];

	/* Format the string into the scratch buffer */
	vsnprintf(str_scratch, sizeof(str_scratch), fmt, ap);

	/* Delegate the rest of the work to gdb_out */
	gdb_out(str_scratch);
}

void gdb_outf(const char *const fmt, ...)
{
	/* Wrap the va_list version of gdb_voutf */
	va_list ap;
	va_start(ap, fmt);
	gdb_voutf(fmt, ap);
	va_end(ap);
}
