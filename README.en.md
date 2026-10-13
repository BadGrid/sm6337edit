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
    same character height (own fonts embedded in the exe)
  - superscript / subscript (ESC S/T)
  - code page: standard PC table 14 or the additional CP866-compatible table 15 (ESC R)
- WordPad-style paragraph formatting: left, center, right and justified alignment (Ctrl+L/E/R/J), left
  and first-line indent, tab stops (including right and centered ones — "date ... No. number" on one
  line). The printer cannot do this itself, so the program works from the real character widths and
  prints spaces: paper and print preview match. Lines wrap at word boundaries. Justify works only if the
  RichEdit version supports it (RichEdit 2.0 from Win98 does not: the paragraph stays left-aligned)
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

## How formatting becomes printer commands

The program does not keep "pitch" as a separate attribute — RichEdit has no such thing. The pitch is
derived from the **font name** and character height of each run of text:

| Font in the text | What the printer gets |
|---|---|
| `SM6337 Mono 10` or `Courier New` | 10 cpi (Pica) |
| `SM6337 Mono 12` | 12 cpi (Elite, ESC M) |
| `SM6337 Mono 17` | 17 cpi (condensed, SI) |
| `SM6337 Mono Wide` | expanded (ESC W) |
| `Terminal` | 12 cpi — **legacy scheme**, see below |
| `Small Fonts` | 17 cpi — **legacy scheme**, see below |
| `Courier New` exactly 14 pt | expanded — **legacy scheme** |
| any other font | 10 cpi, as Pica |

The rest:

- bold -> ESC E, underline -> ESC -, superscript/subscript -> ESC S/T;
- **strikeout -> double strike** (ESC G): RichEdit has no "double strike" attribute, so the least
  useful one for printer text was borrowed; on screen it looks like ordinary strikeout;
- **color and font size do not affect printing** (except the legacy 14 pt Courier New) — the printer is
  monochrome and only the pitch sets the character width;
- if the own `SM6337 Mono *` fonts are missing (could not be installed), the screen shows some substitute
  and on-screen widths no longer match the print; the ESC/P stream is unaffected, because it is built
  from the font name stored in the file.

**Legacy scheme.** The first versions had no own fonts and encoded the pitch with Windows raster fonts:
`Terminal` is 12, `Small Fonts` is 17, Courier New 14 pt is expanded. It is kept for old `.rtf` files.
Side effect: a foreign document set in `Terminal` or `Small Fonts` prints at a different pitch than it
looks. Use `SM6337 Mono *` in new documents.

## Download

[Releases](../../releases) has two variants of the same program:

- **`sm6337edit.exe`** — a single file: RichEdit, icons, fonts and help are embedded in it. Copy it to a
  Win98 SE machine and run it. On startup the program unpacks the embedded DLLs, fonts and help into
  `%TEMP%\SM6337Edit\` (files already there are not rewritten).
- **`sm6337edit-full-<version>.zip`** — the same, with all the files next to the exe (`riched20.dll`,
  `riched32.dll`, `toolbar.bmp`, `sm6337edit.hlp`/`.cnt`, the `sm6337-*.ttf` fonts, `OFL.txt`).
  If `riched20.dll` lies next to the exe, the program takes everything from its own folder and unpacks
  nothing — so you can swap any DLL, font, icons or help, and nothing is written to the temp folder.

Report problems through [Issues](../../issues/new/choose).

## Building

### Locally

You need the `i686-w64-mingw32` toolchain (msvcrt, win32-threads, **not** UCRT), for example
[niXman/mingw-builds-binaries](https://github.com/niXman/mingw-builds-binaries). Unpack it into
`toolchain/mingw32/`, then:

```
make
```

The result is a single file, `build/sm6337edit.exe` (about 800 KB): icons, RichEdit, fonts and help are
embedded into it as resources (`src/app.rc`), nothing else has to be copied. `make full` additionally builds
the `build-full/` folder — the variant with all the files next to the exe.

### GitHub Actions

On every push to `master` and every pull request the repository builds `sm6337edit.exe` with the same
toolchain (`.github/workflows/build.yml`) and puts it into the Artifacts of that run on the Actions
tab, so you can download a ready file without a local toolchain.

## Repository layout

- `src/` — sources (`main.c`, `printer.c`/`printer.h` — printing and code pages, `preview.c`/`preview.h` —
  print preview, `resource.h`)
- `help/` — help sources (RTF, project and contents); how to compile them is in `help/README.md`
- `redist/` — what gets embedded into the exe: `riched20.dll` / `riched32.dll` (RichEdit from Win98 SE),
  `icons/toolbar.bmp`, the compiled help `sm6337edit.hlp` and `fonts/` — the pitch 10/12/17 fonts
  (derived from Noto Sans Mono, OFL license)
- `src/app.rc` — the list of embedded resources
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

## Third-party components

- **`redist/riched20.dll`, `redist/riched32.dll`** — RichEdit libraries (Richedit 2.0, © Microsoft
  Corporation, 1997). They are **not** covered by this project's MIT license and are not the author's:
  they sit in the repository and are embedded in the exe only because the program will not start on a
  bare Windows 98 SE without them. The rights belong to Microsoft; if you are the rights holder and
  object, open an Issue and they will be removed. To build without them, take your own copies from a
  Windows 98 or Office distribution and put them in `redist/`; at startup the program first tries the
  system `riched20.dll`.
- **`redist/fonts/*.ttf`** — derivatives of Noto Sans Mono, SIL OFL 1.1 (`redist/fonts/OFL.txt`).
- **`rom/`** and `СМ6337 РЭ.djvu` — a dump of the printer's ROM and a scan of its user manual, kept as
  reference material; the program does not use them.

Status: **beta**. Printing has been checked on a real СМ6337, but the program was mostly written without
running it on Windows 98 SE — bugs are possible. Please report them in Issues.

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
