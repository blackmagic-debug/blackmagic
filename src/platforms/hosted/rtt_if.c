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

#include <general.h>
#include <unistd.h>
#include <fcntl.h>
#include <rtt_if.h>

/* maybe rewrite this as tcp server */

#ifndef WIN32
#include <termios.h>

typedef struct termios terminal_io_state_s;

/* linux */
static terminal_io_state_s saved_ttystate;
static bool tty_saved = false;

/* set up and tear down */

int rtt_if_init()
{
	terminal_io_state_s ttystate;
	tcgetattr(STDIN_FILENO, &saved_ttystate);
	tty_saved = true;
	tcgetattr(STDIN_FILENO, &ttystate);
	ttystate.c_lflag &= ~ICANON;
	ttystate.c_lflag &= ~ECHO;
	ttystate.c_cc[VMIN] = 1;
	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
	int flags = fcntl(0, F_GETFL, 0);
	fcntl(0, F_SETFL, flags | O_NONBLOCK);
	return 0;
}

int rtt_if_exit()
{
	if (tty_saved)
		tcsetattr(STDIN_FILENO, TCSANOW, &saved_ttystate);
	return 0;
}

/* write buffer to terminal */

uint32_t rtt_write(const char *buf, uint32_t len)
{
	int unused = write(1, buf, len);
	(void)unused;
	return len;
}

/* read character from terminal */

int32_t rtt_getchar()
{
	char ch;
	int len;
	len = read(0, &ch, 1);
	if (len == 1)
		return ch;
	return -1;
}

/* true if no characters available */

bool rtt_nodata()
{
	return false;
}

#else

/* windows, output only */

int rtt_if_init()
{
	return 0;
}

int rtt_if_exit()
{
	return 0;
}

/* write buffer to terminal */

uint32_t rtt_write(const char *buf, uint32_t len)
{
	write(1, buf, len);
	return len;
}

/* read character from terminal */

int32_t rtt_getchar()
{
	return -1;
}

/* true if no characters available */

bool rtt_nodata()
{
	return false;
}

#endif
