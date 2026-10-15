# Build with MinGW-w64 (32-bit): sudo apt install gcc-mingw-w64-i686 zip
CROSS   ?= i686-w64-mingw32-
CC      := $(CROSS)gcc
CFLAGS  := -O2 -Wall -s -static-libgcc
LDFIX   := -Wl,--enable-stdcall-fixup
B       := build
VERSION ?= dev

all: $(B)/WAIL32.DLL $(B)/PITMM.DLL $(B)/PitfallFix.exe

$(B):
	mkdir -p $(B)

$(B)/WAIL32.DLL: src/wail32/wail32.c src/wail32/wail32.def | $(B)
	$(CC) $(CFLAGS) -shared -o $@ $^ -ldsound -lwinmm $(LDFIX)

$(B)/PITMM.DLL: src/pitmm/pitmm.c src/pitmm/pitmm.def | $(B)
	$(CC) $(CFLAGS) -shared -o $@ $^ -lwinmm -luser32 $(LDFIX)

$(B)/embedded.h: $(B)/WAIL32.DLL $(B)/PITMM.DLL config/PITMUSIC.INI tools/embed.py
	python3 tools/embed.py $@ wail32_dll=$(B)/WAIL32.DLL pitmm_dll=$(B)/PITMM.DLL pitmusic_ini=config/PITMUSIC.INI

$(B)/PitfallFix.exe: src/installer/pitfallfix.c $(B)/embedded.h
	$(CC) $(CFLAGS) -I$(B) -DPITFALLFIX_VERSION=\"$(VERSION)\" -o $@ src/installer/pitfallfix.c

dist: all
	rm -rf $(B)/dist && mkdir -p $(B)/dist/PitfallFix/music
	cp $(B)/PitfallFix.exe README.md README.pt-BR.md LICENSE $(B)/dist/PitfallFix/
	cp config/PITMUSIC-soundtrack-example.ini $(B)/dist/PitfallFix/
	touch $(B)/dist/PitfallFix/music/.keep
	cd $(B)/dist && zip -r -9 ../PitfallFix-$(VERSION).zip PitfallFix

clean:
	rm -rf $(B)

.PHONY: all dist clean
