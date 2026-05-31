#---------------------------------------------------------------------------------
# ioquake3-PS3 Unified Makefile
# Requires ps3toolchain (ps3dev/PSL1GHT) to be built and installed.
#
# Environment variables required:
#   PS3DEV  = /usr/local/ps3dev   (or wherever ps3toolchain installed)
#   PSL1GHT = $PS3DEV             (PSL1GHT SDK root; defaults to PS3DEV)
#
# Usage:
#   make              - Build Q3A ELF
#   make pkg          - Build Q3A PKG
#   make ta           - Build Team Arena ELF
#   make TA=1 pkg     - Build Team Arena PKG
#   make oa           - Build Open Arena ELF
#   make OA=1 pkg     - Build Open Arena PKG
#   make all-flavors  - Build all three PKGs in sequence
#   make clean        - Remove build artifacts for all three flavors
#   make DEBUG=1      - Enable debug logging (any flavor)
#
# The three flavors use separate build directories so they never stomp each other:
#   Q3A -> build/    OA -> build_oa/    TA -> build_ta/
#---------------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
  $(error "Set PS3DEV in your environment. export PS3DEV=/usr/local/ps3dev")
endif
ifeq ($(strip $(PSL1GHT)),)
  PSL1GHT := $(PS3DEV)
endif

#---------------------------------------------------------------------------------
# Flavor selection (q3 is default; `make ta` / `make oa` set TA/OA=1)
#---------------------------------------------------------------------------------

ifeq ($(MAKECMDGOALS),ta)
  TA := 1
endif
ifeq ($(MAKECMDGOALS),oa)
  OA := 1
endif

ifeq ($(TA),1)
  FLAVOR        := ta
  TITLE         := Team Arena
  TITLE_ID      := IOQ3TA00
  TARGET        := ioquake3_ta_ps3
  BUILD         := build_ta
  DEFINES_EXTRA := -DSTANDALONETA
  ICON0_SUBDIR  := ta
else ifeq ($(OA),1)
  FLAVOR        := oa
  TITLE         := Open Arena
  TITLE_ID      := IOQ3OA00
  TARGET        := ioquake3_oa_ps3
  BUILD         := build_oa
  DEFINES_EXTRA := -DSTANDALONEOA
  ICON0_SUBDIR  := oa
else
  FLAVOR        := q3
  TITLE         := ioQuake3
  TITLE_ID      := IOQ3PS300
  TARGET        := ioquake3_ps3
  BUILD         := build
  DEFINES_EXTRA :=
  ICON0_SUBDIR  := q3
endif

CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
PORTDIR    := $(CURDIR)

ifneq ($(wildcard $(PORTDIR)/icons/$(ICON0_SUBDIR)/ICON0.PNG),)
  ICON0 ?= $(PORTDIR)/icons/$(ICON0_SUBDIR)/ICON0.PNG
else ifneq ($(wildcard $(PORTDIR)/ICON0.PNG),)
  ICON0 ?= $(PORTDIR)/ICON0.PNG
else
  ICON0 ?= $(PS3DEV)/bin/ICON0.PNG
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
# PSL1GHT SDK paths
#---------------------------------------------------------------------------------
PSL1GHT_INC  := $(PSL1GHT)/ppu/include
PSL1GHT_LIB  := $(PSL1GHT)/ppu/lib
PORTLIBS_INC  := $(PS3DEV)/portlibs/ppu/include
PORTLIBS_LIB  := $(PS3DEV)/portlibs/ppu/lib

