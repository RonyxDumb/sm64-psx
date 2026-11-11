# PS1 texture banking

The PS1 build converts images into 4-bpp indexed textures (16-color CLUT).
Instead of padding each image to its own 2048-byte CD sector, the asset packer
now stores multiple complete payloads in each **8 KiB / four-sector CD bank**.
Each 20-byte `TexHeader` records the bank's first sector and its 4-byte-aligned
payload offset. Every texture is entirely within one bank.

The runtime maintains **two 8 KiB LRU staging banks (16 KiB RAM)**. The first
texture used from a bank fetches all four sectors in one continuous CD read;
other textures in that bank reuse the same RAM without new disc commands.
After upload, they remain resident in 4-bpp PS1 VRAM and their header records
the GPU page, CLUT and texture-window state. This is not a VRAM eviction cache.

GPU transfers launched by `sendVRAMData()` are asynchronous. The bank cache
waits for GPU DMA before evicting a staging bank so it cannot overwrite an
in-flight upload. It also suppresses the loading-message recursion while
fetching texture banks.

The format is deliberately incompatible with old per-image sector metadata:
**rebuild both the packed archive and the generated texture headers together**.
Normal `make` does this because the packer is a dependency of `tex_pack`.

The format uses the existing 16-bit bank-sector field (up to roughly 128 MiB
of texture data) and rejects oversized or malformed 4-bpp images during packing.
It optimizes **CD seek frequency and disc space**, not fill rate or geometry
cost. A bank miss is still a synchronous CD operation, and busy scenes can
require profiling before changing cache or bank sizes.

Verification: `python3 tests/texture_bank_test.py` exercises packing,
header offsets, payload integrity, multiple banks, and invalid data. GitHub
Actions also parses the PS1 texture module and builds RGB888/RGB555 ISOs.
