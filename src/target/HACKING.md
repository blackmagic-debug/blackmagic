# Information and terminology guide

## Reset nomenclature

Within this code base, we refer to the physical reset pin of a target device by 'nRST'/'nRESET'.
This is because while originally primrily an ARM debugger, the project is now also a debugger for
RISC-V and AVR where the ARM ADIv5 terminology 'SRST' meaning 'System reset' is ambiguous and
oft very confusing.

The history of this is that 'SRST' in AVR and RISC-V mean 'Software Reset' - by which we refer
to a reset initiated over JTAG protocol by poking registers in the target device.
ADIv5 'System Reset' refers to resetting the target using its nRST pin.

A third moving piece to all this is that 'nTRST' (sometimes also referred to as 'JRST') is an
optional pin sometimes found in JTAG interfaces to reset the JTAG machinary itself.

In summary, the following applies:

* In the ADIv5 spec, the physical reset pin is referred to by 'SRST'
* In this code base, we refer to it by 'nRST'
* Refering to it by 'nRST' is also then consistent with the silkscreen naming convention
  on probe host boards and in the ARM 10-pin JTAG pinout
* JTAG physical reset is referred to by 'nTRST'
* Software reset as in the case of JTAG-PDI is refered to by 'SRST' if shortened.

The upshot of this is that to inhibit physical reset in the ARM ADIv5/Cortex-M code, set
`CORTEXM_TOPT_INHIBIT_NRST`, which refers to inhibiting the ADIv5 spec 'SRST'.
