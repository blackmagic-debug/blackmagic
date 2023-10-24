#ifndef FTOA_H
#define FTOA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/*
 The ftoa() function converts a floating point number f into a character string.
 s is the address of a buffer. At most size bytes will be written.
 f is a 32-bit single precision IEEE754 floating point number.
 precision is the the number of digits to appear after the decimal point.
 If precision is negative, all digits are printed, and sscanf() of the printed
 output produces the orginal float. Upon successful return, returns the number
 of characters printed (minus terminating 0).
 */
uint32_t ftoa(char *s, size_t size, float f, int32_t precision);
#ifdef __cplusplus
}
#endif
#endif