#---------------------------------------------------------------------------------
# ioq3 sources (same for all three flavors)
#---------------------------------------------------------------------------------
IOQ3_SRCS := \
  code/qcommon/cmd.c \
  code/qcommon/cm_load.c \
  code/qcommon/cm_patch.c \
  code/qcommon/cm_polylib.c \
  code/qcommon/cm_test.c \
  code/qcommon/cm_trace.c \
  code/qcommon/common.c \
  code/qcommon/cvar.c \
  code/qcommon/files.c \
  code/qcommon/huffman.c \
  code/qcommon/md4.c \
  code/qcommon/md5.c \
  code/qcommon/msg.c \
  code/qcommon/net_chan.c \
  code/qcommon/net_ip.c \
  code/qcommon/q_math.c \
  code/qcommon/q_shared.c \
  code/qcommon/unzip.c \
  code/qcommon/vm.c \
  code/qcommon/vm_interpreted.c \
  code/qcommon/vm_none.c \
  code/client/cl_cgame.c \
  code/client/cl_cin.c \
  code/client/cl_console.c \
  code/client/cl_input.c \
  code/client/cl_keys.c \
  code/client/cl_main.c \
  code/client/cl_net_chan.c \
  code/client/cl_parse.c \
  code/client/cl_scrn.c \
  code/client/cl_ui.c \
  code/client/snd_dma.c \
  code/client/snd_mem.c \
  code/client/snd_mix.c \
  code/client/snd_codec.c \
  code/client/snd_codec_wav.c \
  code/client/snd_codec_ogg.c \
  code/client/snd_adpcm.c \
  code/client/snd_wavelet.c \
  code/server/sv_bot.c \
  code/server/sv_ccmds.c \
  code/server/sv_client.c \
  code/server/sv_game.c \
  code/server/sv_init.c \
  code/server/sv_main.c \
  code/server/sv_net_chan.c \
  code/server/sv_snapshot.c \
  code/server/sv_world.c \
  code/botlib/be_aas_bspq3.c \
  code/botlib/be_aas_cluster.c \
  code/botlib/be_aas_debug.c \
  code/botlib/be_aas_entity.c \
  code/botlib/be_aas_file.c \
  code/botlib/be_aas_main.c \
  code/botlib/be_aas_move.c \
  code/botlib/be_aas_optimize.c \
  code/botlib/be_aas_reach.c \
  code/botlib/be_aas_route.c \
  code/botlib/be_aas_routealt.c \
  code/botlib/be_aas_sample.c \
  code/botlib/be_ai_char.c \
  code/botlib/be_ai_chat.c \
  code/botlib/be_ai_gen.c \
  code/botlib/be_ai_goal.c \
  code/botlib/be_ai_move.c \
  code/botlib/be_ai_weap.c \
  code/botlib/be_ai_weight.c \
  code/botlib/be_ea.c \
  code/botlib/be_interface.c \
  code/botlib/l_crc.c \
  code/botlib/l_libvar.c \
  code/botlib/l_log.c \
  code/botlib/l_memory.c \
  code/botlib/l_precomp.c \
  code/botlib/l_script.c \
  code/botlib/l_struct.c \
  code/renderergl1/tr_animation.c \
  code/renderergl1/tr_bsp.c \
  code/renderergl1/tr_curve.c \
  code/renderergl1/tr_init.c \
  code/renderergl1/tr_light.c \
  code/renderergl1/tr_main.c \
  code/renderergl1/tr_marks.c \
  code/renderergl1/tr_mesh.c \
  code/renderergl1/tr_model.c \
  code/renderergl1/tr_model_iqm.c \
  code/renderergl1/tr_scene.c \
  code/renderergl1/tr_shade_calc.c \
  code/renderergl1/tr_shader.c \
  code/renderergl1/tr_backend.c \
  code/renderergl1/tr_cmds.c \
  code/renderergl1/tr_flares.c \
  code/renderergl1/tr_image.c \
  code/renderergl1/tr_shade.c \
  code/renderergl1/tr_shadows.c \
  code/renderergl1/tr_sky.c \
  code/renderergl1/tr_surface.c \
  code/renderergl1/tr_world.c \
  code/renderercommon/puff.c \
  code/renderercommon/tr_font.c \
  code/renderercommon/tr_image_bmp.c \
  code/renderercommon/tr_image_jpg.c \
  code/renderercommon/tr_image_pcx.c \
  code/renderercommon/tr_image_png.c \
  code/renderercommon/tr_image_pvr.c \
  code/renderercommon/tr_image_tga.c \
  code/renderercommon/tr_noise.c

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
# PPC64-correct setjmp/longjmp replacement
# Must be linked before libc.a for symbol override to work.
#---------------------------------------------------------------------------------
PS3_ASM_SRCS := code/sys/ps3_setjmp.S

#---------------------------------------------------------------------------------
# zlib detection
#---------------------------------------------------------------------------------
IOQ3_ZLIB_A := code/libs/zlib/zlib.h
IOQ3_ZLIB_B := code/zlib/zlib.h
IOQ3_ZLIB_C := code/thirdparty/zlib-1.3.1/zlib.h

ifneq ($(wildcard $(IOQ3_ZLIB_A)),)
  ZLIB_DIR      := code/libs/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(IOQ3_ZLIB_B)),)
  ZLIB_DIR      := code/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(IOQ3_ZLIB_C)),)
  ZLIB_DIR      := code/thirdparty/zlib-1.3.1
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

ZLIB_H_COPY  := code/qcommon/zlib.h
ZCONF_H_COPY := code/qcommon/zconf.h

