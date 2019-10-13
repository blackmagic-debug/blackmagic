////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file	src\platforms\ctxLink\wdbp_if.c.
///
/// THis file contains a set of "shims" that are used to direct GDB I/O to the connected
/// debugger. If there is a network connection all I/O is directed through that connection
/// 
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "general.h"
#include "gdb_if.h"
#include "cdcacm.h"
#include "WiFi_Server.h"

#undef gdb_if_putchar
#undef gdb_if_getchar
#undef gdb_if_getchar_to
void gdb_if_putchar(unsigned char c, int flush);
unsigned char gdb_if_getchar(void);
unsigned char gdb_if_getchar_to(int timeout);

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Gdb shim putchar.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="c">	 A char to process.</param>
/// <param name="flush"> The flush.</param>
////////////////////////////////////////////////////////////////////////////////////////////////////

void gdb_shim_putchar(unsigned char c, int flush)
{
	if ( isGDBClientConnected() == true )
	{
		WiFi_gdb_putchar(c, flush);
	}
	else
	{
		gdb_if_putchar(c, flush);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Gdb shim getchar.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <returns> A char.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned char gdb_shim_getchar(void)
{
	if ( isGDBClientConnected() == true )
	{
		return WiFi_GetNext() ;
	}
	else
	{
		if ( cdcacm_get_config() == 1 )
		{
			return gdb_if_getchar() ;
		}
		else
		{
			return (0xFF) ;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Gdb shim getchar to.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
///
/// <param name="timeout"> The timeout.</param>
///
/// <returns> A char.</returns>
////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned char gdb_shim_getchar_to(int timeout)
{
	if ( isGDBClientConnected() == true )
	{
		return WiFi_GetNext_to(timeout) ;
	}
	else
	{
		return gdb_if_getchar_to(timeout) ;
	}
}