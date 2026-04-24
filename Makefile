#---------------------------------------------------------------------------------
# ioquake3-PS3 Makefile
# Requires ps3toolchain (ps3dev/PSL1GHT) to be built and installed.
#
# Environment variables required:
#   PS3DEV  = /usr/local/ps3dev   (or wherever ps3toolchain installed)
#   PSL1GHT = $PS3DEV             (PSL1GHT SDK root)
#
# Usage:
#   make              - Build ELF
#   make pkg          - Build PKG for installation
#   make clean        - Remove build artifacts
#   make DEBUG=1      - Enable debug logging
#---------------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
  $(error "Set PS3DEV in your environment. export PS3DEV=/usr/local/ps3dev")
endif
ifeq ($(strip $(PSL1GHT)),)
  PSL1GHT := $(PS3DEV)
endif

#---------------------------------------------------------------------------------
# Toolchain
#---------------------------------------------------------------------------------
PREFIX  := $(PS3DEV)/ppu/bin/ppu-
CC      := $(PREFIX)gcc
CXX     := $(PREFIX)g++
LD      := $(PREFIX)gcc
AR      := $(PREFIX)ar
OBJCOPY := $(PREFIX)objcopy
STRIP   := $(PREFIX)strip
SPRXLINK := $(PS3DEV)/bin/sprxlinker
FSELF    := $(PS3DEV)/bin/fself
MAKE_SELF_NPDRM := $(PS3DEV)/bin/make_self_npdrm

#---------------------------------------------------------------------------------
# Project identity
#---------------------------------------------------------------------------------
TARGET    := ioquake3_ps3
TITLE_ID  := IOQ3PS300
CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
BUILD     := build
PORTDIR   := $(CURDIR)
# Use project-local ICON0.PNG if present, else PSL1GHT default
ifneq ($(wildcard $(PORTDIR)/ICON0.PNG),)
ICON0     ?= $(PORTDIR)/ICON0.PNG
else
ICON0     ?= $(PS3DEV)/bin/ICON0.PNG
endif

#---------------------------------------------------------------------------------
# ioQuake3 source directory (sibling to this port)
#---------------------------------------------------------------------------------
IOQ3_DIR := ../ioq3

#---------------------------------------------------------------------------------
# PSL1GHT SDK paths
#---------------------------------------------------------------------------------
PSL1GHT_INC  := $(PSL1GHT)/ppu/include
PSL1GHT_LIB  := $(PSL1GHT)/ppu/lib
PORTLIBS_INC  := $(PS3DEV)/portlibs/ppu/include
PORTLIBS_LIB  := $(PS3DEV)/portlibs/ppu/lib

