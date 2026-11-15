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

# 売る版：make SALE=1（実在の選手・大会に似せた名前・身長の表示・ルーティン・決まったしぐさ・大会名を架空のものに）
ifeq ($(SALE),1)
    CFLAGS += -DSALE_BUILD
    TARGET := sweetspot_sale$(suffix $(TARGET))
    ifeq ($(OS),Windows_NT)
        # 配る exe：必要な部品を exe に全部入れる（ほかのパソコンで DLL が見つからないと言われないように）、コンソールの窓を出さない
        LIBS += -static -mwindows
    endif
endif

SRC = main.c player.c input.c audio.c data.c physics.c shots.c serve.c render.c ai.c score.c assist.c playlog.c platform_ime.c tour.c replay.c matchlog.c lang.c

$(TARGET): $(SRC) game.h ja_glyphs.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f sweetspot sweetspot.exe sweetspot_sale sweetspot_sale.exe ai_vs_ai

# AI 同士の試合でバランスを確認する（画面なし。raylib のヘッダだけ使う）
RAYLIB_INC ?= /usr/local/include
sim: tools/ai_vs_ai.c $(SRC) game.h
	$(CC) -std=c99 -O2 -DRAYMATH_STATIC_INLINE -I$(RAYLIB_INC) tools/ai_vs_ai.c -o ai_vs_ai -lm -Wl,--unresolved-symbols=ignore-all
	./ai_vs_ai


# 配る形を作る：売る版を作って、dist/SweetSpot に exe・音・フォント・クレジット・遊び方をまとめ、zip にする
#   Windows：dist/SweetSpot_Windows.zip（PowerShell を使う）。Mac / Linux：dist/SweetSpot_<OS>.zip（zip があれば）
dist:
	$(MAKE) SALE=1
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/make_dist.ps1
else
	sh tools/make_dist.sh
endif

.PHONY: dist clean sim
