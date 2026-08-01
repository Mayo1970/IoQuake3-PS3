#---------------------------------------------------------------------------------
# Flavor selection (q3 is default; `make ta` / `make oa` / `make classic` / `make ef`
# set TA/OA/CLASSIC/EF=1)
#---------------------------------------------------------------------------------

ifeq ($(MAKECMDGOALS),ta)
  TA := 1
endif
ifeq ($(MAKECMDGOALS),oa)
  OA := 1
endif
ifeq ($(MAKECMDGOALS),classic)
  CLASSIC := 1
endif
ifeq ($(MAKECMDGOALS),ef)
  EF := 1
endif

ifeq ($(TA),1)
  FLAVOR        := ta
  TITLE         := Team Arena
  TITLE_ID      := IOTAPS300
  TARGET        := ioquake3_ta_ps3
  BUILD         := build_ta
  DEFINES_EXTRA := -DSTANDALONETA
  ICON0_SUBDIR  := ta
else ifeq ($(OA),1)
  FLAVOR        := oa
  TITLE         := Open Arena
  TITLE_ID      := IOOAPS300
  TARGET        := ioquake3_oa_ps3
  BUILD         := build_oa
  DEFINES_EXTRA := -DSTANDALONEOA
  ICON0_SUBDIR  := oa
else ifeq ($(CLASSIC),1)
  FLAVOR        := classic
  TITLE         := Quake 3 Classic
  TITLE_ID      := IOQCPS301
  TARGET        := ioquake3_classic_ps3
  BUILD         := build_qc
  DEFINES_EXTRA := -DCLASSIC -DLEGACY_PROTOCOL
  ICON0_SUBDIR  := qc
else ifeq ($(EF),1)
  FLAVOR        := ef
  TITLE         := Elite Force
  TITLE_ID      := IOEFPS300
  TARGET        := ioquake3_ef_ps3
  BUILD         := build_ef
  DEFINES_EXTRA := -DELITEFORCE -DLEGACY_PROTOCOL
  ICON0_SUBDIR  := ef
else
  FLAVOR        := q3
  TITLE         := ioQuake3
  TITLE_ID      := IOQ3PS300
  TARGET        := ioquake3_ps3
  BUILD         := build
  DEFINES_EXTRA :=
  ICON0_SUBDIR  := q3
endif

.DEFAULT_GOAL := all

.PHONY: ta oa classic ef all-flavors

ta: all
oa: all
classic: all
ef: all

all-flavors:
	$(MAKE) pkg
	$(MAKE) TA=1 pkg
	$(MAKE) OA=1 pkg
	$(MAKE) CLASSIC=1 pkg
	$(MAKE) EF=1 pkg
