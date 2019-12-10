/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Additions by Dave Marples <dave@marples.net>
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
#include "general.h"
#include "gdb_if.h"
#include "version.h"
#include "platform.h"
#include "remote.h"

#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cl_utils.h"

/* Allow 100mS for responses to reach us */
#define RESP_TIMEOUT (100)

/* Define this to see the transactions across the link */
//#define DUMP_TRANSACTIONS

static int f;  /* File descriptor for connection to GDB remote */

int set_interface_attribs (int fd, int speed, int parity)

/* A nice routine grabbed from
 * https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
 */

{
  struct termios tty;
  memset (&tty, 0, sizeof tty);
  if (tcgetattr (fd, &tty) != 0)
    {
      fprintf(stderr,"error %d from tcgetattr", errno);
      return -1;
    }

  cfsetospeed (&tty, speed);
  cfsetispeed (&tty, speed);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
  // disable IGNBRK for mismatched speed tests; otherwise receive break
  // as \000 chars
  tty.c_iflag &= ~IGNBRK;         // disable break processing
  tty.c_lflag = 0;                // no signaling chars, no echo,
  // no canonical processing
  tty.c_oflag = 0;                // no remapping, no delays
  tty.c_cc[VMIN]  = 0;            // read doesn't block
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

  tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

  tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
  // enable reading
  tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if (tcsetattr (fd, TCSANOW, &tty) != 0)
    {
      fprintf(stderr,"error %d from tcsetattr", errno);
      return -1;
    }
  return 0;
}


void platform_init(int argc, char **argv)
{
  BMP_CL_OPTIONS_t cl_opts = {0};
  cl_opts.opt_idstring = "Blackmagic Debug Probe Remote";
  cl_init(&cl_opts, argc, argv);
  char construct[PLATFORM_MAX_MSG_SIZE];

  printf("\nBlack Magic Probe (" FIRMWARE_VERSION ")\n");
  printf("Copyright (C) 2019  Black Sphere Technologies Ltd.\n");
  printf("License GPLv3+: GNU GPL version 3 or later "
	 "<http://gnu.org/licenses/gpl.html>\n\n");

  f=open(cl_opts.opt_serial,O_RDWR|O_SYNC|O_NOCTTY);
  if (f<0)
    {
      fprintf(stderr,"Couldn't open serial port %s\n", cl_opts.opt_serial);
      exit(-1);
    }

  if (set_interface_attribs (f, 115000, 0)<0)
    {
      exit(-1);
    }

  int c=snprintf(construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_START_STR);
  platform_buffer_write((uint8_t *)construct,c);
  c=platform_buffer_read((uint8_t *)construct, PLATFORM_MAX_MSG_SIZE);

  if ((!c) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"Remote Start failed, error %s\n",c?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  printf("Remote is %s\n",&construct[1]);
  if (cl_opts.opt_mode != BMP_MODE_DEBUG) {
	  int ret = cl_execute(&cl_opts);
	  close(f);
	  exit(ret);
  } else {
	  assert(gdb_if_init() == 0);
  }
}

bool platform_target_get_power(void)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_PWR_GET_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);

  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"platform_target_get_power failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return (construct[1]=='1');
}

void platform_target_set_power(bool power)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,REMOTE_PWR_SET_STR,power?'1':'0');
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);

  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"platform_target_set_power failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }
}

void platform_srst_set_val(bool assert)
{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,REMOTE_SRST_SET_STR,assert?'1':'0');
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);

  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"platform_srst_set_val failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }
}

bool platform_srst_get_val(void)

{
  uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_SRST_GET_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);

  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"platform_srst_set_val failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return (construct[1]=='1');
}

void platform_buffer_flush(void)
{

}

int platform_buffer_write(const uint8_t *data, int size)
{
  int s;

#ifdef DUMP_TRANSACTIONS
  printf("%s\n",data);
#endif
  s=write(f,data,size);
  if (s<0)
    {
      fprintf(stderr,"Failed to write\n");
      exit(-2);
    }

  return size;
}

int platform_buffer_read(uint8_t *data, int maxsize)

{
  uint8_t *c;
  int s;
  int ret;
  uint32_t endTime;
  fd_set  rset;
  struct timeval tv;

  c=data;
  tv.tv_sec=0;

  endTime=platform_time_ms()+RESP_TIMEOUT;
  tv.tv_usec=1000*(endTime-platform_time_ms());

  /* Look for start of response */
  do
    {
      FD_ZERO(&rset);
      FD_SET(f, &rset);

      ret = select(f + 1, &rset, NULL, NULL, &tv);
      if (ret < 0)
	{
	  fprintf(stderr,"Failed on select\n");
	  exit(-4);
	}
      if(ret == 0)
	{
	  fprintf(stderr,"Timeout on read\n");
	  exit(-3);
	}

      s=read(f,c,1);
    }
  while ((s>0) && (*c!=REMOTE_RESP));

  /* Now collect the response */
  do
    {
      FD_ZERO(&rset);
      FD_SET(f, &rset);
      ret = select(f + 1, &rset, NULL, NULL, &tv);
      if (ret < 0)
	{
	  fprintf(stderr,"Failed on select\n");
	  exit(-4);
	}
      if(ret == 0)
	{
	  fprintf(stderr,"Timeout on read\n");
	  exit(-3);
	}
      s=read(f,c,1);
      if (*c==REMOTE_EOM)
	{
	  *c=0;
#ifdef DUMP_TRANSACTIONS
	  printf("       %s\n",data);
#endif
	  return (c-data);
	}
      else
	c++;
    }
  while ((s>=0) && (c-data<maxsize));

  fprintf(stderr,"Failed to read\n");
  exit(-3);
  return 0;
}

#if defined(_WIN32) && !defined(__MINGW32__)
#warning "This vasprintf() is dubious!"
int vasprintf(char **strp, const char *fmt, va_list ap)
{
  int size = 128, ret = 0;

  *strp = malloc(size);
  while(*strp && ((ret = vsnprintf(*strp, size, fmt, ap)) == size))
    *strp = realloc(*strp, size <<= 1);

  return ret;
}
#endif

const char *platform_target_voltage(void)

{
  static uint8_t construct[PLATFORM_MAX_MSG_SIZE];
  int s;

  s=snprintf((char *)construct,PLATFORM_MAX_MSG_SIZE,"%s",REMOTE_VOLTAGE_STR);
  platform_buffer_write(construct,s);

  s=platform_buffer_read(construct, PLATFORM_MAX_MSG_SIZE);

  if ((!s) || (construct[0]==REMOTE_RESP_ERR))
    {
      fprintf(stderr,"platform_target_voltage failed, error %s\n",s?(char *)&(construct[1]):"unknown");
      exit(-1);
    }

  return (char *)&construct[1];
}

void platform_delay(uint32_t ms)
{
  usleep(ms * 1000);
}

uint32_t platform_time_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}
