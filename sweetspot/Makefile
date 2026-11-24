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

# 版の印（表紙に出す）：コミットの短い番号・コミットの日時・ブランチ（claude/ は省く）。手元に変更があれば + を付ける。git がなければ unknown
GIT_ID     := $(shell git log -1 "--format=%h %cd" "--date=format-local:%m/%d %H:%M" 2>/dev/null)
GIT_BRANCH := $(patsubst claude/%,%,$(shell git rev-parse --abbrev-ref HEAD 2>/dev/null))
GIT_DIRTY  := $(if $(shell git status --porcelain --untracked-files=no 2>/dev/null),+,)
BUILD_ID   := $(if $(GIT_ID),$(GIT_ID)$(GIT_DIRTY)  $(GIT_BRANCH),unknown)
VERFLAGS    = -DBUILD_ID='"$(BUILD_ID)"'

SRC = main.c body_model.c parts_model.c player.c input.c audio.c data.c physics.c shots.c serve.c render.c ai.c score.c assist.c playlog.c platform_ime.c tour.c replay.c matchlog.c lang.c demo.c events.c touch.c storage.c career.c coach.c

$(TARGET): $(SRC) game.h ja_glyphs.h
	$(CC) $(CFLAGS) $(VERFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f sweetspot sweetspot.exe sweetspot_sale sweetspot_sale.exe ai_vs_ai
	rm -rf web/out web/mobile web/mobile_demo build

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

# ブラウザ版（体験版）：make web（emsdk を入れて emsdk_env を読み込んでから。raylib をブラウザ向けに作った libraylib.a が要る）
#   WEB_RAYLIB に raylib の src（PLATFORM_WEB で make したもの）を指定する。web/ に index.html・.js・.wasm・.data ができる
#   勝利の証明コードの秘密の鍵は web/demo_secret.txt（なければ作る。リポジトリには入れない。なくすとコードを確かめられないので保管する）
WEB_RAYLIB ?= ../raylib/src
web/demo_secret.txt:
	python3 -c "import secrets; print(secrets.token_hex(16))" > web/demo_secret.txt
web: $(SRC) game.h ja_glyphs.h web/shell.html web/demo_secret.txt
	mkdir -p web/out
	sh tools/stage_web_sounds.sh
	emcc -std=gnu99 -O2 -Wall $(VERFLAGS) -DSALE_BUILD -DDEMO_BUILD -DDEMO_SECRET=\"$(shell cat web/demo_secret.txt 2>/dev/null)\" -DPLATFORM_WEB -I$(WEB_RAYLIB) $(SRC) $(WEB_RAYLIB)/libraylib.a -o web/out/index.html \
	    -sUSE_GLFW=3 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=HEAPF32,FS \
	    --shell-file web/shell.html --preload-file build/web_sounds@sounds --preload-file fonts --preload-file models --exclude-file models/players

# スマホ版（ブラウザ）：make mobile。タイブレークの試合だけ、画面を触る操作、売る版の名前。web/mobile/ にできる
#   メモリは始めから 160MB（128MB だと読み込みの途中で 154MB に増やしていた。途中で増やさない。前は効果音を 3 回ずつ丸ごと読んで 265MB まで増え、iPhone の Safari で落ちていたらしい）
mobile: $(SRC) game.h ja_glyphs.h web/shell_mobile.html
	mkdir -p web/mobile
	sh tools/stage_web_sounds.sh
	emcc -std=gnu99 -O2 -Wall $(VERFLAGS) -DSALE_BUILD -DMOBILE_BUILD -DPLATFORM_WEB -I$(WEB_RAYLIB) $(SRC) $(WEB_RAYLIB)/libraylib.a -o web/mobile/index.html \
	    -sUSE_GLFW=3 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=167772160 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=HEAPF32,FS \
	    --shell-file web/shell_mobile.html --preload-file build/web_sounds@sounds --preload-file fonts --preload-file models --exclude-file models/players

# スマホ版の体験版（ブラウザ）：make mobile-demo。スマホ版に体験版の制限（タイブレークの試合 DEMO_MATCHES 回）と勝利の証明コードを入れる。web/mobile_demo/ にできる
#   勝利の証明コードの鍵は make web と同じ web/demo_secret.txt（なくすとコードを確かめられないので保管する）
mobile-demo: $(SRC) game.h ja_glyphs.h web/shell_mobile.html web/demo_secret.txt
	mkdir -p web/mobile_demo
	sh tools/stage_web_sounds.sh
	emcc -std=gnu99 -O2 -Wall $(VERFLAGS) -DSALE_BUILD -DMOBILE_BUILD -DDEMO_BUILD -DDEMO_SECRET=\"$(shell cat web/demo_secret.txt 2>/dev/null)\" -DPLATFORM_WEB -I$(WEB_RAYLIB) $(SRC) $(WEB_RAYLIB)/libraylib.a -o web/mobile_demo/index.html \
	    -sUSE_GLFW=3 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=167772160 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=HEAPF32,FS \
	    --shell-file web/shell_mobile.html --preload-file build/web_sounds@sounds --preload-file fonts --preload-file models --exclude-file models/players

.PHONY: dist clean sim web mobile mobile-demo
