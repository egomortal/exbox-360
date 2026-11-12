SDK ?= /opt/ps5-payload-sdk
CC := $(SDK)/bin/prospero-clang
CFLAGS := -O2 -Wall -Wextra -D__PS5__ -I$(SDK)/target/user/homebrew/include -I.
SOURCES := main.c gpl/pt.c gpl/elfldr.c gpl/hbldr.c gpl/fs_readfile.c
LDLIBS := -lSceSystemService -lSceUserService -lkernel_sys -lm -lz -lbz2 -liconv

all: build/nxe_dashboard_daemon.elf

build/nxe_dashboard_daemon.elf: $(SOURCES) gpl/hbldr.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f build/nxe_dashboard_daemon.elf

.PHONY: all clean
