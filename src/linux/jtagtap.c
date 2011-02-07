/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2008  Black Sphere Technologies Ltd.
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

/* Low level JTAG implementation using FT2232 with libftdi.
 *
 * Issues:
 * This code is old, rotten and unsupported.
 * Magic numbers everywhere.
 * Should share interface with swdptap.c or at least clean up...
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <assert.h>
#include <alloca.h>

#include <ftdi.h>

#include "general.h"

#define PROVIDE_GENERIC_TAP_SEQ
//#define PROVIDE_GENERIC_TAP_TMS_SEQ
//#define PROVIDE_GENERIC_TAP_TDI_TDO_SEQ
//#define PROVIDE_GENERIC_TAP_TDI_SEQ
#include "jtagtap.h"

#define ALL_ZERO 0xA0
#define TCK 	0x01
#define TDI 	0x02
#define TDO 	0x04
#define TMS 	0x08
#define nSRST 	0x20

static struct ftdi_context ftdic;
static int f;

#define BUF_SIZE 4096
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

static void buffer_flush(void)
{
	assert(ftdi_write_data(&ftdic, outbuf, bufptr) == bufptr);
//	printf("FT2232 buffer flush: %d bytes\n", bufptr);
	bufptr = 0;
}

static int buffer_write(const uint8_t *data, int size)
{
	if((bufptr + size) / BUF_SIZE > 0) buffer_flush();
	memcpy(outbuf + bufptr, data, size);
	bufptr += size;
	return size;
}

static int buffer_read(uint8_t *data, int size)
{
	int index = 0;
	buffer_flush();
	while((index += ftdi_read_data(&ftdic, data + index, size-index)) != size);
	return size;
}


int jtagtap_init(void)
{
	int err;

	if((err = ftdi_init(&ftdic)) != 0) {
		fprintf(stderr, "ftdi_init: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}
	if((err = ftdi_set_interface(&ftdic, INTERFACE_A)) != 0) {
		fprintf(stderr, "ftdi_set_interface: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}
	f = ftdi_usb_open(&ftdic, FT2232_VID, FT2232_PID);
	if(f < 0 && f != -5) {
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n", 
			f, ftdi_get_error_string(&ftdic));
		return -1;
	}
	fprintf(stderr, "ftdi open succeeded(channel 1): %d\n",f);

	if((err = ftdi_set_latency_timer(&ftdic, 1)) != 0) {
		fprintf(stderr, "ftdi_set_latency_timer: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}
	if((err = ftdi_set_baudrate(&ftdic, 1000000)) != 0) {
		fprintf(stderr, "ftdi_set_baudrate: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}
	if((err = ftdi_usb_purge_buffers(&ftdic)) != 0) {
		fprintf(stderr, "ftdi_set_baudrate: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}

	if((err = ftdi_set_bitmode(&ftdic, 0xAB, BITMODE_MPSSE)) != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}

	assert(ftdi_write_data(&ftdic, "\x86\x00\x00\x80\xA8\xAB", 6) == 6);

	if((err = ftdi_write_data_set_chunksize(&ftdic, BUF_SIZE)) != 0) {
		fprintf(stderr, "ftdi_write_data_set_chunksize: %d: %s\n", 
			err, ftdi_get_error_string(&ftdic));
		abort();
	}

	return 0;
}

void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

void jtagtap_srst(void)
{
	buffer_flush();
	//ftdi_write_data(&ftdic, "\x80\x88\xAB", 3);
	//usleep(1000);
	//ftdi_write_data(&ftdic, "\x80\xA8\xAB", 3);
}

#ifndef PROVIDE_GENERIC_TAP_TMS_SEQ
void
jtagtap_tms_seq(uint32_t MS, int ticks)
{
	uint8_t tmp[3] = "\x4B";
	while(ticks >= 0) {
		//jtagtap_next(MS & 1, 1);
		tmp[1] = ticks<7?ticks-1:6;
		tmp[2] = 0x80 | (MS & 0x7F);
		
//		assert(ftdi_write_data(&ftdic, tmp, 3) == 3);
		buffer_write(tmp, 3);
		MS >>= 7; ticks -= 7;
	}
}
#endif

#ifndef PROVIDE_GENERIC_TAP_TDI_SEQ
void 
jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	char *tmp;
	int index = 0;
	int rticks;

	if(!ticks) return;

	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	tmp = alloca(ticks + 9);

	if(ticks) {
		tmp[index++] = 0x19;
		tmp[index++] = ticks - 1;
		tmp[index++] = 0;
		while(ticks--) tmp[index++] = *DI++;
	}

	if(rticks) {
		tmp[index++] = 0x1B;
		tmp[index++] = rticks - 1;
		tmp[index++] = *DI;
	}
	
	if(final_tms) {
		tmp[index++] = 0x4B;
		tmp[index++] = 0;
		tmp[index++] = (*DI)>>rticks?0x81:0x01;
	}
//	assert(ftdi_write_data(&ftdic, tmp, index) == index);
	buffer_write(tmp, index);
}
#endif

#ifndef PROVIDE_GENERIC_TAP_TDI_TDO_SEQ
void 
jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t *tmp;
	int index = 0, rsize;
	int rticks;

	if(!ticks) return;

//	printf("ticks: %d\n", ticks);
	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	tmp = alloca(ticks + 9);
	rsize = ticks;
	if(ticks) {
		tmp[index++] = 0x39;
		tmp[index++] = ticks - 1;
		tmp[index++] = 0;
		while(ticks--) tmp[index++] = *DI++;
	}

	if(rticks) {
		rsize++;
		tmp[index++] = 0x3B;
		tmp[index++] = rticks - 1;
		tmp[index++] = *DI;
	}
	
	if(final_tms) {
		rsize++;
		tmp[index++] = 0x6B;
		tmp[index++] = 0;
		tmp[index++] = (*DI)>>rticks?0x81:0x01;
	}
//	assert(ftdi_write_data(&ftdic, tmp, index) == index);
	buffer_write(tmp, index);
//	index = 0;
//	while((index += ftdi_read_data(&ftdic, tmp + index, rsize-index)) != rsize);
	buffer_read(tmp, rsize);
	/*for(index = 0; index < rsize; index++)
		printf("%02X ", tmp[index]);
	printf("\n");*/
	index = 0;
	if(final_tms) rsize--;
	
	while(rsize--) {
		/*if(rsize) printf("%02X ", tmp[index]);*/
		*DO++ = tmp[index++];
	}
	if(final_tms) {
		rticks++;
		*(--DO) >>= 1;
		*DO |= tmp[index] & 0x80;
	} else DO--;
	if(rticks) {
		*DO >>= (8-rticks);
	}
	/*printf("%02X\n", *DO);*/
}
#endif

uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDO)
{
	uint8_t ret;
	uint8_t tmp[3] = "\x6B\x00\x00";
	tmp[2] = (dTDO?0x80:0) | (dTMS?0x01:0);
//	assert(ftdi_write_data(&ftdic, tmp, 3) == 3);
//	while(ftdi_read_data(&ftdic, &ret, 1) != 1);
	buffer_write(tmp, 3);
	buffer_read(&ret, 1);

	ret &= 0x80;

//	DEBUG("jtagtap_next(TMS = %d, TDO = %d) = %02X\n", dTMS, dTDO, ret);

	return ret;
}

