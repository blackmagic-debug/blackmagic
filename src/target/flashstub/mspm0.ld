MEMORY { sram (rwx): ORIGIN = 0x20000000, LENGTH = 0x00000400 }

SECTIONS
{
	.text :
	{
		KEEP(*(.entry))
		*(.text.*, .text)
		/* include data so we store stack area in objcopy's output */
		*(.data)
		*(.bss)
	} > sram
}
