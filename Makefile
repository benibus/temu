SRC_DIR := src
OBJ_DIR := build

BIN := temu
SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

PKG_CONFIG = pkg-config
IDLIBS = $(shell $(PKG_CONFIG) --cflags xrender freetype2 fontconfig)
LDLIBS = $(shell $(PKG_CONFIG) --libs xrender freetype2 fontconfig) -lutil -lm
LFLAGS =
IFLAGS =

CC ?= cc

CPPFLAGS_COMMON  = $(IFLAGS) $(IDLIBS) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600
CPPFLAGS_DEBUG   = $(CPPFLAGS_COMMON) -DBUILD_DEBUG -MMD -MP
CPPFLAGS_RELEASE = $(CPPFLAGS_COMMON) -DBUILD_RELEASE
WFLAGS = -Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function
CFLAGS_COMMON  = -std=c99 $(WFLAGS)
CFLAGS_DEBUG   = -O0 -ggdb3 -no-pie $(CFLAGS_COMMON)
CFLAGS_RELEASE = -O2 $(CFLAGS_COMMON)

BUILD := Debug
ifeq ($(BUILD), Release)
	CFLAGS = $(CFLAGS_RELEASE)
	CPPFLAGS = $(CPPFLAGS_RELEASE)
else
	CFLAGS = $(CFLAGS_DEBUG)
	CPPFLAGS = $(CPPFLAGS_DEBUG)
endif

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) | $(OBJ_DIR)
	$(CC) $(LFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -pv $@

clean:
	@echo cleaning...
	@$(RM) -rv $(OBJ_DIR)
	@$(RM) -v $(BIN)

-include $(OBJ:.o=.d)
