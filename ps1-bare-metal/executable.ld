/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

ENTRY(_start)

MEMORY {
	KERNEL_RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0x010000
	/* subtract an arbitrary 0x8000 (32kb) space from the end for the stack */
#ifdef BIG_RAM
	APP_RAM (rwx) : ORIGIN = 0x80010000, LENGTH = 0x7e8000
#else
	APP_RAM (rwx) : ORIGIN = 0x80010000, LENGTH = 0x1e8000
#endif
	SCRATCHPAD (rw)  : ORIGIN = 0x1f800000, LENGTH = 0x000400
}

SECTIONS {
	/* Code sections */
	.text : ALIGN(4) {
		_textStart = .;

		*(.plt .MIPS.stubs)
		*(.text.startup .text.startup.* .text.exit .text.exit.*)
		*(.text.cold .text.cold.* .text.unlikely .text.*_unlikely .text.unlikely.*)
		*(.text.hot .text.hot.*)
		*(.text .text.* .gnu.linkonce.t.*)
		*(.dl_exec .tessellation .bhv_cmd)
	} > APP_RAM

	.rodata : ALIGN(4) {
		*(.rodata .rodata.* .gnu.linkonce.r.*)

		_textEnd = .;
	} > APP_RAM

	/* Global constructor/destructor arrays */

	.preinit_array : ALIGN(4) {
		. = ALIGN((. != 0) ? 4 : 1);
		_preinitArrayStart = .;

		KEEP(*(.preinit_array))

		_preinitArrayEnd = .;
	} > APP_RAM

	.init_array : ALIGN(4) {
		. = ALIGN((. != 0) ? 4 : 1);
		_initArrayStart = .;

		KEEP(*(SORT(.init_array.*) SORT(.ctors.*)))
		KEEP(*(.init_array .ctors))

		_initArrayEnd = .;
	} > APP_RAM

	.fini_array : ALIGN(4) {
		. = ALIGN((. != 0) ? 4 : 1);
		_finiArrayStart = .;

		KEEP(*(.fini_array .dtors))
		KEEP(*(SORT(.fini_array.*) SORT(.dtors.*)))

		_finiArrayEnd = .;
	} > APP_RAM

	/* Data sections */

	.data : ALIGN(4) {
		*(.data .data.* .gnu.linkonce.d.*)
	} > APP_RAM

	/*
	 * Set _gp (copied to $gp) to point to the beginning of .sdata plus 0x8000,
	 * so anything within .sdata and .sbss can be accessed using the $gp
	 * register as base plus a signed 16-bit immediate.
	 */
	.sdata : ALIGN(16) {
		_gp = . + 0x7ff0;

		*(.sdata .sdata.* .gnu.linkonce.s.*)
	} > APP_RAM

	.sbss (NOLOAD) : ALIGN(4) {
		_bssStart = .;

		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
	} > APP_RAM

	.bss (NOLOAD) : ALIGN(4) {
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)

		. = ALIGN((. != 0) ? 4 : 1);
		_bssEnd = .;
	} > APP_RAM

	/* Dummy sections */

	.dummy (NOLOAD) : {
		KEEP(*(.dummy))
	} > APP_RAM

	/DISCARD/ : {
		*(.pdr .MIPS.* .gnu.lto_*)
	}

	/* this only works because i patched convertExecutable.py (line 115) to discard this section */
	/* otherwise it blows up the exe to 1.8gb */
	.scratchpad (NOLOAD) : {
		*(.scratchpad)
	} > SCRATCHPAD
}
