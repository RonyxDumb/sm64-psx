#include <port/cd.h>
#include <string.h>
#include <stdbool.h>
#include <ps1/registers.h>
#include <ps1/cop0.h>
#include <vendor/printf.h>
#include <assert.h>
#include <port/gfx/gfx.h>

#if !defined(SERIAL) && !defined(BENCH)

#include <port/psx/cd_psx.h>

#define assert_eq(x, y, fmt) ({typeof(x) a = (x); typeof(y) b = (y); if(a != b) {printf("assert_eq failed: lhs = " fmt ", rhs = " fmt "\n", a, b); _assertAbort(__FILE__, __LINE__, #x " == " #y);} a;})

static bool cd_inited = false;
static u32 dat_lba;

#define SECTOR_SIZE 2048
extern u32 gGlobalTimer;

void psx_cd_await_interrupt(u8 expected, u8* result, int result_size) {
	CDROM_ADDRESS = 1; // interrupt flags are only accessible at index 1
	while(true) {
		asm volatile inline("");
		u8 got = CDROM_HINTSTS & 7;
		if(got) { // supposedly the read is a bit unstable, so read twice just in case
			got = CDROM_HINTSTS & 7;

			// acknowledge whichever interrupts we get, but only stop on the expected one
			if(got == expected) {
				for(int i = 0; i < result_size; i++) {
					result[i] = CDROM_RESULT;
				}

				// special handling for INT1, because you're supposed to do this before acknowledging it
				if(got == 1) {
					// request the data and wait for it to be ready
					CDROM_ADDRESS = 0;
					CDROM_HCHPCTL = 0;
					CDROM_HCHPCTL = CDROM_HCHPCTL_BFRD;
					CDROM_ADDRESS = 1;
				}

				CDROM_HCLRCTL = 7; // acknowledge the interrupt
				return;
			} else {
				//assert(got != 5);
				CDROM_HCLRCTL = 7; // acknowledge the interrupt
			}
		}
	}
}

void psx_cd_run_cmd(u8 cmd, const u8* args, int arg_count, u8* result, int result_size) {
	while(CDROM_HSTS & CDROM_HSTS_BUSYSTS) {
		asm volatile inline("");
	}

	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = 7 | CDROM_HCLRCTL_CLRPRM; // clear interrupts, clear param fifo
	delayMicroseconds(3);

	while(!(CDROM_HSTS & CDROM_HSTS_PRMEMPT)) {
		asm volatile inline("");
	}

	CDROM_ADDRESS = 0;
	for(int i = 0; i < arg_count; i++) {
		CDROM_PARAMETER = args[i];
	}
	CDROM_COMMAND = cmd;
	psx_cd_await_interrupt(3, result, result_size);
}

MinSecFrame lba_to_msf(u32 lba) {
	lba += 150;
	return (MinSecFrame) {
		.min = TO_BCD(lba / (75 * 60)),
		.sec = TO_BCD(lba / 75 % 60),
		.frame = TO_BCD(lba % 75)
	};
}

void psx_cd_do_read(u8* buf, u32 logical_block, u32 sector_count, u8* excess_buf) {
	MinSecFrame bak_msf;
	if(cd_playing_audio) {
		u8 stat;
		do {
			psx_cd_run_cmd(CDROM_GETPARAM, NULL, 0, &stat, 1);
		} while(stat & CDROM_STAT_SEEK);
		psx_cd_run_cmd(CDROM_GETLOCL, NULL, 0, bak_msf.bytes, 3);
	}

	psx_cd_run_cmd(CDROM_SETMODE, (const u8[]) {MODE_2X_SPEED}, 1, NULL, 0);
	MinSecFrame msf = lba_to_msf(logical_block);
	psx_cd_run_cmd(CDROM_SETLOC, (u8*) &msf, 3, NULL, 0);
	psx_cd_run_cmd(CDROM_READN, NULL, 0, NULL, 0);
	u32 sector = 0;
	while(true) {
		u8 alignas(u32)* cur_dst;
		if(sector < sector_count) {
			cur_dst = buf + sector * SECTOR_SIZE;
			sector++;
		} else if(excess_buf) {
			cur_dst = excess_buf;
			excess_buf = NULL; // this will make the next iteration exit
		} else {
			break;
		}

		psx_cd_await_interrupt(1, NULL, 0);
		do {
			delayMicroseconds(3);
		} while(!(CDROM_HSTS & CDROM_HSTS_DRQSTS));
		// sector data ready, now we can start a DMA transfer
		DMA_MADR(DMA_CDROM) = (u32) cur_dst;
		DMA_BCR(DMA_CDROM) = SECTOR_SIZE / 4; // the DMA size is in 4-byte words
		DMA_CHCR(DMA_CDROM) = DMA_CHCR_ENABLE | DMA_CHCR_READ | DMA_CHCR_TRIGGER | DMA_CHCR_MODE_BURST;
		// now wait for the DMA
		do {
			delayMicroseconds(10);
		} while(DMA_CHCR(DMA_CDROM) & DMA_CHCR_ENABLE);
	}
	psx_cd_run_cmd(CDROM_PAUSE, NULL, 0, NULL, 0);
	psx_cd_await_interrupt(2, NULL, 0);

	if(cd_playing_audio) {
		psx_cd_run_cmd(CDROM_SETMODE, (const u8[]) {MODE_XA_ADPCM | MODE_XA_SECTOR_FILTER | MODE_2X_SPEED}, 1, NULL, 0);
		psx_cd_run_cmd(CDROM_SETLOC, (u8*) &bak_msf, 3, NULL, 0);
		psx_cd_run_cmd(CDROM_READS, NULL, 0, NULL, 0);
	}
}

