# Serial Wire Out (SWO) User Guide

SWO is a data stream that is sent out of a single pin when the debug interface is in SWD mode.
It can be encoded either using NRZ (UART) or RZ (Manchester) formats. The pin is a dedicated
one that would be used for `TDO` when the debug interface is in `JTAG` mode. On the STM32 it's
port `PB3`.

When in NRZ mode the SWO data rate that comes out of the chip _must_ match the rate that the
debugger expects. By default on BMP the baudrate is 2.25MBps but that can be changed as an
optional parameter to the monitor traceswo command. The following sets the SWO output to
115kBps

```
monitor traceswo 115200
```

The maximum SWO speed is constrained by both the capabilities of the BMP STM32F103 USART and
USB bandwidth. The UART baudrate is set by `b=(72x10^6)/d` where `d >= 16` or a maximum speed
of 4.5Mbps `UART1` and 2.25Mbps on `UART2`.  4.5Mbps is too fast for the USB link when
streaming continuously.

One can safely use the 4.5Mbps setting if the debug data is sent in bursts, or if a different
CPU is used than the STM32F103 as BMP host, but one can potentially run the risk of losing
packets if there is a long runs of data which the USB cannot flush in time (there's a 12K
buffer, so the it is a pretty long run before it becomes a problem).

Note that the baudrate equation means there are only certain speeds available. The highest:
```
BRR        USART1(stlink)  USART2(swlink)
16    	   4.50  Mbps      2.25  Mbps
17	   4.235 Mbps 	   2.118 Mbps
18	   4.000 Mbps 	   2.0   Mbps
19	   3.789 Mbps 	   1.895 Mbps
20	   3.600 Mbps 	   1.8   Mbps
...
24	   3.0   Mbps      1.5   Mbps
...
36         2.0   Mbps      1.0   Mbps
```

The USART will cope with some timing slip, but it's recommended to stay as close to these
values as you can. As the speed comes down the spread between each valid value so miss-timing is
less of an issue. The `monitor traceswo <x>` command will automatically find the closest
divisor to the value you set for the speed, so be aware the error could be significant.

Depending on what you're using to wake up SWO on the target side, you may need code to get it
into the correct mode and emitting data. You can do that via gdb direct memory accesses, or
from program code.

An example for a STM32F103 for the UART (NRZ) data format that we use;

```c
    /* STM32 specific configuration to enable the TRACESWO IO pin */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR |= (2 << 24); // Disable JTAG to release TRACESWO
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // Enable IO trace pins

    TPI->ACPR = 31;  // Output bits at 72000000/(31+1)=2.25MHz.
    TPI->SPPR = 2;   // Use Async mode (1 for RZ/Manchester)
    TPI-FFCR  = 0;   // Disable formatter

    /* Configure instrumentation trace macroblock */
    ITM->LAR = 0xC5ACCE55;
    ITM->TCR = 1 << ITM_TCR_TraceBusID_Pos | ITM_TCR_SYNCENA_Msk |
               ITM_TCR_ITMENA_Msk;
    ITM->TER = 0xFFFFFFFF; // Enable all stimulus ports
```

Code for the STM32L476 might look like:
```c
#define BAUDRATE 115200
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; /* Enable IO pins for Async trace */
    uint32_t divisor, clk_frequency;
    clk_frequency = NutGetCpuClock();
    divisor = clk_frequency / BAUDRATE;
    divisor--;
    TPI->CSPSR = 1; /* port size = 1 bit */
    TPI->ACPR = divisor;
    TPI->SPPR = 2; /*Use Async mode pin protocol */
    TPI->FFCR = 0x00; /* Bypass the TPIU formatter and send output directly*/

/* Configure Trace Port Interface Unit */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Enable access to registers
    DWT->CTRL = 0x400003FE; // DWT needs to provide sync for ITM
    ITM->LAR = 0xC5ACCE55; // Allow access to the Control Register
    ITM->TPR = 0x0000000F; // Trace access privilege from user level code, please
    ITM->TCR = 0x0001000D; // ITM_TCR_TraceBusID_Msk | ITM_TCR_DWTENA_Msk | ITM_TCR_SYNCENA_Msk | ITM_TCR_ITMENA_Msk
    ITM->TER = 1; // Only Enable stimulus port 1

    while(1) {
        for (uint32_t i = 'A'; i <= 'Z'; i++) {
            ITM_SendChar(i);
            NutSleep(1);
        }
    }
```

If you're using RZ mode (for example on Black Magic Probe V2.1 and older), the trace output
speed needs to be set to be quite a lot lower, in the order of 200kHz. To achieve this the
divisor has to be set to something like 359. That's because the STM32F103 (as most
microcontrollers) doesn't have a dedicated RZ decoder so it all has to be done in software. The
advantage of RZ is that the probe can adapt to the speed of the target, so you don't have to
set the speed on the probe in the monitor traceswo command, and it will be tolerant of
different speeds.

The SWO data appears on USB Interface 5, Endpoint 5.

# SWOListen

A program swolisten.c can be found in `./scripts` which will listen to this endpoint, decode
the datastream, and output it to a set of UNIX fifos which can then be used as the input to
other programs (e.g. cat, or something more sophisticated like gnuplot, octave or whatever).
This program doesn't care if the data originates from a RZ or NRZ port, or at what speed.

