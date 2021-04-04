This directory contains an example app.  It is not intended to be part
of the BMP firmware but is included here for review.

The applet consists of a single C file logger_applet.c that:

- Adds a configuration command

- Polls the target's log buffer and prints messages to GDB console
  when BMP is attached to GDB and the target is running

- Handles some GDB RSP packets to retrieve a target symbol 'config'
  that contains an application-specific configuration structure.

- Takes over the serial console to allow the probe to be used as a
  stand-alone logging monitor, to allow gathering logs without the
  need for GDB to be attached.  (TODO: needs further work)
  