#define UNALIGNED_U32(p) ((u32) *(u8*) (p) | (u32) *((u8*) (p) + 1) << 8 | (u32) *((u8*) (p) + 2) << 16 | (u32) *((u8*) (p) + 3) << 24)

// this is not optimized for being called repeatedly, and doesn't support subfolders
// but since we only need 1 file in this game it's perfectly fine
u32 psx_cd_find_file_lba(const char* name) {
	u32 name_len = strlen(name);
	u8 alignas(u32) tmp[SECTOR_SIZE];
	psx_cd_do_read(tmp, 16, 1, NULL); // read the primary volume descriptor
	u32 root_lba = *(u32*) (tmp + 158); // get the pointer to the root folder data from it
	u32 root_len = *(u32*) (tmp + 166);
	psx_cd_do_read(tmp, root_lba, 1, NULL);
	u32 i = 0;
	//printf("root dir lba: %u len: %u\n", root_lba, root_len);
	while(i < root_len && tmp[i] != 0) {
		u8 entry_size = tmp[i];
		u32 contents_lba = UNALIGNED_U32(tmp + i + 2);
		//u32 contents_size = UNALIGNED_U32(tmp + i + 10);
		u8 entry_filename_len = *(tmp + i + 32);
		if(name_len == entry_filename_len && !strncmp((const char*) (tmp + i + 33), name, entry_filename_len)) {
			//printf("file: (len %u) %.*s (size %u contents %u size %u)\n", entry_filename_len, entry_filename_len, tmp + i + 33, entry_size, contents_lba, contents_size);
			return contents_lba;
		}
		i += entry_size;
	}
	abortf("could not find file '%s'\n", name);
}

#endif

#ifdef BENCH

static u8 ext_files_dat[] = {
	#embed <ext_files.dat>
};

void cd_read(void* out, u32 pos, u32 size) {
	assert(pos + size <= sizeof(ext_files_dat));
	memcpy(out, ext_files_dat + pos, size);
}

#elifdef SERIAL

static void sio_send_byte(u8 byte) {
retry:
	if((SIO_STAT(1) & (SIO_STAT_TX_NOT_FULL | SIO_STAT_CTS)) == SIO_STAT_CTS) { // not ready to send; busy wait
		goto retry;
	} else if(SIO_STAT(1) & SIO_STAT_CTS) {
		SIO_DATA(1) = byte;
	} else { // CTS is off and the data won't be accepted anyway
		return;
	}
	if(byte == 0xFF) {
	retry2:
		if((SIO_STAT(1) & (SIO_STAT_TX_NOT_FULL | SIO_STAT_CTS)) == SIO_STAT_CTS) { // not ready to send; busy wait
			goto retry2;
		} else if(SIO_STAT(1) & SIO_STAT_CTS) {
			SIO_DATA(1) = byte;
		} else { // CTS is off and the data won't be accepted anyway
			return;
		}
	}
}

static void sio_send(u8* buf, u32 size) {
	for(u32 i = 0; i < size; i++) {
		sio_send_byte(buf[i]);
	}
}

static void sio_read(u8* buf, u32 size) {
	for(u32 i = 0; i < size; i++) {
		while(!(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY)) {
			asm volatile inline("");
		}
		u8 byte = SIO_DATA(1);
		if(byte == 0xFF) {
			while(!(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY)) {
				asm volatile inline("");
			}
			byte = SIO_DATA(1);
		}
		buf[i] = byte;
	}
}

