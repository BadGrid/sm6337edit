TOOLCHAIN := $(CURDIR)/toolchain/mingw32/bin
export PATH := $(TOOLCHAIN):$(PATH)
CC := $(TOOLCHAIN)/gcc.exe
WINDRES := $(TOOLCHAIN)/windres.exe

CFLAGS := -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -mwindows -O2 -Wall -finput-charset=UTF-8 -fexec-charset=CP1251
LIBS := -lcomctl32 -lcomdlg32 -lgdi32 -lshell32

SRC := src/main.c src/printer.c src/preview.c
RES := obj/app_res.o
OUT := build/sm6337edit.exe

# Всё, что нужно программе, встраивается в exe ресурсами (src/app.rc): иконки, RichEdit, шрифты, справка.
RESFILES := redist/icons/toolbar.bmp redist/riched20.dll redist/riched32.dll \
            redist/fonts/sm6337-10.ttf redist/fonts/sm6337-12.ttf redist/fonts/sm6337-17.ttf \
            redist/fonts/sm6337-w.ttf redist/fonts/OFL.txt redist/sm6337edit.hlp help/sm6337edit.cnt

all: $(OUT)

build:
	mkdir -p build

obj:
	mkdir -p obj

$(RES): src/app.rc src/resource.h $(RESFILES) | obj
	$(WINDRES) -Isrc -I. -O coff src/app.rc -o $(RES)

$(OUT): $(SRC) src/resource.h src/printer.h src/preview.h $(RES) | build
	$(CC) $(CFLAGS) $(SRC) $(RES) -o $(OUT) $(LIBS)

# Версия со всеми файлами рядом с exe: можно подменить любую DLL, шрифт, иконки или справку
full: $(OUT)
	mkdir -p build-full
	cp $(OUT) build-full/
	cp redist/riched20.dll redist/riched32.dll redist/icons/toolbar.bmp redist/sm6337edit.hlp help/sm6337edit.cnt build-full/
	cp redist/fonts/*.ttf redist/fonts/OFL.txt build-full/

clean:
	rm -rf build obj build-full

.PHONY: all full clean