#---------------------------------------------------------------------------------
# Internal libjpeg (jpeg-9f)
#---------------------------------------------------------------------------------
JPEG_DIR      := code/thirdparty/jpeg-9f
JPEG_CFLAGS   := -DUSE_INTERNAL_JPEG -DXMD_H -I$(JPEG_DIR)
IOQ3_JPEG_SRCS := $(wildcard $(JPEG_DIR)/j*.c)

#---------------------------------------------------------------------------------
# Vendored OGG + Vorbis
#---------------------------------------------------------------------------------
OGG_DIR    := code/thirdparty/libogg-1.3.6
VORBIS_DIR := code/thirdparty/libvorbis-1.3.7
OGG_CFLAGS := -I$(OGG_DIR)/include -I$(VORBIS_DIR)/include -I$(VORBIS_DIR)/lib
IOQ3_OGG_SRCS := \
  $(OGG_DIR)/src/bitwise.c \
  $(OGG_DIR)/src/framing.c \
  $(VORBIS_DIR)/lib/mdct.c \
  $(VORBIS_DIR)/lib/block.c \
  $(VORBIS_DIR)/lib/window.c \
  $(VORBIS_DIR)/lib/synthesis.c \
  $(VORBIS_DIR)/lib/info.c \
  $(VORBIS_DIR)/lib/floor0.c \
  $(VORBIS_DIR)/lib/floor1.c \
  $(VORBIS_DIR)/lib/res0.c \
  $(VORBIS_DIR)/lib/mapping0.c \
  $(VORBIS_DIR)/lib/registry.c \
  $(VORBIS_DIR)/lib/codebook.c \
  $(VORBIS_DIR)/lib/sharedbook.c \
  $(VORBIS_DIR)/lib/smallft.c \
  $(VORBIS_DIR)/lib/vorbisfile.c \
  $(VORBIS_DIR)/lib/analysis.c \
  $(VORBIS_DIR)/lib/bitrate.c \
  $(VORBIS_DIR)/lib/envelope.c \
  $(VORBIS_DIR)/lib/lpc.c \
  $(VORBIS_DIR)/lib/lsp.c \
  $(VORBIS_DIR)/lib/psy.c

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
  $(DEFINES_EXTRA) \
  -DMAX_CLIENTS=8 \
  -DMAX_RAW_SAMPLES=8192 \
  -DBOTLIB -DUSE_CODEC_VORBIS=1 -DUSE_CODEC_OPUS=0 -DUSE_OPENAL=0 \
  -DUSE_LOCAL_HEADERS \
  -DMIN_DEDICATED_COMHUNKMEGS=16 -DMIN_COMHUNKMEGS=16 \
  $(ZLIB_CFLAGS) \
  $(JPEG_CFLAGS) \
  $(OGG_CFLAGS) \
  -include $(PORTDIR)/code/sys/ps3_platform.h \
  -I$(PORTDIR)/code/sys/include \
  -I$(PORTDIR)/code \
  -I$(PORTDIR)/code/gl \
  -Icode \
  -Icode/sys \
  -Icode/qcommon \
  -Icode/client \
  -Icode/renderercommon \
  -Icode/renderergl1 \
  -Icode/botlib \
  -I$(PSL1GHT_INC) \
  -I$(PORTLIBS_INC)

# VMX/AltiVec variant: same flags but -mno-altivec replaced by -maltivec.
# Used only for files that contain explicit VMX intrinsics.
CFLAGS_VMX := $(filter-out -mno-altivec,$(CFLAGS)) -maltivec

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
ALL_SRCS := $(PS3_SRCS) $(IOQ3_SRCS) $(IOQ3_ZLIB_SRCS) $(IOQ3_JPEG_SRCS) $(IOQ3_OGG_SRCS)

OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(ALL_SRCS))
ASM_OBJS := $(patsubst %.S,$(BUILD)/%.o,$(PS3_ASM_SRCS))

#---------------------------------------------------------------------------------
# Phony targets
#---------------------------------------------------------------------------------
.PHONY: all ta oa all-flavors clean pkg self install prebuild

all: $(BUILD)/$(TARGET).elf

ta: $(BUILD)/$(TARGET).elf

oa: $(BUILD)/$(TARGET).elf

#---------------------------------------------------------------------------------
# Multi-flavor build
#---------------------------------------------------------------------------------
all-flavors:
	@echo "=== Building ioQuake3 ==="
	$(MAKE) pkg
	@echo "=== Building Team Arena ==="
	$(MAKE) TA=1 pkg
	@echo "=== Building Open Arena ==="
	$(MAKE) OA=1 pkg
	@echo "=== All builds complete ==="
	@ls -1 build/ioquake3_ps3.pkg build_ta/ioquake3_ta_ps3.pkg build_oa/ioquake3_oa_ps3.pkg 2>/dev/null || true

