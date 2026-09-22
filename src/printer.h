#ifndef PRINTER_H
#define PRINTER_H
#include <windows.h>

/* wsprintf ненадёжен на настоящей Windows 9x (наблюдался обрыв строки сразу
   после %s) — поэтому нигде не используем форматирование из user32.dll,
   собираем текст вручную через strcpy/strcat + это. */
static void UIntToStrA(unsigned long v, char *out)
{
    char tmp[12]; int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

typedef enum { SM_PAGEMODE_SOFTWARE = 0, SM_PAGEMODE_PRINTER = 1 } Sm6337PageMode;

typedef struct {
    int codeTable;       /* 14 = обычная таблица ПЭВМ, 15 = доп.таблица ПЭВМ (CP866-совместимая) */
    BOOL avoidFormFeed;  /* TRUE = не слать аппаратный ПФ (0x0C) — иначе на A4 будет АВР */
    Sm6337PageMode pageMode; /* кто ограничивает размер страницы: редактор (software) или принтер (ESC Q/ESC C) */
    int cols;  /* символов в строке — подбирается пользователем под свой принтер/бумагу */
    int lines; /* строк на странице */
    int feedLinesAfterPrint; /* сколько пустых строк (CR LF) слать вместо ПФ, 0-9 */
    BOOL unidirectional; /* TRUE = однонаправленная печать (ESC U 1) — медленнее, но без смещения строк */
    int lineSpacing; /* ESC A n, шаг 0.353мм, 0-85; 0 = не слать, стандартный интервал принтера */
    int leftMargin;  /* ESC l n, левое поле в символах, 0-255; 0 = не слать */
} Sm6337Settings;

/* Кодирует один символ (в CP1251, как из ANSI RichEdit) в байт под выбранную
   таблицу ПЭВМ (14 или 15). Возвращает FALSE, если символа нет в таблице
   (тогда outByte = '?'). */
BOOL Sm6337_EncodeChar(unsigned char cp1251Byte, int codeTable, unsigned char *outByte);

/* Пишет уже готовый (собранный вызывающим) поток байт напрямую на LPT1 (без драйвера).
   Возвращает TRUE при успехе, иначе errMsg заполняется описанием ошибки. */
BOOL Sm6337_PrintRaw(const unsigned char *bytes, DWORD len, char *errMsg, int errMsgSize);

#endif
