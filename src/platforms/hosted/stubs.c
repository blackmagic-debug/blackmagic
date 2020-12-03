#include <stdint.h>
#include "platform.h"
#include "ftdi_bmp.h"
#include "jlink.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

cable_desc_t cable_desc[1];

int ftdi_bmp_init(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	return -1;
}
int jlink_init(bmp_info_t *info)
{
	return -1;
}

int jlink_jtagtap_init(bmp_info_t *info, jtag_proc_t *jtag_proc)
{
	return -1;
}

bool jlink_srst_get_val(bmp_info_t *info) {
	return false;
}

void jlink_srst_set_val(bmp_info_t *info, bool assert)
{
}
int jlink_swdp_scan(bmp_info_t *info)
{
	return 0;
}
const char *jlink_target_voltage(bmp_info_t *info)
{
	return "unknown";
}
int jtag_scan_stlinkv2(bmp_info_t *info, const uint8_t *irlens)
{
	return 0;
}

void libftdi_buffer_flush(void)
{
}
int libftdi_jtagtap_init(jtag_proc_t *jtag_proc)
{
	return -1;
}
int libftdi_swdptap_init(swd_proc_t *swd_proc)
{
	return -1;
}

const char *libftdi_target_voltage(void)
{
	return 0;
}

void stlink_adiv5_dp_defaults(ADIv5_DP_t *dp)
{
}
int stlink_enter_debug_swd(bmp_info_t *info, ADIv5_DP_t *dp)
{
	return -1;
}
int stlink_init(bmp_info_t *info)
{
	return -1;
}
int stlink_jtag_dp_init(ADIv5_DP_t *dp)
{
	return false;
}
bool stlink_srst_get_val(void)
{
	return false;
}
void stlink_srst_set_val(bmp_info_t *info, bool assert)
{
}
const char *stlink_target_voltage(bmp_info_t *info)
{
	return "unknown";
}

void 	libusb_close (libusb_device_handle *dev_handle)
{
}

void 	libusb_free_device_list (libusb_device **list, int unref_devices)
{
}
void 	libusb_free_transfer (struct libusb_transfer *transfer)
{
}

int 	libusb_get_device_descriptor (libusb_device *dev, struct libusb_device_descriptor *desc)
{
	return -1;
}
ssize_t 	libusb_get_device_list (libusb_context *ctx, libusb_device ***list)
{
	return -1;
}
int 	libusb_get_string_descriptor_ascii (libusb_device_handle *dev_handle, uint8_t desc_index, unsigned char *data, int length)
{
	return -1;
}
int 	libusb_init (libusb_context **context)
{
	return -1;
}
int 	libusb_open (libusb_device *dev, libusb_device_handle **dev_handle)
{
	return -1;
}

int 	libusb_release_interface (libusb_device_handle *dev_handle, int interface_number)
{
	return -1;
}

const char * 	libusb_strerror (enum libusb_error errcode)
{
	return "libusb not linked in";
}
