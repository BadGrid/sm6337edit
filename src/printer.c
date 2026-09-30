/* Кодировка и raw-печать на LPT1 для СМ6337. См. printer_commands.md за деталями команд/таблиц.
   Текст приходит из ANSI RichEdit-контрола в кодировке Windows-1251 (CP1251). */
#include "printer.h"
#include "resource.h"
#include <string.h>

/* CP1251 -> Unicode codepoint для кириллического диапазона (остальное — ASCII один-в-один). */
static unsigned short Cp1251ToUnicode(unsigned char b)
{
    if (b < 0x80) return b;
    /* Типографские знаки, которых нет в таблицах принтера (см. ниже), заменяем близкими
       ASCII-знаками, а не «?»: кавычки-ёлочки, длинное и среднее тире, «№», многоточие, маркер. */
    switch (b) {
    case 0x91: case 0x92: return '\'';            /* ‘ ’ */
    case 0x93: case 0x94: case 0xAB: case 0xBB: return '"'; /* “ ” « » */
    case 0x96: case 0x97: return '-';             /* – — */
    case 0x85: return '.';                        /* … */
    case 0x95: return '*';                        /* • */
    case 0xA0: return ' ';                        /* неразрывный пробел */
    case 0xB9: return '#';                        /* № */
    case 0xA9: return 'c';                        /* © */
    case 0xAE: return 'R';                        /* ® */
    }
    if (b == 0xA8) return 0x0401; /* Ё */
    if (b == 0xB8) return 0x0451; /* ё */
    if (b >= 0xC0 && b <= 0xDF) return (unsigned short)(0x0410 + (b - 0xC0)); /* А-Я */
    if (b >= 0xE0 && b <= 0xFF) return (unsigned short)(0x0430 + (b - 0xE0)); /* а-я */
    return '?';
}

BOOL Sm6337_EncodeChar(unsigned char cp1251Byte, int codeTable, unsigned char *outByte)
{
    unsigned short ch = Cp1251ToUnicode(cp1251Byte);

    if (ch < 0x80) { *outByte = (unsigned char)ch; return TRUE; }

    if (ch == 0x0401) { *outByte = 0xF0; return TRUE; } /* Ё */
    if (ch == 0x0451) { *outByte = 0xF1; return TRUE; } /* ё */

    if (codeTable == 15) {
        /* CP866-совместимая раскладка (рис.7.9) */
        if (ch >= 0x0410 && ch <= 0x042F) { *outByte = (unsigned char)(0x80 + (ch - 0x0410)); return TRUE; } /* А-Я */
        if (ch >= 0x0430 && ch <= 0x043F) { *outByte = (unsigned char)(0xA0 + (ch - 0x0430)); return TRUE; } /* а-п */
        if (ch >= 0x0440 && ch <= 0x044F) { *outByte = (unsigned char)(0xE0 + (ch - 0x0440)); return TRUE; } /* р-я */
    } else {
        /* Обычная таблица ПЭВМ, n=14 (рис.7.8) — буквы сдвинуты на 0xB0 */
        if (ch >= 0x0410 && ch <= 0x041F) { *outByte = (unsigned char)(0xB0 + (ch - 0x0410)); return TRUE; } /* А-П */
        if (ch >= 0x0420 && ch <= 0x042F) { *outByte = (unsigned char)(0xC0 + (ch - 0x0420)); return TRUE; } /* Р-Я */
        if (ch >= 0x0430 && ch <= 0x043F) { *outByte = (unsigned char)(0xD0 + (ch - 0x0430)); return TRUE; } /* а-п */
        if (ch >= 0x0440 && ch <= 0x044F) { *outByte = (unsigned char)(0xE0 + (ch - 0x0440)); return TRUE; } /* р-я */
    }

    *outByte = '?';
    return FALSE;
}

BOOL Sm6337_PrintRaw(const unsigned char *bytes, DWORD len, char *errMsg, int errMsgSize)
{
    (void)errMsgSize;
    HANDLE hPort = CreateFileA("LPT1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE) {
        char code[12]; UIntToStrA(GetLastError(), code);
        strcpy(errMsg, "Не удалось открыть LPT1 (код ошибки ");
        strcat(errMsg, code);
        strcat(errMsg, "). Принтер подключён и включён?");
        return FALSE;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hPort, bytes, len, &written, NULL);
    if (!ok || written != len) {
        char code[12]; UIntToStrA(GetLastError(), code);
        strcpy(errMsg, "Ошибка записи в LPT1 (код ");
        strcat(errMsg, code);
        strcat(errMsg, ").");
        ok = FALSE;
    }

    CloseHandle(hPort);
    return ok;
}

/* Полный путь к файлу рядом с exe; TRUE, если такой файл существует. */
BOOL Sm6337_FileBesideExe(const char *name, char *outPath, int outSize)
{
    char *sl;
    if (outSize < MAX_PATH) return FALSE;
    if (GetModuleFileNameA(NULL, outPath, (DWORD)outSize) == 0) return FALSE;
    sl = strrchr(outPath, '\\');
    if (!sl) return FALSE;
    *(sl + 1) = 0;
    if (strlen(outPath) + strlen(name) + 1 > (size_t)outSize) return FALSE;
    strcat(outPath, name);
    return GetFileAttributesA(outPath) != 0xFFFFFFFF;
}

HBITMAP Sm6337_LoadToolbarStrip(void)
{
    char path[MAX_PATH];
    HBITMAP h = NULL;
    if (Sm6337_FileBesideExe("toolbar.bmp", path, MAX_PATH))
        h = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    if (!h)
        h = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDB_TOOLBAR), IMAGE_BITMAP, 0, 0, 0);
    return h;
}
