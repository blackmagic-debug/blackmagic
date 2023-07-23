/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Sid Price <sid@sidprice.com>
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

#ifndef PLATFORMS_HOSTED_WINDOWS_FTDI
#define PLATFORMS_HOSTED_WINDOWS_FTDI

/** Port interface for chips with multiple interfaces */
enum ftdi_interface {
	INTERFACE_ANY = 0,
	INTERFACE_A = 1,
	INTERFACE_B = 2,
	INTERFACE_C = 3,
	INTERFACE_D = 4
};

/** Automatic loading / unloading of kernel modules */
enum ftdi_module_detach_mode {
	AUTO_DETACH_SIO_MODULE = 0,
	DONT_DETACH_SIO_MODULE = 1,
	AUTO_DETACH_REATACH_SIO_MODULE = 2
};

/* FTDI MPSSE commands */
#define SET_BITS_LOW 0x80U
/*BYTE DATA*/
/*BYTE Direction*/
#define SET_BITS_HIGH 0x82U
/*BYTE DATA*/
/*BYTE Direction*/
#define GET_BITS_LOW   0x81U
#define GET_BITS_HIGH  0x83U
#define LOOPBACK_START 0x84U
#define LOOPBACK_END   0x85U
#define TCK_DIVISOR    0x86U
/* H Type specific commands */
#define DIS_DIV_5         0x8aU
#define EN_DIV_5          0x8bU
#define EN_3_PHASE        0x8cU
#define DIS_3_PHASE       0x8dU
#define CLK_BITS          0x8eU
#define CLK_BYTES         0x8fU
#define CLK_WAIT_HIGH     0x94U
#define CLK_WAIT_LOW      0x95U
#define EN_ADAPTIVE       0x96U
#define DIS_ADAPTIVE      0x97U
#define CLK_BYTES_OR_HIGH 0x9cU
#define CLK_BYTES_OR_LOW  0x9dU
/*FT232H specific commands */
#define DRIVE_OPEN_COLLECTOR 0x9eU
/* Value Low */
/* Value HIGH */ /*rate is 12000000/((1+value)*2) */
#define DIV_VALUE(rate) ((rate) > 6000000U) ? 0U : ((6000000U / (rate)-1U) > 0xffffU) ? 0xffffU : (6000000U / (rate)-1U)

/* Commands in MPSSE and Host Emulation Mode */
#define SEND_IMMEDIATE 0x87U
#define WAIT_ON_HIGH   0x88U
#define WAIT_ON_LOW    0x89U

/* Commands in Host Emulation Mode */
#define READ_SHORT 0x90U
/* Address_Low */
#define READ_EXTENDED 0x91U
/* Address High */
/* Address Low  */
#define WRITE_SHORT 0x92U
/* Address_Low */
#define WRITE_EXTENDED 0x93U

/* Address High */
/* Address Low  */

/* Shifting commands IN MPSSE Mode*/
#define MPSSE_WRITE_NEG 0x01U /* Write TDI/DO on negative TCK/SK edge*/
#define MPSSE_BITMODE   0x02U /* Write bits, not bytes */
#define MPSSE_READ_NEG  0x04U /* Sample TDO/DI on negative TCK/SK edge */
#define MPSSE_LSB       0x08U /* LSB first */
#define MPSSE_DO_WRITE  0x10U /* Write TDI/DO */
#define MPSSE_DO_READ   0x20U /* Read TDO/DI */
#define MPSSE_WRITE_TMS 0x40U /* Write TMS/CS */

