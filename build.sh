#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra -ggdb $(pkg-config --cflags raylib)"
LIBS="$(pkg-config --libs raylib) -lm -ldl -lpthread -lX11"

mkdir -p ./build/
if [ ! -z "${HOTRELOAD}" ]; then
  clang $CFLAGS -o ./build/libplug.so -fPIC -shared \
    ./src/plug.c ./src/tinyfiledialogs.c \
    $LIBS

  clang $CFLAGS -DHOTRELOAD -o ./build/music \
    ./src/music.c \
    $LIBS -L./build/
else
  clang $CFLAGS -o ./build/music \
    ./src/plug.c ./src/music.c ./src/tinyfiledialogs.c \
    $LIBS -L./build/
fi

clang -o ./build/fft ./src/fft.c -lm