#---------------------------------------------------------------------------------
# Source files from ioQ3
#---------------------------------------------------------------------------------
IOQ3_SRCS := \
  $(IOQ3_DIR)/code/qcommon/cmd.c \
  $(IOQ3_DIR)/code/qcommon/cm_load.c \
  $(IOQ3_DIR)/code/qcommon/cm_patch.c \
  $(IOQ3_DIR)/code/qcommon/cm_polylib.c \
  $(IOQ3_DIR)/code/qcommon/cm_test.c \
  $(IOQ3_DIR)/code/qcommon/cm_trace.c \
  $(IOQ3_DIR)/code/qcommon/common.c \
  $(IOQ3_DIR)/code/qcommon/cvar.c \
  $(IOQ3_DIR)/code/qcommon/files.c \
  $(IOQ3_DIR)/code/qcommon/huffman.c \
  $(IOQ3_DIR)/code/qcommon/md4.c \
  $(IOQ3_DIR)/code/qcommon/msg.c \
  $(IOQ3_DIR)/code/qcommon/net_chan.c \
  $(IOQ3_DIR)/code/qcommon/net_ip.c \
  $(IOQ3_DIR)/code/qcommon/q_math.c \
  $(IOQ3_DIR)/code/qcommon/q_shared.c \
  $(IOQ3_DIR)/code/qcommon/unzip.c \
  $(IOQ3_DIR)/code/qcommon/vm.c \
  $(IOQ3_DIR)/code/qcommon/vm_interpreted.c \
  $(IOQ3_DIR)/code/qcommon/vm_none.c \
  $(IOQ3_DIR)/code/client/cl_cgame.c \
  $(IOQ3_DIR)/code/client/cl_cin.c \
  $(IOQ3_DIR)/code/client/cl_console.c \
  $(IOQ3_DIR)/code/client/cl_input.c \
  $(IOQ3_DIR)/code/client/cl_keys.c \
  $(IOQ3_DIR)/code/client/cl_main.c \
  $(IOQ3_DIR)/code/client/cl_net_chan.c \
  $(IOQ3_DIR)/code/client/cl_parse.c \
  $(IOQ3_DIR)/code/client/cl_scrn.c \
  $(IOQ3_DIR)/code/client/cl_ui.c \
  $(IOQ3_DIR)/code/client/snd_dma.c \
  $(IOQ3_DIR)/code/client/snd_mem.c \
  $(IOQ3_DIR)/code/client/snd_mix.c \
  $(IOQ3_DIR)/code/client/snd_codec.c \
  $(IOQ3_DIR)/code/client/snd_codec_wav.c \
  $(IOQ3_DIR)/code/client/snd_adpcm.c \
  $(IOQ3_DIR)/code/client/snd_wavelet.c \
  $(IOQ3_DIR)/code/server/sv_bot.c \
  $(IOQ3_DIR)/code/server/sv_ccmds.c \
  $(IOQ3_DIR)/code/server/sv_client.c \
  $(IOQ3_DIR)/code/server/sv_game.c \
  $(IOQ3_DIR)/code/server/sv_init.c \
  $(IOQ3_DIR)/code/server/sv_main.c \
  $(IOQ3_DIR)/code/server/sv_net_chan.c \
  $(IOQ3_DIR)/code/server/sv_snapshot.c \
  $(IOQ3_DIR)/code/server/sv_world.c \
  $(IOQ3_DIR)/code/botlib/be_aas_bspq3.c \
  $(IOQ3_DIR)/code/botlib/be_aas_cluster.c \
  $(IOQ3_DIR)/code/botlib/be_aas_debug.c \
  $(IOQ3_DIR)/code/botlib/be_aas_entity.c \
  $(IOQ3_DIR)/code/botlib/be_aas_file.c \
  $(IOQ3_DIR)/code/botlib/be_aas_main.c \
  $(IOQ3_DIR)/code/botlib/be_aas_move.c \
  $(IOQ3_DIR)/code/botlib/be_aas_optimize.c \
  $(IOQ3_DIR)/code/botlib/be_aas_reach.c \
  $(IOQ3_DIR)/code/botlib/be_aas_route.c \
  $(IOQ3_DIR)/code/botlib/be_aas_routealt.c \
  $(IOQ3_DIR)/code/botlib/be_aas_sample.c \
  $(IOQ3_DIR)/code/botlib/be_ai_char.c \
  $(IOQ3_DIR)/code/botlib/be_ai_chat.c \
  $(IOQ3_DIR)/code/botlib/be_ai_gen.c \
  $(IOQ3_DIR)/code/botlib/be_ai_goal.c \
  $(IOQ3_DIR)/code/botlib/be_ai_move.c \
  $(IOQ3_DIR)/code/botlib/be_ai_weap.c \
  $(IOQ3_DIR)/code/botlib/be_ai_weight.c \
  $(IOQ3_DIR)/code/botlib/be_ea.c \
  $(IOQ3_DIR)/code/botlib/be_interface.c \
  $(IOQ3_DIR)/code/botlib/l_crc.c \
  $(IOQ3_DIR)/code/botlib/l_libvar.c \
  $(IOQ3_DIR)/code/botlib/l_log.c \
  $(IOQ3_DIR)/code/botlib/l_memory.c \
  $(IOQ3_DIR)/code/botlib/l_precomp.c \
  $(IOQ3_DIR)/code/botlib/l_script.c \
  $(IOQ3_DIR)/code/botlib/l_struct.c \
  $(IOQ3_DIR)/code/renderergl1/tr_animation.c \
  $(IOQ3_DIR)/code/renderergl1/tr_bsp.c \
  $(IOQ3_DIR)/code/renderergl1/tr_curve.c \
  $(IOQ3_DIR)/code/renderergl1/tr_init.c \
  $(IOQ3_DIR)/code/renderergl1/tr_light.c \
  $(IOQ3_DIR)/code/renderergl1/tr_main.c \
  $(IOQ3_DIR)/code/renderergl1/tr_marks.c \
  $(IOQ3_DIR)/code/renderergl1/tr_mesh.c \
  $(IOQ3_DIR)/code/renderergl1/tr_model.c \
  $(IOQ3_DIR)/code/renderergl1/tr_model_iqm.c \
  $(IOQ3_DIR)/code/renderergl1/tr_scene.c \
  $(IOQ3_DIR)/code/renderergl1/tr_shade_calc.c \
  $(IOQ3_DIR)/code/renderergl1/tr_shader.c \
  $(IOQ3_DIR)/code/renderergl1/tr_backend.c \
  $(IOQ3_DIR)/code/renderergl1/tr_cmds.c \
  $(IOQ3_DIR)/code/renderergl1/tr_flares.c \
  $(IOQ3_DIR)/code/renderergl1/tr_image.c \
  $(IOQ3_DIR)/code/renderergl1/tr_shade.c \
  $(IOQ3_DIR)/code/renderergl1/tr_shadows.c \
  $(IOQ3_DIR)/code/renderergl1/tr_sky.c \
  $(IOQ3_DIR)/code/renderergl1/tr_surface.c \
  $(IOQ3_DIR)/code/renderergl1/tr_world.c \
  $(IOQ3_DIR)/code/renderercommon/puff.c \
  $(IOQ3_DIR)/code/renderercommon/tr_font.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_bmp.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_jpg.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_pcx.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_png.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_pvr.c \
  $(IOQ3_DIR)/code/renderercommon/tr_image_tga.c \
  $(IOQ3_DIR)/code/renderercommon/tr_noise.c

