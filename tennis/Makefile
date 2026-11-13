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
	rm -f tennis tennis.exe ai_vs_ai

# AI 同士の試合でバランスを確認する（画面なし。raylib のヘッダだけ使う）
RAYLIB_INC ?= /usr/local/include
sim: tools/ai_vs_ai.c main.c
	$(CC) -std=c99 -O2 -DRAYMATH_STATIC_INLINE -I$(RAYLIB_INC) tools/ai_vs_ai.c -o ai_vs_ai -lm -Wl,--unresolved-symbols=ignore-all
	./ai_vs_ai
