OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
	pre_mbr	: ORIGIN = 0x80000000 , LENGTH =    440
	low8k	: ORIGIN = 0x80000200 , LENGTH = 0x1E00
	high8k	: ORIGIN = 0x80002000 , LENGTH = 0x2000
}

SECTIONS
{
	.startup : {
		*(.text.1)
	} > pre_mbr

	.text : {
		*(.text.0)
	} > low8k

	.data : {
		. = ALIGN(4);
		*(.rodata*) *(.data*) *(.scommon*) *(.reginfo*)
	} > low8k

	.bss : {
		. = ALIGN(4);
		_bss_start = ABSOLUTE(.);
		*(.sbss*) *(.bss*)
		. = ALIGN(4);
		_bss_end = ABSOLUTE(.);
	} > high8k

	__stack = 0x80004000;
}