#---------------------------------------------------------------------------------
# PS3 port source files
#---------------------------------------------------------------------------------
PS3_SRCS := \
  code/sys/ps3_main.c \
  code/sys/ps3_sys.c \
  code/sys/ps3_glimp.c \
  code/input/ps3_input.c \
  code/input/ps3_osk.c \
  code/audio/ps3_snd.c \
  code/renderer/ps3_renderer.c \
  code/renderer/qgl_ps3.c \
  code/renderer/ps3_gl_stubs.c \
  code/gl/ps3gl_main.c \
  code/gl/ps3gl_states.c \
  code/gl/ps3gl_matrices.c \
  code/gl/ps3gl_vertices.c \
  code/gl/ps3gl_colors.c \
  code/gl/ps3gl_textures.c \
  code/gl/ps3gl_draw.c \
  code/gl/ps3gl_shaders.c

#---------------------------------------------------------------------------------
# PPC64-correct setjmp/longjmp replacement (overrides broken newlib version)
# Must be linked before libc.a for symbol override to work.
#---------------------------------------------------------------------------------
PS3_ASM_SRCS := code/sys/ps3_setjmp.S

#---------------------------------------------------------------------------------
# zlib detection (same logic as Wii port)
#---------------------------------------------------------------------------------
IOQ3_ZLIB_A := $(IOQ3_DIR)/code/libs/zlib/zlib.h
IOQ3_ZLIB_B := $(IOQ3_DIR)/code/zlib/zlib.h
IOQ3_ZLIB_C := $(IOQ3_DIR)/code/thirdparty/zlib-1.3.1/zlib.h

ifneq ($(wildcard $(IOQ3_ZLIB_A)),)
  ZLIB_DIR      := $(IOQ3_DIR)/code/libs/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(IOQ3_ZLIB_B)),)
  ZLIB_DIR      := $(IOQ3_DIR)/code/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(IOQ3_ZLIB_C)),)
  ZLIB_DIR      := $(IOQ3_DIR)/code/thirdparty/zlib-1.3.1
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(PORTLIBS_INC)/zlib.h),)
  ZLIB_DIR      := $(PORTLIBS_INC)
  ZLIB_CFLAGS   := -I$(PORTLIBS_INC)
  IOQ3_ZLIB_SRCS :=
  ZLIB_LIBS     := -L$(PORTLIBS_LIB) -lz
  $(info >>> Using ps3toolchain portlibs zlib)
