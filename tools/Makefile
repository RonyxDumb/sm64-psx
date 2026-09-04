include ../util.mk

HOST_ENV := $(shell uname 2>/dev/null || echo Unknown)
HOST_ENV := $(patsubst MINGW%,MinGW,$(HOST_ENV))

# Returns the path to the command $(1) if exists. Otherwise returns an empty string.
find-command = $(shell which $(1) 2>/dev/null)
ifneq ($(call find-command,gcc),)
CC           := gcc
CXX          := gcc++
else ifneq ($(call find-command,clang),)
CC           := clang
CXX          := clang++
else
$(error no suitable C and C++ compiler found)
endif

CFLAGS       := -I. -Iinclude -Wall -Wextra -Wno-unused-parameter -pedantic -O3 -march=native -s -g
LDFLAGS      := -lm -lstdc++
ALL_PROGRAMS := textconv aifc_decode aiff_extract_codebook tabledesign extract_data_for_mio skyconv makextfiles compress_mario_anims psx_sample_gen convert_image_psx n64graphics mio0
LIBAUDIOFILE := audiofile/libaudiofile.a

BUILD_PROGRAMS := $(ALL_PROGRAMS:%=build/%)

default: all

build/makextfiles_SOURCES := makextfiles.c

build/compress_mario_anims_SOURCES := compress_mario_anims.c

build/psx_sample_gen: $(LIBAUDIOFILE)
build/psx_sample_gen_SOURCES := psx_sample_gen.c
build/psx_sample_gen_CFLAGS  := -Iaudiofile
build/psx_sample_gen_LDFLAGS := -Laudiofile -laudiofile

build/convert_image_psx_SOURCES := convert_image_psx.c

build/n64graphics_SOURCES := n64graphics.c utils.c
build/n64graphics_CFLAGS  := -DN64GRAPHICS_STANDALONE
build/n64graphics_ci_SOURCES := n64graphics_ci_dir/n64graphics_ci.c n64graphics_ci_dir/exoquant/exoquant.c n64graphics_ci_dir/utils.c

build/mio0_SOURCES := libmio0.c
build/mio0_CFLAGS  := -DMIO0_STANDALONE

#build/n64cksum_SOURCES := n64cksum.c utils.c
#build/n64cksum_CFLAGS  := -DN64CKSUM_STANDALONE
#build/patch_elf_32bit_SOURCES := patch_elf_32bit.c
#build/vadpcm_enc_SOURCES := sdk-tools/adpcm/vadpcm_enc.c sdk-tools/adpcm/vpredictor.c sdk-tools/adpcm/quant.c sdk-tools/adpcm/util.c sdk-tools/adpcm/vencode.c
#build/vadpcm_enc_CFLAGS  := -Wno-unused-result -Wno-uninitialized -Wno-sign-compare -Wno-absolute-value

build/textconv_SOURCES := textconv.c utf8.c hashtable.c

build/aifc_decode_SOURCES := aifc_decode.c

build/aiff_extract_codebook_SOURCES := aiff_extract_codebook.c

build/tabledesign: $(LIBAUDIOFILE)
build/tabledesign_SOURCES := sdk-tools/tabledesign/codebook.c sdk-tools/tabledesign/estimate.c sdk-tools/tabledesign/print.c sdk-tools/tabledesign/tabledesign.c
build/tabledesign_CFLAGS  := -Iaudiofile -Wno-uninitialized
build/tabledesign_LDFLAGS := -Laudiofile -laudiofile -lstdc++



build/extract_data_for_mio_SOURCES := extract_data_for_mio.c

build/skyconv_SOURCES := skyconv.c n64graphics.c utils.c

build/psxavenc:
	@mkdir -p build
	$(V)$(RM) -rf psxavenc-git
	$(V)git clone https://codeberg.org/WonderfulToolchain/psxavenc psxavenc-git
	$(V)if [ ! -f psxavenc-git/psxavenc/mdec.c.bak ]; then mv psxavenc-git/psxavenc/mdec.c psxavenc-git/psxavenc/mdec.c.bak; fi
	$(V)echo "#include <libavutil/mem.h>" | cat - psxavenc-git/psxavenc/mdec.c.bak > psxavenc-git/psxavenc/mdec.c
	$(V)cd psxavenc-git && meson setup build && meson compile -C build
	$(V)mv psxavenc-git/build/psxavenc $@
	$(V)$(RM) -rf psxavenc-git

build/mkpsxiso:
	@mkdir -p build
	$(V)wget -q https://github.com/Lameguy64/mkpsxiso/releases/download/v2.10/mkpsxiso-2.10-Linux.zip
	$(V)unzip mkpsxiso-2.10-Linux.zip
	$(V)mv mkpsxiso-2.10-Linux/bin/mkpsxiso $@
	$(V)$(RM) -rf mkpsxiso-2.10-Linux mkpsxiso-2.10-Linux.zip

all: $(LIBAUDIOFILE) $(BUILD_PROGRAMS) build/psxavenc build/mkpsxiso

clean:
	$(RM) -rf build $(LIBAUDIOFILE) $(BUILD_PROGRAMS:build/%=%) psxavenc mkpsxiso
	$(MAKE) -C audiofile clean

define COMPILE
$(1): $($1_SOURCES)
	@mkdir -p build
	$$(CC) $(CFLAGS) $($1_CFLAGS) $$^ -o $$@ $($1_LDFLAGS) $(LDFLAGS)
endef

$(foreach p,$(BUILD_PROGRAMS),$(eval $(call COMPILE,$(p))))

$(LIBAUDIOFILE):
	@$(MAKE) -C audiofile

.PHONY: all clean default
