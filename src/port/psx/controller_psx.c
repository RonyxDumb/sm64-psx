#include "PR/os_time.h"
#include <stdbool.h>
#include <ultra64.h>
#include <lib/src/osContInternal.h>
#include <ps1/registers.h>
#include <game/rumble_init.h>
#include <game/main.h>
#include <port/gfx/gfx.h>

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
 *
 */

void delayMicroseconds(int time) {
	// Calculate the approximate number of CPU cycles that need to be burned,
	// assuming a 33.8688 MHz clock (1 us = 33.8688 = ~33.875 cycles). The loop
	// consists of a branch and a decrement, thus each iteration will burn 2 CPU
	// cycles.
	time = ((time * 271) + 4) / 8;

	__asm__ volatile inline(
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set reorder\n"
		: "+r"(time)
	);
}

static bool waitForAcknowledge(int timeout) {
	// Controllers and memory cards will acknowledge bytes received by sending
	// short pulses over the DSR line, which will be forwarded by the serial
	// interface to the interrupt controller. This is not guaranteed to happen
	// (it will not if e.g. no device is connected), so we have to implement a
	// timeout to avoid waiting forever in such cases.
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			// Reset the interrupt controller and serial interface's flags to
			// ensure the interrupt can be triggered again.
			IRQ_STAT     = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;

			return true;
		}

		delayMicroseconds(10);
	}

	return false;
}

// As the controller bus is shared with memory cards, an addressing mechanism is
// used to ensure packets are processed by a single device at a time. The first
// byte of each request packet is thus the "address" of the peripheral that
// shall respond to it.
typedef enum {
	ADDR_CONTROLLER  = 0x01,
	ADDR_MEMORY_CARD = 0x81
} DeviceAddress;

// The address is followed by a command byte and any required parameters. The
// only command used in this example (and supported by all controllers) is
// CMD_POLL, however some controllers additionally support a "configuration
// mode" which grants access to an extended command set.
typedef enum {
	CMD_INIT_PRESSURE   = '@', // Initialize DualShock pressure sensors (config)
	CMD_POLL            = 'B', // Read controller state
	CMD_CONFIG_MODE     = 'C', // Enter or exit configuration mode
	CMD_SET_ANALOG      = 'D', // Set analog mode/LED state (config)
	CMD_GET_ANALOG      = 'E', // Get analog mode/LED state (config)
	CMD_GET_MOTOR_INFO  = 'F', // Get information about a motor (config)
	CMD_GET_MOTOR_LIST  = 'G', // Get list of all motors (config)
	CMD_GET_MOTOR_STATE = 'H', // Get current state of vibration motors (config)
	CMD_GET_MODE        = 'L', // Get list of all supported modes (config)
	CMD_REQUEST_CONFIG  = 'M', // Configure poll request format (config)
	CMD_RESPONSE_CONFIG = 'O', // Configure poll response format (config)
	CMD_CARD_READ       = 'R', // Read 128-byte memory card sector
	CMD_CARD_IDENTIFY   = 'S', // Retrieve memory card size information
	CMD_CARD_WRITE      = 'W'  // Write 128-byte memory card sector
} DeviceCommand;

#define DTR_DELAY   60
#define DSR_TIMEOUT 120

static void selectPort(int port) {
	// Set or clear the bit that controls which set of controller and memory
	// card ports is going to have its DTR (port select) signal asserted. The
	// actual serial bus is shared between all ports, however devices will not
	// process packets if DTR is not asserted on the port they are plugged into.
	if(port) {
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	} else {
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;
	}
}

static uint8_t exchangeByte(uint8_t value) {
	// Wait until the interface is ready to accept a byte to send, then wait for
	// it to finish receiving the byte sent by the device.
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL))
		__asm__ volatile inline("");

	SIO_DATA(0) = value;

	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile inline("");

	return SIO_DATA(0);
}

static int exchangePacket(
	DeviceAddress address, const uint8_t *request, uint8_t *response,
	int reqLength, int maxRespLength
) {
	// Reset the interrupt flag and assert the DTR signal to tell the controller
	// or memory card that we're about to send a packet. Devices may take some
	// time to prepare for incoming bytes so we need a small delay here.
	IRQ_STAT     = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(DTR_DELAY);

	int respLength = 0;

	// Send the address byte and wait for the device to respond with a pulse on
	// the DSR line. If no response is received assume no device is connected,
	// otherwise make sure the serial interface's data buffer is empty to
	// prepare for the actual packet transfer.
	SIO_DATA(0) = address;

	if (waitForAcknowledge(DSR_TIMEOUT)) {
		while (SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)
			SIO_DATA(0);

		// Send and receive the packet simultaneously one byte at a time,
		// padding it with zeroes if the packet we are receiving is longer than
		// the data being sent.
		while (respLength < maxRespLength) {
			if (reqLength > 0) {
				*(response++) = exchangeByte(*(request++));
				reqLength--;
			} else {
				*(response++) = exchangeByte(0);
			}

			respLength++;

			// The device will keep sending DSR pulses as long as there is more
			// data to transfer. If no more pulses are received, terminate the
			// transfer.
			if (!waitForAcknowledge(DSR_TIMEOUT))
				break;
		}
	}

	// Release DSR, allowing the device to go idle.
	delayMicroseconds(DTR_DELAY);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;

	return respLength;
}

#define PSX_BTN_SELECT 1
#define PSX_BTN_L3 2
#define PSX_BTN_R3 4
#define PSX_BTN_START 8
#define PSX_BTN_UP 16
#define PSX_BTN_RIGHT 32
#define PSX_BTN_DOWN 64
#define PSX_BTN_LEFT 128
#define PSX_BTN_L2 256
#define PSX_BTN_R2 512
#define PSX_BTN_L1 1024
#define PSX_BTN_R1 2048
#define PSX_BTN_TRIANGLE 4096
#define PSX_BTN_CIRCLE 8192
#define PSX_BTN_CROSS 16384
#define PSX_BTN_SQUARE 32768

