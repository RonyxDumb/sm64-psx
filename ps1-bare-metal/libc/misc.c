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

#include <stdio.h>
#include "ps1/registers.h"
#include <port/gfx/gfx.h>

static __inline__ void pcsx_putc(int c) { *((volatile char* const)0x1f802080) = c; }
static __inline__ void pcsx_debugbreak() { *((volatile char* const)0x1f802081) = 0; }
static __inline__ void pcsx_execSlot(uint8_t slot) { *((volatile uint8_t* const)0x1f802081) = slot; }
static __inline__ void pcsx_exit(int code) { *((volatile int16_t* const)0x1f802082) = code; }
static __inline__ void pcsx_message(const char* msg) { *((volatile const char** const)0x1f802084) = msg; }
static __inline__ void pcsx_checkKernel(int enable) { *((volatile char*)0x1f802088) = enable; }
static __inline__ int pcsx_isCheckingKernel() { return *((volatile char* const)0x1f802088) != 0; }
static __inline__ int pcsx_present() { return *((volatile uint32_t* const)0x1f802080) == 0x58534350; }

/* Serial port stdin/stdout */

[[gnu::cold]] [[gnu::noinline]] void initSerialIO(int baud) {
	SIO_CTRL(1) = SIO_CTRL_RESET;

	SIO_MODE(1) = SIO_MODE_BAUD_DIV16 | SIO_MODE_DATA_8 | SIO_MODE_STOP_1;
	SIO_BAUD(1) = (F_CPU / 16) / baud;
	SIO_CTRL(1) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_RTS;
}

[[gnu::cold]] [[gnu::noinline]] void _putchar(char ch) {
#ifdef SERIAL
	// The serial interface will buffer but not send any data if the CTS input
	// is not asserted, so we are going to abort if CTS is not set to avoid
	// waiting forever.
	while ((SIO_STAT(1) & (SIO_STAT_TX_NOT_FULL | SIO_STAT_CTS)) == SIO_STAT_CTS)
		__asm__ volatile inline("");

	if (SIO_STAT(1) & SIO_STAT_CTS)
		SIO_DATA(1) = ch;
#endif
	if(pcsx_present()) {
		pcsx_putc(ch);
	}
}

[[gnu::cold]] [[gnu::noinline]] int _getchar(void) {
	while (!(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile inline("");

	return SIO_DATA(1);
}

[[gnu::cold]] [[gnu::noinline]] int _puts(const char *str) {
	int length = 1;

	for (; *str; str++, length++)
		_putchar(*str);

	_putchar('\n');
	return length;
}

/* Abort functions */

[[gnu::cold]] [[gnu::noinline]] [[noreturn]] void _assertAbort(const char *file, int line, const char *expr, const char* screen_msg) {
//#ifndef NDEBUG
	printf("%s:%d: failed assert(%s)\n", file, line, expr);
//#endif
	char line_num_buf[32];
	sprintf(line_num_buf, "line %d", line);
	gfx_show_message_screen(screen_msg, file, line_num_buf);
	if(pcsx_present()) pcsx_debugbreak();
	for (;;)
		__asm__ volatile inline("");
}

[[gnu::cold]] [[gnu::noinline]] [[noreturn]] [[gnu::used]] void abort(void) {
//#ifndef NDEBUG
	puts("abort()");
//#endif
	gfx_show_message_screen("aborted", "", "");
	if(pcsx_present()) pcsx_debugbreak();
	for (;;)
		__asm__ volatile inline("");
}

[[gnu::cold]] [[gnu::noinline]] [[noreturn]] void __cxa_pure_virtual(void) {
//#ifndef NDEBUG
	puts("__cxa_pure_virtual()");
//#endif
	gfx_show_message_screen("__cxa_pure_virtual", "", "");
	if(pcsx_present()) pcsx_debugbreak();
	for (;;)
		__asm__ volatile inline("");
}
