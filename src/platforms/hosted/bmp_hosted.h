#if !defined(__BMP_LIBUSB_H)
#define      __BMP_LIBUSB_H

#include "cl_utils.h"

#if HOSTED_BMP_ONLY != 1
# include <libusb-1.0/libusb.h>
struct trans_ctx {
#define TRANS_FLAGS_IS_DONE (1 << 0)
#define TRANS_FLAGS_HAS_ERROR (1 << 1)
    volatile unsigned long flags;
};

typedef struct usb_link_s {
	libusb_context        *ul_libusb_ctx;
	libusb_device_handle  *ul_libusb_device_handle;
	unsigned char         ep_tx;
	unsigned char         ep_rx;
	struct libusb_transfer* req_trans;
	struct libusb_transfer* rep_trans;
	void                  *priv;
} usb_link_t;

int send_recv(usb_link_t *link, uint8_t *txbuf, size_t txsize,
			  uint8_t *rxbuf, size_t rxsize);
#endif
typedef struct bmp_info_s {
	bmp_type_t bmp_type;
	char dev;
	char serial[64];
	char manufacturer[512];
	char product[256];
	char version[256];
	bool is_jtag;
#if HOSTED_BMP_ONLY != 1
	libusb_context *libusb_ctx;
	struct ftdi_context *ftdic;
	usb_link_t *usb_link;
	unsigned int vid;
	unsigned int pid;
	uint8_t interface_num;
	uint8_t in_ep;
	uint8_t out_ep;
#endif
} bmp_info_t;

extern bmp_info_t info;
void bmp_ident(bmp_info_t *info);
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts,bmp_info_t *info);
void libusb_exit_function(bmp_info_t *info);

#endif
