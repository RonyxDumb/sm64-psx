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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

void setupGPU(GP1VideoMode mode, int width, int height) {
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	GP1HorizontalRes horizontalRes = GP1_HRES_320;
	GP1VerticalRes   verticalRes   = GP1_VRES_256;

	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes, verticalRes, mode, false, GP1_COLOR_16BPP
	);
}

void waitForGP0Ready(void) {
	while (!(GPU_GP1 & GP1_STAT_CMD_READY))
		__asm__ volatile inline("");
}

void waitForDMADone(void) {
	while (DMA_CHCR(DMA_GPU) & DMA_CHCR_ENABLE)
		__asm__ volatile inline("");
}

void waitForSPUDMADone(void) {
	while (DMA_CHCR(DMA_SPU) & DMA_CHCR_ENABLE)
		__asm__ volatile inline("");
}

void waitForVSync(void) {
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile inline("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

void sendLinkedList(const void *data) {
	waitForDMADone();
	waitForGP0Ready();
	assert(!((uint32_t) data % 4));

	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_CHCR(DMA_GPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_LIST | DMA_CHCR_ENABLE;
}

void sendVRAMData(const void *data, int x, int y, int width, int height) {
	assert(!((uint32_t) data % 4));

	size_t length = (width * height) / 2;
	size_t chunkSize, numChunks;

	waitForDMADone();
	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	waitForGP0Ready();
	GPU_GP0 = gp0_vramWrite();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;
}

void receiveVRAMData(void *data, int x, int y, int width, int height) {
	waitForDMADone();
	waitForGP0Ready();
	assert(!((uint32_t) data % 4));

	size_t length = (width * height) / 2;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	//while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
	//	__asm__ volatile inline("");

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_READ);

	GPU_GP0 = gp0_vramRead();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	while (!(GPU_GP1 & GP1_STAT_READ_READY))
		__asm__ volatile inline("");

	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = DMA_CHCR_READ | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;
	waitForDMADone();
	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
}

static void set_spu_transfer(unsigned addr, unsigned mode) {
	SPU_DMA_CTRL = 4; // ensures normal transfer behavior
	// clear transfer mode and wait until it is applied
	SPU_CTRL &= ~SPU_CTRL_XFER_BITMASK;
	while((SPU_STAT & SPU_STAT_XFER_BITMASK) != SPU_STAT_XFER_NONE) {
		asm volatile("");
	}
	SPU_ADDR = addr / 8;
	// now set the mode to the desired one
	SPU_CTRL |= mode;
	while((SPU_STAT & SPU_STAT_XFER_BITMASK) != mode) {
		asm volatile("");
	}
}

[[gnu::noinline]] void sendSPUData(const void *data, unsigned addr, unsigned length) {
	assert(!((uint32_t) data % 4));
	assert(!((uint32_t) addr % 8));
	length /= 4;

	waitForSPUDMADone();
	set_spu_transfer(addr, SPU_STAT_XFER_DMA_WRITE);
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
		length = 0;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;
		length %= DMA_MAX_CHUNK_SIZE;

		assert(length % DMA_MAX_CHUNK_SIZE == 0);
	}

	//while (!(SPU_STAT & SPU_STAT_WRITE_REQ))
	//	__asm__ volatile inline("");

	DMA_MADR(DMA_SPU) = (uint32_t) data;
	DMA_BCR (DMA_SPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_SPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;
	waitForSPUDMADone();
}

[[gnu::noinline]] void receiveSPUData(void *data, unsigned addr, unsigned length) {
	assert(!((uint32_t) data % 4));
	assert(!((uint32_t) addr % 8));
	length /= 4;

	waitForSPUDMADone();
	set_spu_transfer(addr, SPU_STAT_XFER_DMA_READ);
	size_t chunkSize, numChunks;
	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
		length = 0;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;
		length %= DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	while (!(SPU_STAT & SPU_STAT_READ_REQ))
		__asm__ volatile inline("");

	DMA_MADR(DMA_SPU) = (uint32_t) data;
	DMA_BCR (DMA_SPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_SPU) = DMA_CHCR_READ | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;
	waitForSPUDMADone();
}

void clearOrderingTable(uint32_t *table, int numEntries) {
	DMA_MADR(DMA_OTC) = (uint32_t) &table[numEntries - 1];
	DMA_BCR (DMA_OTC) = numEntries;
	DMA_CHCR(DMA_OTC) = 0
		| DMA_CHCR_READ | DMA_CHCR_REVERSE | DMA_CHCR_MODE_BURST
		| DMA_CHCR_ENABLE | DMA_CHCR_TRIGGER;

	while (DMA_CHCR(DMA_OTC) & DMA_CHCR_ENABLE)
		__asm__ volatile inline("");
}

//uint32_t *allocatePacket(DMAChain *chain, int zIndex, int numCommands) {
//	uint32_t *ptr      = chain->nextPacket;
//	chain->nextPacket += numCommands + 1;
//
//	assert((zIndex >= 0) && (zIndex < ORDERING_TABLE_SIZE));
//
//	*ptr = gp0_tag(numCommands, (void *) chain->orderingTable[zIndex]);
//	chain->orderingTable[zIndex] = gp0_tag(0, ptr);
//
//	assert(chain->nextPacket < &(chain->data)[CHAIN_BUFFER_SIZE]);
//
//	return &ptr[1];
//}