// wait approximately this amount by running a 2-cycle loop
static void delay_microseconds(u32 us) {
	// convert the microseconds into a cycle count, with 271/8 as an approximation of the CPU clock
	u32 cycles = us * 271 / 8;
	// need to use inline asm to ensure the loop timing is correct
	asm volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz %0, .\n"
		"addiu %0, %0, -2\n"
		".set pop\n"
		: "+r"(cycles)
	);
}

static u8 exchange_byte_timeout(u8 send) {
	sio_send_byte(send);
	for(u32 time = 0; time < 5000000; time += 10) {
		if(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY) {
			return SIO_DATA(1);
		}
		delay_microseconds(10);
	}
	return 0;
}

static void sio_await_handshake() {
	while(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY) {
		UNUSED u8 dummy = SIO_DATA(1);
	}
	while(exchange_byte_timeout(0xDF) != 0xFD) {}
	while(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY) {
		UNUSED u8 dummy = SIO_DATA(1);
	}
}

static u32 read_attempt(void* out, u32 pos, u32 size) {
	//gfx_show_message_screen("loading", "", "");
	sio_await_handshake();
	u8 request[9];
	request[0] = 0xFF;
	request[1] = pos & 0xFF;
	request[2] = pos >> 8 & 0xFF;
	request[3] = pos >> 16 & 0xFF;
	request[4] = pos >> 24 & 0xFF;
	request[5] = size & 0xFF;
	request[6] = size >> 8 & 0xFF;
	request[7] = size >> 16 & 0xFF;
	request[8] = size >> 24 & 0xFF;
	sio_send(request, sizeof(request));
	union {
		u32 value;
		u8 bytes[4];
	} hash;
	sio_read(hash.bytes, 4);
	sio_read(out, size);
	return hash.value;
}

void cd_read(void* out, u32 pos, u32 size) {
	while(true) {
		u32 expected_hash = read_attempt(out, pos, size);
		u32 hash = 0xdabadee;
		for(u32 i = 0; i + 16 <= size; i += 16) {
			u32* chunk = out + i;
			u32 w0 = chunk[0], w1 = chunk[1], w2 = chunk[2], w3 = chunk[3];
			asm volatile inline("");
			hash = __builtin_stdc_rotate_left(hash, 1) + (w0 ^ w1 ^ w2 ^ w3);
		}
		if(hash == expected_hash) {
			break;
		}
		printf("hash mismatch, expected %08x, got %08x, retrying\n", expected_hash, hash);
	}
}

#else

void cd_read(void* out, u32 pos, u32 size) {
	if(cd_inited) {
		gfx_show_message_screen("caricamento", "", "");
	} else {
		BIU_COM_DELAY = 0x1325;
		BIU_DEV5_CTRL = 0x00020943; // enable cdrom bus
		DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_CDROM * 4); // enable cd dma

		CDROM_ADDRESS = 1;
		CDROM_HINTMSK_W = 7; // enable all response interrupt flags
		CDROM_HCLRCTL = 0x7F; // clear any pending responses, clear xa-adpcm buffer, clear param fifo

		// clear request state
		CDROM_ADDRESS = 0;
		CDROM_HCHPCTL = 0;

		// reset cd audio playback in both channels
		CDROM_ADDRESS = 2;
		CDROM_ATV0 = 128;
		CDROM_ATV1 = 0;
		CDROM_ADDRESS = 3;
		CDROM_ATV2 = 128;
		CDROM_ATV3 = 0;
		CDROM_ADPCTL = CDROM_ADPCTL_CHNGATV;

		// initialize
		psx_cd_run_cmd(CDROM_NOP, NULL, 0, NULL, 0);
		psx_cd_run_cmd(CDROM_NOP, NULL, 0, NULL, 0);
		psx_cd_run_cmd(CDROM_INIT, NULL, 0, NULL, 0);
		psx_cd_await_interrupt(2, NULL, 0);
		psx_cd_run_cmd(CDROM_DEMUTE, NULL, 0, NULL, 0);

		dat_lba = psx_cd_find_file_lba("EXT.DAT;1");
		assert(dat_lba);
		cd_inited = true;
	}

	// the segment starts are already aligned to sectors by makextfiles.c
	assert(pos % SECTOR_SIZE == 0);
	u32 sector = dat_lba + pos / SECTOR_SIZE;
	u32 sector_count = size / SECTOR_SIZE;
	if(sector_count) {
		psx_cd_do_read(out, sector, sector_count, NULL);
	}
	u32 excess_size = size % SECTOR_SIZE;
	if(excess_size > size) {
		excess_size = size;
	}
	if(excess_size) {
		u8 alignas(u32) excess[SECTOR_SIZE];
		psx_cd_do_read(excess, sector + sector_count, 1, NULL);
		memcpy(out + sector_count * SECTOR_SIZE, excess, excess_size);
	}
}

#endif