else
  $(error "zlib.h not found. Build ps3libraries or provide zlib.")
endif

# Copy zlib.h next to unzip.h for quoted includes
ZLIB_H_COPY  := $(IOQ3_DIR)/code/qcommon/zlib.h
ZCONF_H_COPY := $(IOQ3_DIR)/code/qcommon/zconf.h

#---------------------------------------------------------------------------------
# Internal libjpeg (bundled jpeg-9f -- avoids PSL1GHT portlibs libjpeg 8.0 bugs)
# On PPC64 (LP64 ABI), libjpeg's "typedef long INT32" gives 64-bit INT32 which
# corrupts JPEG decoding. We define XMD_H to suppress jmorecfg.h's INT32/INT16
# typedefs and provide correct ones via ps3_platform.h.
#---------------------------------------------------------------------------------
JPEG_DIR      := $(IOQ3_DIR)/code/thirdparty/jpeg-9f
JPEG_CFLAGS   := -DUSE_INTERNAL_JPEG -DXMD_H -I$(JPEG_DIR)
IOQ3_JPEG_SRCS := $(wildcard $(JPEG_DIR)/j*.c)

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ifeq ($(DEBUG),1)
  DEBUG_FLAG := -DPS3_DEBUG -g
else
  DEBUG_FLAG :=
endif

CFLAGS := \
  -O2 -Wall -Wno-unused-variable -Wno-missing-braces -Wno-cpp \
  -mno-altivec \
  $(DEBUG_FLAG) \
  -D__PS3__ -D__lv2ppu__ \
  -DMAX_CLIENTS=8 \
  -DMAX_RAW_SAMPLES=8192 \
  -DBOTLIB -DUSE_CODEC_VORBIS=0 -DUSE_CODEC_OPUS=0 -DUSE_OPENAL=0 \
  -DUSE_LOCAL_HEADERS \
  -DMIN_DEDICATED_COMHUNKMEGS=16 -DMIN_COMHUNKMEGS=16 \
  $(ZLIB_CFLAGS) \
  $(JPEG_CFLAGS) \
  -include $(PORTDIR)/code/sys/ps3_platform.h \
  -I$(PORTDIR)/code/sys/include \
  -I$(PORTDIR)/code \
  -I$(PORTDIR)/code/gl \
  -I$(IOQ3_DIR)/code \
  -I$(IOQ3_DIR)/code/sys \
  -I$(IOQ3_DIR)/code/qcommon \
  -I$(IOQ3_DIR)/code/client \
  -I$(IOQ3_DIR)/code/renderercommon \
  -I$(IOQ3_DIR)/code/renderergl1 \
  -I$(IOQ3_DIR)/code/botlib \
  -I$(PSL1GHT_INC) \
  -I$(PORTLIBS_INC)

CXXFLAGS := $(CFLAGS)

LDFLAGS := \
  -L$(PSL1GHT_LIB) \
  -L$(PORTLIBS_LIB) \
  -Wl,--wrap,CL_GenerateQKey \
  -Wl,--wrap,Com_Printf

LIBS := \
  -lrsx -lgcm_sys -lio -laudio -lsysutil \
  -lrt -llv2 -lnet -lnetctl -lsysmodule \
  $(ZLIB_LIBS) -lpng -lz -lm

#---------------------------------------------------------------------------------
# Source collection
#---------------------------------------------------------------------------------
ALL_SRCS := $(PS3_SRCS) $(IOQ3_SRCS) $(IOQ3_ZLIB_SRCS) $(IOQ3_JPEG_SRCS)

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(ALL_SRCS))
ASM_OBJS := $(patsubst %.S,$(BUILD)/%.o,$(PS3_ASM_SRCS))

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
.PHONY: all clean pkg self install prebuild

all: $(BUILD)/$(TARGET).elf

# Copy zlib headers next to unzip.h
prebuild:
	@cp $(ZLIB_DIR)/zlib.h $(ZLIB_H_COPY) 2>/dev/null || true
	@test -f $(ZLIB_DIR)/zconf.h && cp $(ZLIB_DIR)/zconf.h $(ZCONF_H_COPY) || true

