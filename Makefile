# --- Configuration ---
CC = clang
CFLAGS = -Wall -Wextra -ggdb $(shell pkg-config --cflags raylib)
LIBS = $(shell pkg-config --libs raylib) -lm -ldl -lpthread -lX11

# Directories
SRC_DIR = src
BUILD_DIR = build

# Source files (Adjust 'musicalizer.c' if your main file is named 'music.c')
HOST_SRC = $(SRC_DIR)/musicalizer.c
PLUG_SRC = $(SRC_DIR)/plug.c
TINY_SRC = $(SRC_DIR)/tinyfiledialogs.c
FFT_SRC = $(SRC_DIR)/fft.c

# Output names
TARGET_MUSIC = $(BUILD_DIR)/music
TARGET_LIBPLUG = $(BUILD_DIR)/libplug.so
TARGET_FFT = $(BUILD_DIR)/fft

# --- Build Logic ---

.PHONY: all prepare clean

# Default rule: builds music and fft
all: prepare $(TARGET_MUSIC) $(TARGET_FFT)

# Create build directory
prepare:
	@mkdir -p $(BUILD_DIR)

# Logic for 'music' executable based on HOTRELOAD environment variable
$(TARGET_MUSIC): $(HOST_SRC) $(PLUG_SRC) $(TINY_SRC)
ifdef HOTRELOAD
	@echo "--- Building in HOT RELOAD mode ---"
	$(CC) $(CFLAGS) -o $(TARGET_LIBPLUG) -fPIC -shared $(PLUG_SRC) $(TINY_SRC) $(LIBS)
	$(CC) $(CFLAGS) -DHOTRELOAD -o $(TARGET_MUSIC) $(HOST_SRC) $(LIBS) -L$(BUILD_DIR)
else
	@echo "--- Building in STANDARD mode ---"
	$(CC) $(CFLAGS) -o $(TARGET_MUSIC) $(HOST_SRC) $(PLUG_SRC) $(TINY_SRC) $(LIBS) -L$(BUILD_DIR)
endif

# Build FFT tool
$(TARGET_FFT): $(FFT_SRC)
	$(CC) -o $(TARGET_FFT) $(FFT_SRC) -lm

# Utility rules
clean:
	rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned."

run: all
	./$(TARGET_MUSIC)