#---------------------------------------------------------------------------------
# Prebuild: copy zlib headers next to unzip.h
#---------------------------------------------------------------------------------
prebuild:
	@cp $(ZLIB_DIR)/zlib.h $(ZLIB_H_COPY) 2>/dev/null || true
	@test -f $(ZLIB_DIR)/zconf.h && cp $(ZLIB_DIR)/zconf.h $(ZCONF_H_COPY) || true

#---------------------------------------------------------------------------------
# Link
#---------------------------------------------------------------------------------
$(BUILD)/$(TARGET).elf: prebuild $(ASM_OBJS) $(OBJS)
	@echo "Linking $@"
	$(LD) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) $(LIBS) -o $@

#---------------------------------------------------------------------------------
# Assembly files: must NOT use $(CFLAGS)
#---------------------------------------------------------------------------------
ASFLAGS := -mno-altivec

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# Default C rule
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Port-specific files need network shim
$(BUILD)/code/sys/ps3_main.o: code/sys/ps3_main.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -DPS3_INCLUDE_NET -c $< -o $@

$(BUILD)/code/qcommon/net_ip.o: code/qcommon/net_ip.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -DPS3_INCLUDE_NET -c $< -o $@

# VMX-enabled files: compiled with -maltivec instead of -mno-altivec
$(BUILD)/code/audio/ps3_snd.o: code/audio/ps3_snd.c
	@mkdir -p $(dir $@)
	@echo "CC $< [vmx]"
	@$(CC) $(CFLAGS_VMX) -c $< -o $@


# common.c with overridden memory constants
$(BUILD)/code/qcommon/common.o: code/qcommon/common.c
	@mkdir -p $(dir $@)
	@echo "CC $< [ps3-patched]"
	@$(CC) $(CFLAGS) \
	       -UMIN_COMHUNKMEGS -DMIN_COMHUNKMEGS=16 \
	       -UMIN_DEDICATED_COMHUNKMEGS -DMIN_DEDICATED_COMHUNKMEGS=16 \
	       -c $< -o $@

#---------------------------------------------------------------------------------
# self / install / pkg
#---------------------------------------------------------------------------------
self: $(BUILD)/$(TARGET).elf
	@echo "Running sprxlinker..."
	$(SPRXLINK) $(BUILD)/$(TARGET).elf
	@echo "Creating SELF..."
	$(FSELF) $(BUILD)/$(TARGET).elf $(BUILD)/EBOOT.BIN
	@echo "Done: $(BUILD)/EBOOT.BIN"

install: self
	@echo "Creating install directory..."
	@mkdir -p $(BUILD)/install/USRDIR
	@cp $(BUILD)/EBOOT.BIN $(BUILD)/install/USRDIR/EBOOT.BIN
	@cp $(ICON0) $(BUILD)/install/ICON0.PNG
	python3 $(PORTDIR)/make_sfo.py $(BUILD)/install/PARAM.SFO --title "$(TITLE)" --appid "$(TITLE_ID)"
	@echo ""
	@echo "Done. FTP the contents of $(BUILD)/install/ to:"
	@echo "  /dev_hdd0/game/$(TITLE_ID)/"

pkg: $(BUILD)/$(TARGET).elf
	@echo "Running sprxlinker..."
	$(SPRXLINK) $(BUILD)/$(TARGET).elf
	@echo "Creating NPDRM SELF..."
	@mkdir -p $(BUILD)/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $(BUILD)/$(TARGET).elf $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	@echo "Creating SFO..."
	python3 $(PORTDIR)/make_sfo.py $(BUILD)/pkg/PARAM.SFO --title "$(TITLE)" --appid "$(TITLE_ID)"
	@echo "Copying ICON0.PNG..."
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	@echo "Creating PKG..."
	python3 $(PS3DEV)/bin/pkg.py --contentid $(CONTENT_ID) $(BUILD)/pkg/ $(BUILD)/$(TARGET).pkg
	@echo "Done: $(BUILD)/$(TARGET).pkg"

#---------------------------------------------------------------------------------
# Clean -- wipes all three flavor build dirs
#---------------------------------------------------------------------------------
clean:
	@rm -rf build/ build_oa/ build_ta/
	@rm -f $(ZLIB_H_COPY) $(ZCONF_H_COPY)
	@echo "Cleaned."