static s8 analog_raw_to_n64(u8 raw) {
	s16 signed_val = (s16) (u16) raw - 0x80;
	if(signed_val > 0) signed_val += 1;
	return signed_val * 80 / 0x80;
}

static bool inited = false;
static bool is_connected = false;
static bool is_analog = false;
static bool is_dualshock = false;
static u64 last_poll_us = 0;

void controller_backend_read(OSContPad* pad, u32 port) {
	if(!inited) {
		SIO_CTRL(0) = SIO_CTRL_RESET;
		SIO_MODE(0) = SIO_MODE_BAUD_DIV1 | SIO_MODE_DATA_8;
		SIO_BAUD(0) = F_CPU / 250000;
		SIO_CTRL(0) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_DSR_IRQ_ENABLE;
		inited = true;
	}

	selectPort(port);
	u8 response[8];
	int response_len;

	// controllers apparently reset if not polled for a while, so reconfigure if 200ms have passed
	u64 now_us = osGetTime();
	if(now_us < last_poll_us || now_us >= last_poll_us + 200'000) {
		is_connected = false;
		is_analog = false;
		is_dualshock = false;
		pad->button = 0;
		pad->stick_x = 0;
		pad->stick_y = 0;
		pad->right_stick_x = 0;
		pad->right_stick_y = 0;
	}
	last_poll_us = now_us;

	if(!is_connected) {
		static const u8 request_config[] = {CMD_CONFIG_MODE, 0, 1};
		response_len = exchangePacket(ADDR_CONTROLLER, request_config, response, sizeof(request_config), sizeof(response));
		if(response_len >= 4) {
			static const u8 request_analog[] = {CMD_SET_ANALOG, 0, 1, 3};
			response_len = exchangePacket(ADDR_CONTROLLER, request_analog, response, sizeof(request_analog), sizeof(response));
			if(response_len >= 4) {
				static const u8 request_rumble[] = {CMD_REQUEST_CONFIG, 0, 0, 1, 255, 255, 255, 255};
				response_len = exchangePacket(ADDR_CONTROLLER, request_rumble, response, sizeof(request_rumble), sizeof(response));
				if(response_len >= 4) {
					is_dualshock = true;
				}
				is_analog = true;
			}
		}
		is_connected = true;
	}

	pad->errnum = 0;

	static u8 request_poll[] = {CMD_POLL, 0, 0, 0};
	update_rumble(&request_poll[2], &request_poll[3]);
	if(is_dualshock) {
		request_poll[2] = request_poll[2] * 191 / 255; // weaken the rumble a little bit because the motor is big
	} else {
		request_poll[2] = 0x40;
	}
	response_len = exchangePacket(ADDR_CONTROLLER, request_poll, response, sizeof(request_poll), sizeof(response));
	if(response_len < 4) {
		is_connected = false;
		is_analog = false;
		is_dualshock = false;
		pad->errnum = 1;
		return;
	}
	if(response_len >= 8) {
		is_analog = true;
	}

	// Bytes 2 and 3 hold a bitfield representing the state all buttons. As each
	// bit is active low (i.e. a zero represents a button being pressed), the
	// entire field must be inverted.
	uint16_t buttons = ~(response[2] | response[3] << 8);
	pad->button = 0;
	if(buttons & (PSX_BTN_CROSS)) pad->button |= A_BUTTON;
	if(buttons & (PSX_BTN_SQUARE | PSX_BTN_CIRCLE)) pad->button |= B_BUTTON;
	if(buttons & PSX_BTN_START) pad->button |= START_BUTTON;
	if(buttons & (PSX_BTN_TRIANGLE)) pad->button |= U_CBUTTONS;
	if(buttons & (PSX_BTN_L1 | PSX_BTN_L2)) pad->button |= Z_TRIG;
	if(buttons & (PSX_BTN_L2)) pad->button |= L_TRIG;
	if(buttons & (PSX_BTN_R1)) pad->button |= R_TRIG;

	if(buttons & (PSX_BTN_LEFT)) pad->stick_x = -80;
	else if(buttons & (PSX_BTN_RIGHT)) pad->stick_x = 80;
	else if(is_analog) pad->stick_x = analog_raw_to_n64((s8) response[6]);
	else pad->stick_x = 0;
	if(buttons & (PSX_BTN_DOWN)) pad->stick_y = -80;
	else if(buttons & (PSX_BTN_UP)) pad->stick_y = 80;
	else if(is_analog) pad->stick_y = -analog_raw_to_n64((s8) response[7]);
	else pad->stick_y = 0;

	if(is_analog) {
		pad->right_stick_x = analog_raw_to_n64((s8) response[4]);
		pad->right_stick_y = -analog_raw_to_n64((s8) response[5]);
	} else {
		pad->right_stick_x = 0;
		pad->right_stick_y = 0;
	}
	//if(is_analog) {
	//	if((u8) response[4] >= 128 + 64) {
	//		pad->button |= L_CBUTTONS;
	//	} else if((u8) response[4] <= 128 - 64) {
	//		pad->button |= R_CBUTTONS;
	//	}
	//}
	pad->errnum = 0;

	static u8 last_buttons = 0;
	if((buttons & PSX_BTN_R2) && !(last_buttons & PSX_BTN_R2)) {
		debug_processed_poly_count = 0;
		gShowDebugText = !gShowDebugText;
		gShowProfiler = !gShowProfiler;
	}
	last_buttons = buttons;
}
