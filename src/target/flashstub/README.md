Flash Stubs
===========

These are simple routines for programming the flash on various Cortex-M
microcontrollers.  The routines should be provided with the naked attribute
as the stack may not be available, and must not make any function calls.
The stub must call `stub_exit(code)` provided by `stub.h` to return control
to the debugger.  Up to 4 word sized parameters may be taken.

These stubs are compiled instructions comma separated hex values in the
resulting `*.stub` files here, which may be included in the drivers for the
specific device.  The drivers call these flash stubs on the target by calling
`cortexm_run_stub` defined in `cortexm.h`.
