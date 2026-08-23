# Makefile to rebuild SM64 split image

include util.mk

.RECIPEPREFIX = >
SHELL = /bin/bash

# Default target
default: all

# Preprocessor definitions
DEFINES :=

MAKEFLAGS += --jobs=$(shell nproc) --no-builtin-rules

#==============================================================================#
# Build Options                                                                #
#==============================================================================#

# VERSION - selects the version of the game to build
#   jp - builds the 1996 Japanese version
#   us - builds the 1996 North American version
#   eu - builds the 1997 PAL version
#   sh - builds the 1997 Japanese Shindou version, with rumble pak support
VERSION ?= us
$(eval $(call validate-option,VERSION,jp us eu sh))

ifeq ($(VERSION),jp)
	DEFINES += VERSION_JP=1
	VERSION_JP_US ?= true
else ifeq ($(VERSION),us)
	DEFINES += VERSION_US=1
	VERSION_JP_US ?= true
else ifeq ($(VERSION),eu)
	DEFINES += VERSION_EU=1
	VERSION_JP_US ?= false
else ifeq ($(VERSION),sh)
	DEFINES += VERSION_SH=1
	VERSION_JP_US ?= false
endif

BIG_RAM ?= 0
BENCH ?= 0
ifneq ($(BENCH),0)
	DEFINES += BENCH=1
	BIG_RAM := 1
endif
ifneq ($(BIG_RAM),0)
	DEFINES += BIG_RAM=1
endif
MARIO_HEAD ?= 0
ifneq ($(MARIO_HEAD),0)
	DEFINES += MARIO_HEAD=1
endif

DEFINES += F3D_OLD=1 NON_MATCHING=1 AVOID_UB=1 NO_AUDIO=1

ifeq ($(SATURN),1)
	PLATFORM := saturn
else ifeq ($(PC),1)
	PLATFORM := pc
else
	PLATFORM := psx
endif

# Whether to hide commands or not
VERBOSE ?= 0
ifeq ($(VERBOSE),0)
	V := @
endif

# Whether to colorize build messages
COLOR ?= 1

# display selected options unless 'make clean' or 'make distclean' is run
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
	$(info ==== Build Options ====)
	$(info Platform:           $(PLATFORM))
	$(info Region:             $(VERSION))
	$(info =======================)
endif

BUILD_DIR_BASE := build

#==============================================================================#
# Universal Dependencies                                                       #
#==============================================================================#

TOOLS_DIR := tools

# This is a bit hacky, but a lot of rules implicitly depend
# on tools and assets, and we use directory globs in the makefiles

PYTHON := python3
HOST_UNAME := $(shell uname -s 2>/dev/null || echo Unknown)
ifeq ($(origin HOST_TOOL_EXEEXT), undefined)
	ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_UNAME)),)
		HOST_TOOL_EXEEXT := .exe
	else
		HOST_TOOL_EXEEXT :=
	endif
endif

ifeq ($(filter clean distclean print-%,$(MAKECMDGOALS)),)
	# Make sure assets exist
	NOEXTRACT ?= 0
	ifeq ($(NOEXTRACT),0)
		DUMMY != $(PYTHON) extract_assets.py $(VERSION) >&2 || echo FAIL
		ifeq ($(DUMMY),FAIL)
			$(error Failed to extract assets)
		endif
	endif

	# Make the host tools needed by the selected platform.
	$(info Building tools...)
	ifeq ($(PLATFORM),pc)
		DUMMY != $(MAKE) --no-print-directory -C $(TOOLS_DIR) host-tools HOST_TOOL_EXEEXT=$(HOST_TOOL_EXEEXT) >&2 || echo FAIL
	else
		DUMMY != $(MAKE) --no-print-directory -C $(TOOLS_DIR) all-except-recomp >&2 || echo FAIL
	endif
	ifeq ($(DUMMY),FAIL)
		$(error Failed to build tools)
	endif
	$(info Building...)
endif

ifeq ($(PLATFORM),saturn)
	include Makefile.ss.mk
else ifeq ($(PLATFORM),pc)
	include Makefile.pc.mk
else
	include Makefile.psx.mk
endif

PSXAVENC := tools/psxavenc
MKPSXISO := tools/mkpsxiso