/** MPSSE bitbang modes */
enum ftdi_mpsse_mode {
	BITMODE_RESET = 0x00,   /**< switch off bitbang mode, back to regular serial/FIFO */
	BITMODE_BITBANG = 0x01, /**< classical asynchronous bitbang mode, introduced with B-type chips */
	BITMODE_MPSSE = 0x02,   /**< MPSSE mode, available on 2232x chips */
	BITMODE_SYNCBB = 0x04,  /**< synchronous bitbang mode, available on 2232x and R-type chips  */
	BITMODE_MCU = 0x08,     /**< MCU Host Bus Emulation mode, available on 2232x chips */
	/* CPU-style fifo mode gets set via EEPROM */
	BITMODE_OPTO = 0x10,   /**< Fast Opto-Isolated Serial Interface Mode, available on 2232x chips  */
	BITMODE_CBUS = 0x20,   /**< Bitbang on CBUS pins of R-type chips, configure in EEPROM before */
	BITMODE_SYNCFF = 0x40, /**< Single Channel Synchronous FIFO mode, available on 2232H chips */
	BITMODE_FT1284 = 0x80, /**< FT1284 mode, available on 232H chips */
};

/** FTDI chip type */
enum ftdi_chip_type {
	TYPE_AM = 0,
	TYPE_BM = 1,
	TYPE_2232C = 2,
	TYPE_R = 3,
	TYPE_2232H = 4,
	TYPE_4232H = 5,
	TYPE_232H = 6,
	TYPE_230X = 7,
};

/*
 * Main context structure for all FTD2xx functions.
 * Do not access directly if possible.
*/
struct ftdi_context {
	/* USB specific */
	/** libusb's context */
	struct libusb_context *usb_ctx;
	/** libusb's usb_dev_handle */
	struct libusb_device_handle *usb_dev;
	/** usb read timeout */
	int usb_read_timeout;
	/** usb write timeout */
	int usb_write_timeout;

	/* FTDI specific */
	/** FTDI chip type */
	enum ftdi_chip_type type;
	/** baudrate */
	int baudrate;
	/** bitbang mode state */
	unsigned char bitbang_enabled;
	/** pointer to read buffer for ftdi_read_data */
	unsigned char *readbuffer;
	/** read buffer offset */
	unsigned int readbuffer_offset;
	/** number of remaining data in internal read buffer */
	unsigned int readbuffer_remaining;
	/** read buffer chunk size */
	unsigned int readbuffer_chunksize;
	/** write buffer chunk size */
	unsigned int writebuffer_chunksize;
	/** maximum packet size. Needed for filtering modem status bytes every n packets. */
	unsigned int max_packet_size;

	/* FTDI FT2232C requirecments */
	/** FT2232C interface number: 0 or 1 */
	int interface; /* 0 or 1 */
	/** FT2232C index number: 1 or 2 */
	int index; /* 1 or 2 */
	/* Endpoints */
	/** FT2232C end points: 1 or 2 */
	int in_ep;
	int out_ep; /* 1 or 2 */

	/** Bitbang mode. 1: (default) Normal bitbang mode, 2: FT2232C SPI bitbang mode */
	unsigned char bitbang_mode;

	/** Decoded eeprom structure */
	struct ftdi_eeprom *eeprom;

	/** String representation of last error */
	const char *error_str;

	/** Defines behavior in case a kernel module is already attached to the device */
	enum ftdi_module_detach_mode module_detach_mode;
};

struct ftdi_context *ftdi_new(void);
int ftdi_set_interface(struct ftdi_context *ftdi, enum ftdi_interface interface);
int ftdi_set_baudrate(struct ftdi_context *ftdi, int baudrate);

int ftdi_usb_open_desc(struct ftdi_context *ftdi, int vendor, int product, const char *description, const char *serial);
int ftdi_usb_close(struct ftdi_context *ftdi);
void ftdi_free(struct ftdi_context *ftdi);

int ftdi_usb_purge_buffers(struct ftdi_context *ftdi);

int ftdi_set_latency_timer(struct ftdi_context *ftdi, unsigned char latency);
int ftdi_set_bitmode(struct ftdi_context *ftdi, unsigned char bitmask, unsigned char mode);

int ftdi_read_data(struct ftdi_context *ftdi, unsigned char *buf, int size);

int ftdi_write_data(struct ftdi_context *ftdi, const unsigned char *buf, int size);
int ftdi_write_data_set_chunksize(struct ftdi_context *ftdi, unsigned int chunksize);

const char *ftdi_get_error_string(struct ftdi_context *ftdi);

#endif // PLATFORMS_HOSTED_WINDOWS_FTDI
