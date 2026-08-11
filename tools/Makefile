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

CFLAGS       := -I. -Iinclude -Wall -Wextra -Wno-unused-parameter -pedantic -O3 -march=native -mtune=native -s -g
LDFLAGS      := -lm -lstdc++
ALL_PROGRAMS := textconv aifc_decode aiff_extract_codebook tabledesign extract_data_for_mio skyconv makextfiles compress_mario_anims psx_sample_gen convert_image_psx n64graphics mio0
LIBAUDIOFILE := audiofile/libaudiofile.a

BUILD_PROGRAMS := $(ALL_PROGRAMS)

default: all

makextfiles_SOURCES := makextfiles.c

compress_mario_anims_SOURCES := compress_mario_anims.c

psx_sample_gen: $(LIBAUDIOFILE)
psx_sample_gen_SOURCES := psx_sample_gen.c
psx_sample_gen_CFLAGS  := -Iaudiofile
psx_sample_gen_LDFLAGS := -Laudiofile -laudiofile

convert_image_psx_SOURCES := convert_image_psx.c

n64graphics_SOURCES := n64graphics.c utils.c
n64graphics_CFLAGS  := -DN64GRAPHICS_STANDALONE
n64graphics_ci_SOURCES := n64graphics_ci_dir/n64graphics_ci.c n64graphics_ci_dir/exoquant/exoquant.c n64graphics_ci_dir/utils.c

mio0_SOURCES := libmio0.c
mio0_CFLAGS  := -DMIO0_STANDALONE

#n64cksum_SOURCES := n64cksum.c utils.c
#n64cksum_CFLAGS  := -DN64CKSUM_STANDALONE

textconv_SOURCES := textconv.c utf8.c hashtable.c

#patch_elf_32bit_SOURCES := patch_elf_32bit.c

aifc_decode_SOURCES := aifc_decode.c

aiff_extract_codebook_SOURCES := aiff_extract_codebook.c

tabledesign: $(LIBAUDIOFILE)
tabledesign_SOURCES := sdk-tools/tabledesign/codebook.c sdk-tools/tabledesign/estimate.c sdk-tools/tabledesign/print.c sdk-tools/tabledesign/tabledesign.c
tabledesign_CFLAGS  := -Iaudiofile -Wno-uninitialized
tabledesign_LDFLAGS := -Laudiofile -laudiofile -lstdc++

#vadpcm_enc_SOURCES := sdk-tools/adpcm/vadpcm_enc.c sdk-tools/adpcm/vpredictor.c sdk-tools/adpcm/quant.c sdk-tools/adpcm/util.c sdk-tools/adpcm/vencode.c
#vadpcm_enc_CFLAGS  := -Wno-unused-result -Wno-uninitialized -Wno-sign-compare -Wno-absolute-value

extract_data_for_mio_SOURCES := extract_data_for_mio.c

skyconv_SOURCES := skyconv.c n64graphics.c utils.c

# the psxavenc CI seems to just not work currently
#psxavenc:
#	$(V)wget https://github.com/WonderfulToolchain/psxavenc/releases/download/v0.3.1/psxavenc-linux.zip
#	$(V)unzip psxavenc-linux.zip
#	$(V)mv bin/psxavenc ./psxavenc
#	$(V)rm -rf bin psxavenc-linux.zip
psxavenc:
	$(V)rm -rf psxavenc-git
	$(V)git clone https://github.com/WonderfulToolchain/psxavenc psxavenc-git
	$(V)cd psxavenc-git && meson setup build && meson compile -C build
	$(V)mv psxavenc-git/build/psxavenc ./psxavenc
	$(V)rm -rf psxavenc-git

mkpsxiso:
	$(V)wget -q https://github.com/Lameguy64/mkpsxiso/releases/download/v2.10/mkpsxiso-2.10-Linux.zip
	$(V)unzip mkpsxiso-2.10-Linux.zip
	$(V)mv mkpsxiso-2.10-Linux/bin/mkpsxiso ./mkpsxiso
	$(V)rm -rf mkpsxiso-2.10-Linux mkpsxiso-2.10-Linux.zip

all-except-recomp: $(LIBAUDIOFILE) $(BUILD_PROGRAMS) psxavenc mkpsxiso

all: all-except-recomp ido5.3_recomp

clean:
	$(RM) $(ALL_PROGRAMS) psxavenc mkpsxiso
	$(MAKE) -C audiofile clean
	$(MAKE) -C ido5.3_recomp clean

define COMPILE
$(1): $($1_SOURCES)
	$$(CC) $(CFLAGS) $($1_CFLAGS) $$^ -o $$@ $($1_LDFLAGS) $(LDFLAGS)
endef

ido5.3_recomp:
	$(MAKE) -C ido5.3_recomp

$(foreach p,$(BUILD_PROGRAMS),$(eval $(call COMPILE,$(p))))

$(LIBAUDIOFILE):
	@$(MAKE) -C audiofile

.PHONY: all all-except-recomp clean default ido5.3_recomp
