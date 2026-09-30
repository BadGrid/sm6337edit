TOOLCHAIN := $(CURDIR)/toolchain/mingw32/bin
export PATH := $(TOOLCHAIN):$(PATH)
CC := $(TOOLCHAIN)/gcc.exe

CFLAGS := -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -mwindows -O2 -Wall -finput-charset=UTF-8 -fexec-charset=CP1251
LIBS := -lcomctl32 -lcomdlg32 -lgdi32 -lshell32

SRC := src/main.c src/printer.c src/preview.c
OUT := build/sm6337edit.exe

all: $(OUT)

build:
	mkdir -p build

$(OUT): $(SRC) src/resource.h src/printer.h src/preview.h | build
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)
	cp redist/riched20.dll redist/riched32.dll build/
	cp redist/icons/toolbar.bmp build/
	cp help/sm6337edit.cnt build/
	-cp redist/sm6337edit.hlp build/
	-cp redist/fonts/*.ttf redist/fonts/OFL.txt build/

clean:
	rm -rf build

.PHONY: all clean
