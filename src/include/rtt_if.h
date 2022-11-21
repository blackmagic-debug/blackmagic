/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021 Koen De Vleeschauwer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INCLUDE_RTT_IF_H
#define INCLUDE_RTT_IF_H

/* rtt i/o to terminal */

/* Define NO_LIBOPENCM3 either in "platform.h" or on the command line
 * in order to avoid including "libopencm3/usb/usbd.h".
 */
#include "platform.h"

#if PC_HOSTED == 0 && !defined(NO_LIBOPENCM3)
#include <libopencm3/usb/usbd.h>

/* usb rx callback */
void rtt_serial_receive_callback(usbd_device *dev, uint8_t ep);
#endif

/* default buffer sizes, 8 bytes added to up buffer for alignment and padding */
/* override RTT_UP_BUF_SIZE and RTT_DOWN_BUF_SIZE in platform.h if needed */

#if !defined(RTT_UP_BUF_SIZE) || !defined(RTT_DOWN_BUF_SIZE)
#if (PC_HOSTED == 1)
#define RTT_UP_BUF_SIZE   (4096U + 8U)
#define RTT_DOWN_BUF_SIZE 512U
#elif defined(STM32F7)
#define RTT_UP_BUF_SIZE   (4096U + 8U)
#define RTT_DOWN_BUF_SIZE 2048U
#elif defined(STM32F4)
#define RTT_UP_BUF_SIZE   (2048U + 8U)
#define RTT_DOWN_BUF_SIZE 256U
#else /* stm32f103 */
#define RTT_UP_BUF_SIZE   (1024U + 8U)
#define RTT_DOWN_BUF_SIZE 256U
#endif
#endif

/* hosted initialisation */
int rtt_if_init(void);
/* hosted teardown */
int rtt_if_exit(void);

/* target to host: write len bytes from the buffer starting at buf. return number bytes written */
uint32_t rtt_write(const char *buf, uint32_t len);
/* host to target: read one character, non-blocking. return character, -1 if no character */
int32_t rtt_getchar();
/* host to target: true if no characters available for reading */
bool rtt_nodata();

#endif /* INCLUDE_RTT_IF_H */
