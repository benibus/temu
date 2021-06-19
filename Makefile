SRCDIR = src/
OBJDIR = build/

PKG_CONFIG = pkg-config

INCFLAGS = $(shell $(PKG_CONFIG) --cflags freetype2 fontconfig)
LDFLAGS = -lX11 -lutil -lXft $(shell $(PKG_CONFIG) --libs freetype2 fontconfig)

CC ?= cc

CPPFLAGS = -D_POSIX_C_SOURCE=199309L -D_XOPEN_SOURCE=600
WFLAGS = -Wall -Wextra -Wpedantic \
         -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function

CFLAGS_COMMON  = -std=c99 $(WFLAGS) $(INCFLAGS)

CFLAGS_DEBUG   = -O0 -ggdb3 -no-pie $(CFLAGS_COMMON)
CFLAGS_RELEASE = -O2 $(CFLAGS_COMMON)

BUILD := Debug
ifeq ($(BUILD), Release)
	CFLAGS = $(CFLAGS_RELEASE)
else
	CFLAGS = $(CFLAGS_DEBUG)
endif

BIN = temu
INCLUDE = x.h parser.h ring.h term.h utils.h config.h
OBJ = main.o x.o parser.o ring.o pty.o keys.o buffer.o utils.o

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(@) $(OBJ) $(LDFLAGS)

utils.o: utils.c utils.h

buffer.o: $(INCLUDE)
keys.o:   $(INCLUDE)
pty.o:    $(INCLUDE)
ring.o:   $(INCLUDE)
parser.o: $(INCLUDE)
x.o:      $(INCLUDE)
main.o:   $(INCLUDE)

clean:
	rm -f $(BIN) *.o *.bin
