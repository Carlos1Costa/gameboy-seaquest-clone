#
# Seaquest for Game Boy (DMG) - GBDK-2020
#
#   make        build Seaquest.gb
#   make run    build and run in mgba-qt
#   make hello  build the Hello toolchain smoke test (hello/main.c)
#   make clean  remove build outputs
#

# Change this if GBDK lives somewhere else (ex: GBDK_HOME = /home/user/gbdk/)
GBDK_HOME = ./gbdk/

LCC       = $(GBDK_HOME)bin/lcc
PNG2ASSET = $(GBDK_HOME)bin/png2asset
EMULATOR  = mgba-qt

PROJECTNAME = Seaquest
ROM         = $(PROJECTNAME).gb

BUILDDIR = build
ASSETDIR = $(BUILDDIR)/assets

LCCFLAGS += -Wa-l -Wl-m -Wl-j -I$(ASSETDIR)
# Uncomment to emit debug info (for Emulicious source-level debugging)
# LCCFLAGS += -debug

ROMFLAGS = -Wm-yn"SEAQUEST"

# Sprite PNGs become metasprites (8x8 hardware sprites); tileset PNGs become
# raw tile strips whose tile order matches the image left to right.
SPRITE_PNGS  = res/player_sub.png res/torpedo.png
TILESET_PNGS = res/hud_tiles.png

ASSET_SOURCES = $(patsubst res/%.png,$(ASSETDIR)/%.c,$(SPRITE_PNGS) $(TILESET_PNGS))
ASSET_HEADERS = $(ASSET_SOURCES:.c=.h)
CSOURCES      = $(wildcard src/*.c)
HEADERS       = $(wildcard src/*.h)

OBJS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(CSOURCES)) \
       $(patsubst $(ASSETDIR)/%.c,$(ASSETDIR)/%.o,$(ASSET_SOURCES))

all: $(ROM)

$(ROM): $(OBJS)
	$(LCC) $(LCCFLAGS) $(ROMFLAGS) -o $@ $(OBJS)

# Every game object depends on every header: the project is small, so a full
# recompile on header change is cheaper than maintaining dependency files.
$(BUILDDIR)/%.o: src/%.c $(HEADERS) $(ASSET_HEADERS) | $(BUILDDIR)
	$(LCC) $(LCCFLAGS) -c -o $@ $<

$(ASSETDIR)/%.o: $(ASSETDIR)/%.c
	$(LCC) $(LCCFLAGS) -c -o $@ $<

$(patsubst res/%.png,$(ASSETDIR)/%.c,$(SPRITE_PNGS)): $(ASSETDIR)/%.c: res/%.png | $(ASSETDIR)
	$(PNG2ASSET) $< -o $@ -spr8x8 -keep_palette_order -noflip -no_palettes

$(patsubst res/%.png,$(ASSETDIR)/%.c,$(TILESET_PNGS)): $(ASSETDIR)/%.c: res/%.png | $(ASSETDIR)
	$(PNG2ASSET) $< -o $@ -map -tiles_only -keep_duplicate_tiles -noflip -keep_palette_order -no_palettes

# png2asset writes the .h next to the .c
$(ASSETDIR)/%.h: $(ASSETDIR)/%.c ;

$(BUILDDIR) $(ASSETDIR):
	mkdir -p $@

run: $(ROM)
	$(EMULATOR) $(ROM)

# Toolchain smoke test; remove once Seaquest reaches MVP.
hello: Hello.gb

Hello.gb: hello/main.c
	$(LCC) -o $@ $<

clean:
	rm -rf $(BUILDDIR) $(ROM) $(PROJECTNAME).map $(PROJECTNAME).noi $(PROJECTNAME).sym $(PROJECTNAME).ihx Hello.gb

.PHONY: all run hello clean
.SECONDARY: $(ASSET_SOURCES) $(ASSET_HEADERS)
