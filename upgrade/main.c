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
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#   include <lusb0_usb.h>
#else
#   include <usb.h>
#endif

#include <assert.h>

#include "dfu.h"
#include "stm32mem.h"
#include "bindata.h"

#define VERSION "1.0"

#define LOAD_ADDRESS 0x8002000

void banner(void)
{
	puts("\nBlack Magic Probe -- Firmware Upgrade Utility -- Version " VERSION);
	puts("Copyright (C) 2011  Black Sphere Technologies Ltd.");
	puts("License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n");
}


struct usb_device * find_dev(void)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	struct usb_dev_handle *handle;
	char man[40];
	char prod[40];

	usb_find_busses();
	usb_find_devices();

	for(bus = usb_get_busses(); bus; bus = bus->next) {
		for(dev = bus->devices; dev; dev = dev->next) {
			/* Check for ST Microelectronics vendor ID */
			if ((dev->descriptor.idVendor != 0x483) &&
			    (dev->descriptor.idVendor != 0x1d50))
				continue;

			handle = usb_open(dev);
			usb_get_string_simple(handle, dev->descriptor.iManufacturer, man,
						sizeof(man));
			usb_get_string_simple(handle, dev->descriptor.iProduct, prod,
						sizeof(prod));
#if 0
			printf("%s:%s [%04X:%04X] %s : %s\n", bus->dirname, dev->filename,
				dev->descriptor.idVendor, dev->descriptor.idProduct, man, prod);
#endif
			usb_close(handle);

			if (((dev->descriptor.idProduct == 0x5740) ||
			     (dev->descriptor.idProduct == 0x6018)) &&
			   !strcmp(man, "Black Sphere Technologies"))
				return dev;

			if (((dev->descriptor.idProduct == 0xDF11) ||
			     (dev->descriptor.idProduct == 0x6017)) &&
			   !strcmp(man, "Black Sphere Technologies"))
				return dev;
		}
	}
	return NULL;
}

usb_dev_handle * get_dfu_interface(struct usb_device *dev, uint16_t *interface)
{
	int i, j, k;
	struct usb_config_descriptor *config;
	struct usb_interface_descriptor *iface;

	usb_dev_handle *handle;

	for(i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		config = &dev->config[i];

		for(j = 0; j < config->bNumInterfaces; j++) {
			for(k = 0; k < config->interface[j].num_altsetting; k++) {
				iface = &config->interface[j].altsetting[k];
				if((iface->bInterfaceClass == 0xFE) &&
				   (iface->bInterfaceSubClass = 0x01)) {
					handle = usb_open(dev);
					//usb_set_configuration(handle, i);
					usb_claim_interface(handle, j);
					//usb_set_altinterface(handle, k);
					//*interface = j;
					*interface = iface->bInterfaceNumber;
					return handle;
				}
			}
		}
	}
	return NULL;
}

int main(void)
{
	struct usb_device *dev;
	usb_dev_handle *handle;
	uint16_t iface;
	int state;
	uint32_t offset;

	banner();
	usb_init();

retry:
	if(!(dev = find_dev()) || !(handle = get_dfu_interface(dev, &iface))) {
		puts("FATAL: No compatible device found!\n");
#ifdef WIN32
		system("pause");
#endif
		return -1;
	}

	state = dfu_getstate(handle, iface);
	if((state < 0) || (state == STATE_APP_IDLE)) {
		puts("Resetting device in firmware upgrade mode...");
		dfu_detach(handle, iface, 1000);
		usb_release_interface(handle, iface);
		usb_close(handle);
#ifdef WIN32
		Sleep(5000);
#else
		sleep(5);
#endif
		goto retry;
	}
	printf("Found device at %s:%s\n", dev->bus->dirname, dev->filename);

	dfu_makeidle(handle, iface);

	for(offset = 0; offset < bindatalen; offset += 1024) {
		printf("Progress: %d%%\r", (offset*100)/bindatalen);
		fflush(stdout);
		assert(stm32_mem_erase(handle, iface, LOAD_ADDRESS + offset) == 0);
		stm32_mem_write(handle, iface, (void*)&bindata[offset], 1024);
	}
	stm32_mem_manifest(handle, iface);

	usb_release_interface(handle, iface);
	usb_close(handle);

	puts("All operations complete!\n");

#ifdef WIN32
		system("pause");
#endif

	return 0;
}

