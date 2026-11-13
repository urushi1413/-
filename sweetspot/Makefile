# raylib がインストール済みの前提（pkg-config / Homebrew / w64devkit など）
CC     ?= gcc
CFLAGS ?= -std=c99 -Wall -O2
TARGET  = sweetspot

ifeq ($(OS),Windows_NT)
    # raylib の Windows インストーラーを入れた場合の場所（違う場所なら make RAYLIB_PATH=... で指定）
    RAYLIB_PATH ?= C:/raylib/raylib/src
    CFLAGS += -I$(RAYLIB_PATH)
    LIBS = -L$(RAYLIB_PATH) -lraylib -lopengl32 -lgdi32 -lwinmm -limm32
    TARGET := $(TARGET).exe
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Darwin)
        LIBS = -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    else
        LIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    endif
endif

SRC = main.c player.c input.c data.c physics.c shots.c serve.c render.c ai.c score.c assist.c playlog.c platform_ime.c

$(TARGET): $(SRC) game.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f sweetspot sweetspot.exe ai_vs_ai

# AI 同士の試合でバランスを確認する（画面なし。raylib のヘッダだけ使う）
RAYLIB_INC ?= /usr/local/include
sim: tools/ai_vs_ai.c $(SRC) game.h
	$(CC) -std=c99 -O2 -DRAYMATH_STATIC_INLINE -I$(RAYLIB_INC) tools/ai_vs_ai.c -o ai_vs_ai -lm -Wl,--unresolved-symbols=ignore-all
	./ai_vs_ai
