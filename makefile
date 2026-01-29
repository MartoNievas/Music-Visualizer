CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = -lraylib -lm -lpthread -ldl -lrt -lX11

SRC = src/music.c
BIN = music

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all run clean
