# Information and terminology guide

Table of Contents:

* [Reset nomenclature](#reset-nomenclature)
* [Multiple-inclusion guarding](#multiple-inclusion-guarding)
* [typedef and structure, enumeration and union naming](#typedef-and-structure-enumeration-and-union-naming)
* [Print format specifiers](#print-format-specifiers)

## Reset nomenclature

Within this code base, we refer to the physical reset pin of a target device by 'nRST'/'nRESET'.
This is because while originally primrily an ARM debugger, the project is now also a debugger for
RISC-V and AVR where the ARM ADIv5 terminology 'SRST' meaning 'System reset' is ambiguous and
oft very confusing.

The history of this is that 'SRST' in AVR and RISC-V mean 'Software Reset' - by which we refer
to a reset initiated over JTAG protocol by poking registers in the target device.
ADIv5 'System Reset' refers to resetting the target using its nRST pin.

A third moving piece to all this is that 'nTRST' (sometimes also referred to as 'JRST') is an
optional pin sometimes found in JTAG interfaces to reset the JTAG machinery itself.

In summary, the following applies:

* In the ADIv5 spec, the physical reset pin is referred to by 'SRST'
* In this code base, we refer to it by 'nRST'
* Referring to it by 'nRST' is also then consistent with the silkscreen naming convention
  on probe host boards and in the ARM 10-pin JTAG pinout
* JTAG physical reset is referred to by 'nTRST'
* Software reset as in the case of JTAG-PDI is referred to by 'SRST' if shortened.

The upshot of this is that to inhibit physical reset in the ARM ADIv5/Cortex-M code, set
`CORTEXM_TOPT_INHIBIT_NRST`, which refers to inhibiting the ADIv5 spec 'SRST'.

## Multiple-inclusion guarding

At this time, the project uses include guard macros in headers to prevent multiple-inclusion issues.
It's simple enough, and it works across platforms and configurations - but how to name the guard macro?

The answer to this question is that you should take the full path to the file, including its name, relative
to the src/ directory this file is in, and turn that into a capitalised, underscored name - for example:

`src/platforms/common/usb.h` => `PLATFORMS_COMMON_USB_H`

This creates a consistent, standards compliant name for the macro that's unique to that header and so
free from conflicts. Please check and define it at the top of the header under the copyright and license
notice, and then close the check block at the bottom of the file with a matching comment like so:

```c
/*
 * [copyright notice]
 */

#ifndef PATH_TO_HEADER_H
#define PATH_TO_HEADER_H

/* [...] contents here */

#endif /*PATH_TO_HEADER_H*/
```

## typedef and structure, enumeration and union naming

Within the code base you will find all kinds of `struct`s, `enum`s, etc. If you find yourself needing to write
a new one or modify an existing one, here are the naming rules we expect to see applied when submitting a pull
request:

* The identifiers used should use lower_snake_case
* The type's name should not be prefixed or suffixed in any way - for example, if you are writing a structure
  to hold information on the Flash for the LPC43xx series, name it `lpc43xx_flash`.
* The type should be `typedef`d as part of its definition with the same name as the bare type, but with a suffix
  for what kind of definition is being `typedef`d added.

The suffixes expected are `_s` for a `struct`, `_e` for an `enum`, `_u` for a `union`, and `_t` for other
miscellaneous types.

A complete example for what this looks like is this:

```c
typedef enum target_halt_reason {
	/* values */
} target_halt_reason_e;

typedef struct lpc43xx_flash {
	/* contents */
} lpc43xx_flash_s;
```

## Print format specifiers

When writing format specifiers for `printf` and friends, care should be taken to ensure they match the variables'
underlying types, taking into consideration that they may change depending on the compiler. To this end, some
standard macros are available to help.

### Non-exhaustive cheat sheet

* decimal:
  - `char`: `%c`
  - `int`, `int8_t`, `int16_t`: `%d`
  - `unsigned int`, `uint8_t`, `uint16_t`: `%u`
  - `uint32_t`: `PRIu32`
  - `int32_t`: `PRId32`
  - `float`: `%f`
  - `size_t`: `%zu`
* hex:
  - `uint8_t`: `PRIx8`
  - `uint16_t`: `PRIx16`
  - `uint32_t`: `PRIx32`

A note on `enum`: its underlying type is not well defined in C11, `enum` **constants** have type `int` (C11 §6.4.4.3),
but `enum` **types** are implementation defined and can have any type compatible with `char`, a _signed integer type_,
or an _unsigned integer type_ (C11 §6.7.2.2).
_GCC_ defaults to `unsigned int` unless there are signed entries in the `enum`,
additionally the flag `short-enums` affects this behaviour.
This makes it difficult to write portable format specifiers, for now `%d` should be used.
