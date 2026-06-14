#!/bin/sh

BUILD_MODE=$1
ROOT_DIR="$PWD"

EXE=igario
SOURCES="$ROOT_DIR/igario.c $ROOT_DIR/os_linux.c"

RAYLIB_DIR="$ROOT_DIR/vendor/raylib/src"
RAYLIB_DEFINES="-D_GNU_SOURCE -D_DEFAULT_SOURCE -DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33 -D_GLFW_X11"
RAYLIB_SOURCES="$RAYLIB_DIR/rcore.c $RAYLIB_DIR/rshapes.c $RAYLIB_DIR/rtextures.c $RAYLIB_DIR/rtext.c $RAYLIB_DIR/rmodels.c $RAYLIB_DIR/raudio.c $RAYLIB_DIR/rglfw.c"
RAYLIB_INCLUDES="-I$RAYLIB_DIR -I$RAYLIB_DIR/external/glfw/include"

BUILD_DIR="$ROOT_DIR/build"
INCLUDES="-I$RAYLIB_DIR"
LINKARGS="-lm -ldl -lpthread -lX11 -lxcb -lGL -lGLX -lXext -lGLdispatch -lXau -lXdmcp -lX11"
CFLAGS="-std=c99"
WARNINGS="-Wall -Wextra"

if [ -z "$CC" ]; then
    CC=cc
fi

if [ "$BUILD_MODE" = "RELEASE" ]; then
    CFLAGS="$CFLAGS -Oz -s"
    BUILD_DIR="${BUILD_DIR}_release"
    LINKARGS="$LINKARGS -flto -fwhole-program -ffunction-sections -fdata-sections -Wl,--gc-sections"
else
    CFLAGS="$CFLAGS -O0 -ggdb -DDEBUG"
    BUILD_DIR="${BUILD_DIR}_debug"
fi

if [ ! -d "$BUILD_DIR" ]; then
    mkdir "$BUILD_DIR"
fi

if [ ! -d "$BUILD_DIR/raylib" ]; then
    mkdir "$BUILD_DIR/raylib"
    cd "$BUILD_DIR/raylib" && "$CC" -c $RAYLIB_DEFINES $RAYLIB_INCLUDES $CFLAGS $RAYLIB_SOURCES
fi

cd "$BUILD_DIR" && "$CC" -o $EXE -D_GNU_SOURCE $INCLUDES $CFLAGS $WARNINGS $BUILD_DIR/raylib/*.o $SOURCES $LINKARGS
