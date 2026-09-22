# SM6337 Edit

WordPad-подобный текстовый редактор для настоящей Windows 98 SE с прямой печатью
на принтер СМ6337 (Epson ESC/P-совместимый клон) через LPT1, без драйвера.

## Сборка

Нужен тулчейн `i686-w64-mingw32` (msvcrt, win32-threads) — например,
[niXman/mingw-builds-binaries](https://github.com/niXman/mingw-builds-binaries).
Распакуй в `toolchain/mingw32/`, затем:

```
make
```

Результат — `build/sm6337edit.exe` + иконки/DLL рядом с ним.

## Формат

- `src/` — исходники (main.c, printer.c, resource.h, printer.h)
- `redist/` — riched20.dll/riched32.dll (RichEdit из Win98 SE) и `icons/toolbar.bmp`
- `printer_commands.md` — справочник команд ESC/P и кодовых таблиц СМ6337
- `rom/` — дамп символогенератора принтера

## Запуск

Только на настоящей Windows 98 SE (или совместимой) — используются `*A` (ANSI)
Win32 API и прямая запись в LPT1.
