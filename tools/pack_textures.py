import sys

with open(sys.argv[2], "r") as list_file:
	in_file_list = list_file.read().splitlines()

queued_headers = []
out_pixel_archive_bytes = bytearray()
cur_sector = 0
for in_path in in_file_list:
	assert in_path.endswith(".fulldata")
	with open(in_path, "rb") as in_file:
		contents = in_file.read()
		pixel_data = contents[16:]
		unaligned_len = len(pixel_data)
		sector_count = (unaligned_len + 2047) // 2048
		queued_headers.append((
			in_path.removesuffix(".fulldata") + ".texheader",
			contents[:16]
				+ cur_sector.to_bytes(2, "little")
				+ sector_count.to_bytes(2, "little")
		))
		out_pixel_archive_bytes += pixel_data
		out_pixel_archive_bytes += bytes(0 for _ in range(sector_count * 2048 - unaligned_len))
		cur_sector += sector_count

with open(sys.argv[1], "wb") as out_pixel_archive:
	out_pixel_archive.write(out_pixel_archive_bytes)

for path, contents in queued_headers:
	with open(path, "wb") as out_header:
		out_header.write(contents)