$(BUILD)/$(TARGET).elf: prebuild $(ASM_OBJS) $(OBJS)
	@echo "Linking $@"
	$(LD) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) $(LIBS) -o $@

# Rule for assembly files (.S)
# NOTE: Must NOT use $(CFLAGS) here -- it contains -include ps3_platform.h
# which pulls C code (typedefs, inline functions) into the assembler.
# Only pass flags the assembler actually needs.
ASFLAGS := -mno-altivec

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# Default rule for C files
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Port-specific files need network shim
$(BUILD)/code/sys/ps3_main.o: code/sys/ps3_main.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -DPS3_INCLUDE_NET -c $< -o $@

# net_ip.c needs network shim
$(BUILD)/$(IOQ3_DIR)/code/qcommon/net_ip.o: $(IOQ3_DIR)/code/qcommon/net_ip.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -DPS3_INCLUDE_NET -c $< -o $@

# common.c with overridden memory constants
$(BUILD)/$(IOQ3_DIR)/code/qcommon/common.o: $(IOQ3_DIR)/code/qcommon/common.c
	@mkdir -p $(dir $@)
	@echo "CC $< [ps3-patched]"
	@$(CC) $(CFLAGS) \
	       -UMIN_COMHUNKMEGS -DMIN_COMHUNKMEGS=16 \
	       -UMIN_DEDICATED_COMHUNKMEGS -DMIN_DEDICATED_COMHUNKMEGS=16 \
	       -c $< -o $@

# Run sprxlinker to patch import stubs, then create fake SELF
self: $(BUILD)/$(TARGET).elf
	@echo "Running sprxlinker..."
	$(SPRXLINK) $(BUILD)/$(TARGET).elf
	@echo "Creating SELF..."
	$(FSELF) $(BUILD)/$(TARGET).elf $(BUILD)/EBOOT.BIN
	@echo "Done: $(BUILD)/EBOOT.BIN"

# Create installable directory for FTP transfer to PS3 (CFW)
# Copy build/install/ to /dev_hdd0/game/IOQ3PS300/ via FTP
install: self
	@echo "Creating install directory..."
	@mkdir -p $(BUILD)/install/USRDIR
	@cp $(BUILD)/EBOOT.BIN $(BUILD)/install/USRDIR/EBOOT.BIN
	@cp $(ICON0) $(BUILD)/install/ICON0.PNG
	python3 $(PORTDIR)/make_sfo.py $(BUILD)/install/PARAM.SFO --title "ioQuake3" --appid "$(TITLE_ID)"
	@echo ""
	@echo "Done. FTP the contents of build/install/ to:"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/"
	@echo ""
	@echo "Directory structure on PS3:"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/PARAM.SFO"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/ICON0.PNG"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/USRDIR/EBOOT.BIN"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/USRDIR/baseq3/pak0.pk3  (add game data)"

# Create PKG for PS3 installation (NPDRM SELF)
pkg: $(BUILD)/$(TARGET).elf
	@echo "Running sprxlinker..."
	$(SPRXLINK) $(BUILD)/$(TARGET).elf
	@echo "Creating NPDRM SELF..."
	@mkdir -p $(BUILD)/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $(BUILD)/$(TARGET).elf $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	@echo "Creating SFO..."
	python3 $(PORTDIR)/make_sfo.py $(BUILD)/pkg/PARAM.SFO --title "ioQuake3" --appid "$(TITLE_ID)"
	@echo "Copying ICON0.PNG..."
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	@echo "Creating PKG..."
	python3 $(PS3DEV)/bin/pkg.py --contentid $(CONTENT_ID) $(BUILD)/pkg/ $(BUILD)/$(TARGET).pkg
	@echo "Done: $(BUILD)/$(TARGET).pkg"

clean:
	@rm -rf $(BUILD)
	@rm -f $(ZLIB_H_COPY) $(ZCONF_H_COPY)
	@# Also remove upstream .o files (OBJS resolves build/../ioq3/ to ./ioq3/)
	@rm -rf ioq3/
	@echo "Cleaned."
