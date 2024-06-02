MEMORY { sram (rwx): ORIGIN = 0x20000000, LENGTH = 0x00001000 }

SECTIONS
{
	.text :
	{
		KEEP(*(.entry))
		*(.text.*, .text)
	} > sram
}
