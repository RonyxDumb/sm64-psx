#!/usr/bin/env python3

import argparse
import os.path
import pathlib
import time
from typing import Coroutine

import telnetlib3
import asyncio
import struct
import traceback

connected = False

async def shell(reader: telnetlib3.TelnetReader, writer: telnetlib3.TelnetWriter, archive_path: pathlib.Path, max_chunk_size: int):
	global connected, loaded_files, loaded_file_ids
	connected = True
	print("[server] connected")
	with open(archive_path, "rb") as f:
		loaded_bytes = f.read()
	midline = False
	last_request = time.time()
	while True:
		r = await reader.readexactly(1)
		t = time.time()
		match r:
			case b'\xFF':
				if t >= last_request + 5.0:
					with open(archive_path, "rb") as f:
						loaded_bytes = f.read()
				last_request = t
				pos = int.from_bytes(await reader.readexactly(4), byteorder='little')
				size = int.from_bytes(await reader.readexactly(4), byteorder='little')
				hash = 0xdabadee
				for i in range(pos, pos + size - 15, 16):
					w0, w1, w2, w3 = struct.unpack("<IIII", loaded_bytes[i:i + 16])
					hash = ((hash << 1 | (hash >> 31 & 1)) + (w0 ^ w1 ^ w2 ^ w3)) & 0xFFFFFFFF
				writer.write(struct.pack("<I", hash))
				remaining = size
				while remaining:
					chunk = min(remaining, max_chunk_size)
					data = loaded_bytes[pos:pos + chunk]
					if len(data) < chunk:
						print(f"[server] warning: game requested {chunk} bytes, only have {len(data)} bytes to send")
					writer.write(data)
					await writer.drain()
					remaining -= chunk
					pos += chunk
				print(f"[server] info: sent a segment (pos {pos}, size {size})")
			case b'\xDF':
				writer.write(b'\xFD')
				await writer.drain()
			case b'\xAC':
				print("[server] warning: game is expecting data")
			case c:
				c = c.decode()
				if not midline:
					print("[stdout] ", end="", flush=True)
				print(c, end="", flush=True)
				midline = c != '\n'

async def main():
	parser = argparse.ArgumentParser(description = "server providing files for sm64-psx over serial")
	parser.add_argument("archive_path", type = pathlib.Path, default = None)
	parser.add_argument("serial_address", type = str, default = "localhost:6699")
	parser.add_argument("-s", "--slow", action = "store_true", default = False, help = "use 115200 baud instead of 518400")
	parser.add_argument("-c", "--chunk", type = int, default = 1024, help = "max chunk size")
	args = parser.parse_args()
	assert args.serial_address
	parts = args.serial_address.split(":")
	assert len(parts) == 2
	ip = parts[0]
	port = int(parts[1])
	speed = 115200 if args.slow else 518400

	first_attempt = True
	global connected
	while True:
		try:
			reader, writer = await telnetlib3.open_connection(ip, port, force_binary=True, encoding=False, tspeed=(speed, speed))
			await shell(reader, writer, args.archive_path, args.chunk)
			await writer.protocol.waiter_closed
		except Exception as e:
			if connected:
				first_attempt = False
				connected = False
				print(f"[server] exception: {traceback.format_exc()}")
				print("[server] disconnected, retrying", flush=True)
			elif first_attempt:
				print("[server] awaiting connection", flush=True)
				first_attempt = False

if __name__ == "__main__":
	try:
		asyncio.run(main())
	except KeyboardInterrupt or asyncio.exceptions.CancelledError:
		pass
