#!/bin/sh
set -xe

CFLAGS="-Wall -Wextra -ggdb $(pkg-config --cflags raylib)"
LIBS="$(pkg-config --libs raylib) -lm -ldl -lpthread"

mkdir -p build

if [ -n "$RELOAD" ]; then
  # 🔥 plugin dinámico
  gcc $CFLAGS -fPIC -shared src/plug.c -o build/libplug.so $LIBS
  gcc $CFLAGS -DRELOAD src/music.c -o build/music $LIBS
else
  # 🔒 modo estático
  gcc $CFLAGS src/music.c src/plug.c -o build/music $LIBS
fi
