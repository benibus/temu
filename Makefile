SRC_DIR := src
OBJ_DIR := build

BIN := temu
SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

PKG_CONFIG = pkg-config
IDLIBS = $(shell $(PKG_CONFIG) --cflags freetype2 fontconfig)
LDLIBS = $(shell $(PKG_CONFIG) --libs freetype2 fontconfig) -lX11 -lutil -lXft
LFLAGS =
IFLAGS =

CC ?= cc
CPPFLAGS = $(IFLAGS) $(IDLIBS) -MMD -MP \
         -D_POSIX_C_SOURCE=199309L -D_XOPEN_SOURCE=600
WFLAGS = -Wall -Wextra -Wpedantic \
         -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function
CFLAGS_COMMON  = -std=c99 $(WFLAGS)
CFLAGS_DEBUG   = -O0 -ggdb3 -no-pie $(CFLAGS_COMMON)
CFLAGS_RELEASE = -O2 $(CFLAGS_COMMON)

BUILD := Debug
ifeq ($(BUILD), Release)
	CFLAGS = $(CFLAGS_RELEASE)
else
	CFLAGS = $(CFLAGS_DEBUG)
endif

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) | $(OBJ_DIR)
	$(CC) $(LFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $^ -o $@

$(OBJ_DIR):
	@mkdir -pv $@

clean:
	@echo cleaning...
	@$(RM) -rv $(OBJ_DIR)
	@$(RM) -v $(BIN)

-include $(OBJ:.o=.d)
