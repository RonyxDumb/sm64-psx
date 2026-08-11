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

#pragma once

#include <stdint.h>
#include <ps1/gpucmd.h>

#define DMA_MAX_CHUNK_SIZE  16
//#define CHAIN_BUFFER_SIZE   16384
//#define ORDERING_TABLE_SIZE 2048

//typedef struct {
//	uint32_t data[CHAIN_BUFFER_SIZE];
//	uint32_t orderingTable[ORDERING_TABLE_SIZE];
//	uint32_t *nextPacket;
//} DMAChain;

typedef struct {
	uint8_t  u, v;
	uint16_t width, height;
	uint16_t page, clut;
} TextureInfo;

#ifdef __cplusplus
extern "C" {
#endif

void setupGPU(GP1VideoMode mode, int width, int height);
void waitForGP0Ready(void);
void waitForDMADone(void);
void waitForVSync(void);

void sendLinkedList(const void *data);
void sendVRAMData(const void *data, int x, int y, int width, int height);
void receiveVRAMData(void *data, int x, int y, int width, int height);
void sendSPUData(const void *data, unsigned start, unsigned length);
void receiveSPUData(void *data, unsigned start, unsigned length);
void clearOrderingTable(uint32_t *table, int numEntries);
//uint32_t *allocatePacket(DMAChain *chain, int zIndex, int numCommands);

#ifdef __cplusplus
}
#endif
