# raylib がインストール済みの前提（pkg-config / Homebrew / w64devkit など）
CC     ?= gcc
CFLAGS ?= -std=c99 -Wall -O2
TARGET  = tennis

ifeq ($(OS),Windows_NT)
    LIBS = -lraylib -lopengl32 -lgdi32 -lwinmm
    TARGET := $(TARGET).exe
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Darwin)
        LIBS = -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    else
        LIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    endif
endif

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(LIBS)

clean:
	rm -f tennis tennis.exe
