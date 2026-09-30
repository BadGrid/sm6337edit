# SM6337 Edit

**English** | [Русский](README.md)

A WordPad-like text editor for real **Windows 98 SE** that prints directly to the Soviet dot-matrix
printer **SM6337** (СМ6337, an Epson ESC/P-compatible clone) over LPT1 — **no driver**, raw ESC/P
commands.

![Screenshot](docs/screenshot.png)

The program is written and tested on real hardware: both Win98 SE and the printer itself are real,
not emulated.

> The program's interface and built-in help are currently in Russian only.

## Why

The stock generic Windows driver for printers like this one rasterizes everything as graphics, which
makes small text unreadable. SM6337 Edit sends native ESC/P commands to the printer instead, so the
text comes out crisp, as if printed from the printer's own ROM font.

## Features

- WordPad-style editing: bold, underline, text color (a marker for on-screen review only — the
  printer is monochrome)
- Things WordPad does not have because they are about the printer itself:
  - double-strike (ESC G/H) and expanded (ESC W) printing
  - pitch 10/12/17 cpi (Pica / Elite / Pica+Condensed) — shown on screen with different widths at the
    same character height (own fonts from `redist/fonts/`)
  - superscript / subscript (ESC S/T)
  - code page: standard PC table 14 or the additional CP866-compatible table 15 (ESC R)
- Custom page limits (characters per line, lines per page, left margin, line spacing) — line wrapping
  either in the editor or by the printer (ESC Q/C)
- Landscape orientation (a switch in the print settings): turning it on converts the page size
  automatically in proportion to the A4 sides (longer line, fewer lines, e.g. 82x70 -> 116x49), and back
  the same way; the print preview shows the sheet sideways
- "Do not send hardware form feed" option — on A4 sheets a plain form feed sends the printer into
  an out-of-paper fault (АВР), so by default a number of empty lines is sent instead
- Unidirectional printing (ESC U) — more precise, but slower
- Printing the file name centered on the first line of the sheet - a switch in the print settings
- Open/save `.rtf` (RichEdit serializes the formatting itself)
  and plain `.txt`; the program asks about unsaved changes before closing, creating a new document or opening another file (an asterisk in the window title marks them), a file can be dropped onto the window or opened from the command line; saving goes through a temporary file, so the previous one survives a failure
- All settings live in `C:\edit\sm6337.ini` and survive restarts
- Print preview (File → Print Preview, the sheet-and-magnifier button): shows the pages exactly as
  the printer will produce them — built from the same ESC/P stream that goes to LPT1
- Find (Ctrl+F, F3 for find next) and redo (Ctrl+Y)
- Built-in WinHelp help (F1, Help → Contents), 13 topics from formatting to printer troubleshooting
  (Russian); sources in `help/`, compiled `sm6337edit.hlp` in `redist/`
- Toolbar: tooltips on every button, grayed-out buttons for unavailable actions, pressed buttons for the
  formatting of the current selection (bold/underline/pitch/subscript etc.)

## Download

A ready-made build (exe + the required DLLs and icons) is published in
[Releases](../../releases) as one zip archive — unpack the whole folder onto a Win98 SE machine.
Report problems through [Issues](../../issues/new/choose).

## Building

### Locally

You need the `i686-w64-mingw32` toolchain (msvcrt, win32-threads, **not** UCRT), for example
[niXman/mingw-builds-binaries](https://github.com/niXman/mingw-builds-binaries). Unpack it into
`toolchain/mingw32/`, then:

```
make
```

The result is `build/sm6337edit.exe` with `riched20.dll`, `riched32.dll`, `toolbar.bmp` and the help
files `sm6337edit.hlp` / `sm6337edit.cnt` next to it. Copy the whole `build/` folder to the target
machine.

### GitHub Actions

On every push to `master` and every pull request the repository builds `build/` with the same
toolchain (`.github/workflows/build.yml`) and puts it into the Artifacts of that run on the Actions
tab, so you can download a ready folder without a local toolchain.

## Repository layout

- `src/` — sources (`main.c`, `printer.c`/`printer.h` — printing and code pages, `preview.c`/`preview.h` —
  print preview, `resource.h`)
- `help/` — help sources (RTF, project and contents); how to compile them is in `help/README.md`
- `redist/` — `riched20.dll` / `riched32.dll` (RichEdit from Win98 SE), `icons/toolbar.bmp`, the
  compiled help `sm6337edit.hlp` and `fonts/` — the pitch 10/12/17 fonts (derived from Noto Sans Mono,
  OFL license)
- `printer_commands.md` — reference of ESC/P commands and code tables of the SM6337, compiled by hand
  from the manual (`СМ6337 РЭ.djvu`); in Russian
- `rom/` — dump of the printer's character generator

## Why ANSI and not Unicode

The code was originally written against the `*W` (Unicode) Win32 API. On real Win98 SE it turned out
that some basic `*W` functions (`GetModuleFileNameW`, `LoadLibraryW`, `wsprintfW`, as well as some
`CHARFORMAT2` fields such as `CFM_SPACING`) are either not implemented or do not behave as
documented. It was rewritten to the classic ANSI path (`*A` + CP1251), which is how Russian Win9x
programs were historically built, and it is genuinely more reliable on that OS.

## Running

Real Windows 98 SE (or a compatible Win9x) only — the program uses the `*A` (ANSI) Win32 API and
writes directly to LPT1 through `CreateFileA("LPT1", ...)`.

## Authorship

The project is developed by its author together with the AI assistant Claude (Anthropic): the
assistant wrote and edited code, icons, tooltips, find and redo, print preview, the GitHub Actions
build and the documentation, under the author's direction. Everything is tested by the author on real
hardware: Win98 SE and the SM6337 printer. The assistant's participation is noted in commit messages
(the `Assisted-by` trailer).

---

<sub>On the right side of the toolbar there is a button with a cross that is not tied to any editing
command — it is an Easter-egg prank (a series of dialogs and, on real Win9x, a simulated system hang).
It has nothing to do with the editor's or the printer's features; it is easy to find and is
deliberately not documented in more detail out of respect for those who want to stumble upon it on
their own.</sub>
