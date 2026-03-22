# OSRS PvP C Environment Makefile
#
# standalone targets (no PufferLib dependency):
#   make           — headless benchmark binary
#   make visual    — headed raylib viewer with human input
#   make debug     — debug build with sanitizers
#
# PufferLib training uses setup.py build_osrs instead.

CC = clang
CFLAGS = -Wall -Wextra -O3 -ffast-math -flto -fPIC -std=c11
DEBUG_FLAGS = -Wall -Wextra -g -O0 -fPIC -std=c11 -DDEBUG
LDFLAGS = -lm

TARGET = osrs_pvp
DEMO_SRC = osrs_pvp.c
HEADERS = osrs_pvp.h

# Raylib (for visual target). download from https://github.com/raysan5/raylib/releases
RAYLIB_DIR = raylib-5.5_macos
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
RAYLIB_FLAGS = -I$(RAYLIB_DIR)/include $(RAYLIB_DIR)/lib/libraylib.a \
               -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo
else
RAYLIB_FLAGS = -I$(RAYLIB_DIR)/include -L$(RAYLIB_DIR)/lib -lraylib -lGL -lpthread -ldl -lrt
endif

.PHONY: all clean debug visual

all: $(TARGET)

$(TARGET): $(DEMO_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(DEMO_SRC) $(LDFLAGS)

visual: $(DEMO_SRC) $(HEADERS) osrs_pvp_render.h osrs_pvp_gui.h
	$(CC) $(CFLAGS) -DOSRS_PVP_VISUAL $(RAYLIB_FLAGS) -o $(TARGET)_visual $(DEMO_SRC) $(LDFLAGS)

debug: $(DEMO_SRC) $(HEADERS)
	$(CC) $(DEBUG_FLAGS) -o $(TARGET)_debug $(DEMO_SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET)_debug $(TARGET)_visual *.o