Note that swolisten can be used with either BMP firmware, or with a conventional TTL serial
dongle. See at the bottom of this file for information on how to use a dongle.

The command line to build the swolisten tool may look like:

E.g. for Ubuntu
```
gcc -I /usr/local/include/libusb-1.0 -L /usr/local/lib swolisten.c -o swolisten -lusb-1.0
```

E.g. For Opensuse:
```
gcc -I /usr/include/libusb-1.0 swolisten.c  swolisten -std=gnu99 -g -Og -lusb-1.0
```

**Note:** Make sure to set the libusb include paths appropriately.

Attach to BMP to your PC:
```sh
> arm-none-eabi-gdb      # Start GDB
gdb> target extended_remote /dev/ttyBmpGdb # Choose BMP as the remote target
gdb> mon traceswo        # Start SWO output
gdb> mon traceswo 115200 # If async SWO is used, set the decoding baud rate that matches the target
gdb> mon swdp_scan       # Scan for the SWD device
gdb> attach 1            # Attach to the device
gdb> run                 # Start the program execution
```

Now start `swolisten` with no options.

By default the tool will create fifos for the first 32 channels in a directory swo (which you
will need to create) as follows;

```sh
> ls swo/
chan00 chan02 chan04 chan06 chan08 chan0A chan0C chan0E chan10 chan12 chan14
chan16 chan18 chan1A chan1C chan1E chan01 chan03 chan05 chan07 chan09 chan0B
chan0D chan0F chan11 chan13 chan15 chan17 chan19 chan1B chan1D chan1F

> cat swo/channel0
<<OUTPUT FROM ITM Channel 0>>
```

With the F103 and L476 examples above, an endless stream of
`ABCDEFGHIJKLMNOPQRSTUVWXYZ` should be seen. During reset of the target
device, no output will appear, but with release of reset output restarts.

Information about command line options can be found with the -h option.  swolisten is
specifically designed to be resilient to probe and target disconnects and restarts. The
intention being to give streams whenever they are available.  It does _not_ require GDB to be
running. For the time being traceswo is not turned on by default in the BMP to avoid possible
interactions and making the overall thing less reliable so it needs to be enabled via the
`monitor traceswo` command in GDB. But after it is enabled it is not necessary to have an
active GDB session.

# Reliability

A whole chunk of work has gone into making sure the dataflow over the SWO link is reliable.
The TL;DR is that the link _is_ reliable. There are factors outside of our control (i.e. the
USB bus you connect to) that could potentially break the reliability but there's not too much
we can do about that since the SWO link is unidirectional (no opportunity for re-transmits).
The following section provides evidence for the claim that the link is good;

A test 'mule' sends data flat out to the link at the maximum data rate of 2.25Mbps using a loop
like the one below;

```c
while (1)
{
    for (uint32_t r=0; r<26; r++)
    {
        for (uint32_t g=0; g<31; g++)
        {
            ITM_SendChar('A'+r);
        }
        ITM_SendChar('\n');
    }
}
```

100MB of data (more than 200MB of actual SWO packets, due to the encoding) was sent from the
mule to the BMP where the output from swolisten `chan00` was cat'ted into a file.

```sh
> cat swo/chan00 > o
```

The experiment was interrupted once the file had grown to 100MB. The first and last lines were
removed from it (these represent previously buffered data and an incomplete packet at the point
where the capture was interrupted) and the resulting file analyzed for consistency;

```sh
> sort o | uniq -c
```

The result was:

```
126462 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
126462 BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
126462 CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
126462 DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
126461 EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
126461 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
126461 GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG
126461 HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
126461 IIIIIIIIIIIIIIIIIIIIIIIIIIIIIII
126461 JJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJ
126461 KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
126461 LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
126461 MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
126461 NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN
126461 OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
126461 PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP
126461 QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ
126461 RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
126461 SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
126461 TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT
126461 UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU
126461 VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
126461 WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
126461 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
126461 YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
126461 ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ
```

On inspection, the last line of recorded data was indeed a 'D' line. Indicating that no data
was lost or corrupted during the experiment.

# Swolisten, using a USB to Serial Adapter

The NRZ data that is sent out from SWO is essentially UART, but in a frame. swolisten has been
extended to accommodate USB to serial adapters that can pick this up. This was successfully
tested with CP2102 adapters at up to 921600 baud.

To use this mode just connect SWO to the RX pin of your dongle, and start swolisten with
parameters representing the speed and port. An example;
```sh
> ./swolisten -p /dev/cu.SLAB_USBtoUART -v -b swo/ -s 921600
```

Any individual adapter will only support certain baudrates (Generally multiples of 115200) so
you may have to experiment to find the best supported ones. For the CP2102 dongle 1.3824Mbps
wasn't supported and 1.8432Mbps returned corrupted data.

Please email dave@marples.net with information about adapters you find work well and at what
speed.

# Further information

* SWO is a wide field. Read e.g. the blogs around SWD on
http://shadetail.com/blog/swo-starting-the-steroids
* An open source program suite for SWO under
active development is https://github.com/mubes/orbuculum
