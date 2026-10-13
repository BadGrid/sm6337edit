/* SM6337 Edit — WordPad-подобный редактор с прямой печатью на LPT1 (без драйвера).
   ANSI-сборка (CP1251) — на настоящей Windows 9x/98 Unicode (*W) API поддержан
   не полностью (подтверждено: GetModuleFileNameW, LoadLibraryW, wsprintfW не
   работают), поэтому используем классический ANSI-путь, как обычные русские
   программы эпохи Win95/98. */
#define _WIN32_WINNT 0x0400
#define WINVER 0x0400
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <richedit.h>
#include <string.h>
#include <stdlib.h>
#include "resource.h"
#include "printer.h"
#include "preview.h"
#include "version.h"

static HWND g_hEdit;
static HWND g_hStatus;
static HMODULE g_hRichEdit;
static HFONT g_hFont;
static const char *g_richEditClass = "RichEdit20A";
static int g_codeTable = 15; /* доп.таблица ПЭВМ — CP866-совместимая, дефолт */
static BOOL g_avoidFormFeed = TRUE; /* не слать ПФ — дефолт, чтобы не улетать в АВР на A4 */
static Sm6337PageMode g_pageMode = SM_PAGEMODE_SOFTWARE; /* дефолт безопаснее — не зависит от регистра принтера */
static int g_cols = 82;  /* символов в строке — подбирается пользователем под свой принтер/бумагу */
static int g_lines = 70; /* строк на странице */
static int g_feedLinesAfterPrint = 2;
static BOOL g_unidirectional = FALSE; /* качество печати: FALSE=быстрее(двунаправленная), TRUE=точнее */
static int g_lineSpacing = 0; /* ESC A n, шаг 0.353мм; 0 = стандартный интервал принтера */
static int g_leftMargin = 0;  /* ESC l n, левое поле в символах; 0 = без поля */
/* Альбомная ориентация: бумага боком. g_cols/g_lines хранят размеры ТЕКУЩЕЙ ориентации; при переключении
   в окне настройки они пересчитываются пропорционально сторонам A4 (297 x 210 мм). */
static BOOL g_landscape = FALSE;
static BOOL g_printFileName = FALSE; /* печатать имя файла по центру первой строки листа */
static HWND g_hToolbar;
static char g_iniPath[MAX_PATH] = "";
static char g_helpPath[MAX_PATH] = ""; /* sm6337edit.hlp рядом с exe */
static char g_curFile[MAX_PATH] = "";

static BOOL g_dlgDone;
static BOOL g_dlgLandscape; /* ориентация, которой сейчас соответствуют поля размера в окне настройки */
static BOOL g_runawayResult;
static HWND g_hRunawayNo;

static void LoadSettings(void)
{
    g_codeTable = (int)GetPrivateProfileIntA("Settings", "CodeTable", 15, g_iniPath);
    if (g_codeTable != 14 && g_codeTable != 15) g_codeTable = 15;
    g_avoidFormFeed = GetPrivateProfileIntA("Settings", "AvoidFormFeed", 1, g_iniPath) ? TRUE : FALSE;
    g_cols = (int)GetPrivateProfileIntA("Settings", "Cols", 82, g_iniPath);
    g_lines = (int)GetPrivateProfileIntA("Settings", "Lines", 70, g_iniPath);
    if (g_cols < 1 || g_cols > 255) g_cols = 82;
    if (g_lines < 1 || g_lines > 255) g_lines = 70;
    g_pageMode = (Sm6337PageMode)GetPrivateProfileIntA("Settings", "PageMode", SM_PAGEMODE_SOFTWARE, g_iniPath);
    g_feedLinesAfterPrint = (int)GetPrivateProfileIntA("Settings", "FeedLines", 2, g_iniPath);
    if (g_feedLinesAfterPrint < 0 || g_feedLinesAfterPrint > 9) g_feedLinesAfterPrint = 2;
    g_unidirectional = GetPrivateProfileIntA("Settings", "Unidirectional", 0, g_iniPath) ? TRUE : FALSE;
    g_lineSpacing = (int)GetPrivateProfileIntA("Settings", "LineSpacing", 0, g_iniPath);
    if (g_lineSpacing < 0 || g_lineSpacing > 85) g_lineSpacing = 0;
    g_leftMargin = (int)GetPrivateProfileIntA("Settings", "LeftMargin", 0, g_iniPath);
    if (g_leftMargin < 0 || g_leftMargin > 255) g_leftMargin = 0;
    g_landscape = GetPrivateProfileIntA("Settings", "Landscape", 0, g_iniPath) ? TRUE : FALSE;
    g_printFileName = GetPrivateProfileIntA("Settings", "PrintFileName", 0, g_iniPath) ? TRUE : FALSE;
}

/* Пересчёт размера страницы при смене ориентации: ширина и высота A4 — 297 и 210 мм, так что в
   альбомной строка длиннее (cols x 297/210), а строк меньше (lines x 210/297). Округление целое. */
static void ConvertPageSize(BOOL toLandscape, int *cols, int *lines)
{
    long c = *cols, l = *lines;
    if (toLandscape) { c = (c * 297 + 105) / 210; l = (l * 210 + 148) / 297; }
    else             { c = (c * 210 + 148) / 297; l = (l * 297 + 105) / 210; }
    if (c < 1) c = 1;
    if (c > 255) c = 255;
    if (l < 1) l = 1;
    if (l > 255) l = 255;
    *cols = (int)c; *lines = (int)l;
}

static void SaveSettings(void)
{
    char v[12];
    UIntToStrA((unsigned long)g_codeTable, v); WritePrivateProfileStringA("Settings", "CodeTable", v, g_iniPath);
    UIntToStrA((unsigned long)(g_avoidFormFeed ? 1 : 0), v); WritePrivateProfileStringA("Settings", "AvoidFormFeed", v, g_iniPath);
    UIntToStrA((unsigned long)g_cols, v); WritePrivateProfileStringA("Settings", "Cols", v, g_iniPath);
    UIntToStrA((unsigned long)g_lines, v); WritePrivateProfileStringA("Settings", "Lines", v, g_iniPath);
    UIntToStrA((unsigned long)g_pageMode, v); WritePrivateProfileStringA("Settings", "PageMode", v, g_iniPath);
    UIntToStrA((unsigned long)g_feedLinesAfterPrint, v); WritePrivateProfileStringA("Settings", "FeedLines", v, g_iniPath);
    UIntToStrA((unsigned long)(g_unidirectional ? 1 : 0), v); WritePrivateProfileStringA("Settings", "Unidirectional", v, g_iniPath);
    UIntToStrA((unsigned long)g_lineSpacing, v); WritePrivateProfileStringA("Settings", "LineSpacing", v, g_iniPath);
    UIntToStrA((unsigned long)g_leftMargin, v); WritePrivateProfileStringA("Settings", "LeftMargin", v, g_iniPath);
    UIntToStrA((unsigned long)(g_landscape ? 1 : 0), v); WritePrivateProfileStringA("Settings", "Landscape", v, g_iniPath);
    UIntToStrA((unsigned long)(g_printFileName ? 1 : 0), v); WritePrivateProfileStringA("Settings", "PrintFileName", v, g_iniPath);
}

/* n * 0.353мм, без float/sprintf — целыми, в сотых мм. */
static void FormatSpacingMm(int n, char *out)
{
    if (n <= 0) { strcpy(out, "0 = стандартный принтера (~4.23 мм)"); return; }
    unsigned long hundredths = ((unsigned long)n * 353 + 5) / 10; /* мм*100, округлено */
    unsigned long intPart = hundredths / 100, fracPart = hundredths % 100;
    char tmp[12];
    strcpy(out, "= ");
    UIntToStrA(intPart, tmp); strcat(out, tmp);
    strcat(out, ".");
    UIntToStrA(fracPart, tmp);
    if (fracPart < 10) strcat(out, "0");
    strcat(out, tmp);
    strcat(out, " мм");
}

static LRESULT CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EDIT_LINESPACING && HIWORD(wParam) == EN_CHANGE) {
            BOOL translated = FALSE;
            int v = (int)GetDlgItemInt(hDlg, IDC_EDIT_LINESPACING, &translated, FALSE);
            char mm[48];
            FormatSpacingMm(translated ? v : 0, mm);
            SetDlgItemTextA(hDlg, IDC_STATIC_LINESPACING_MM, mm);
            return 0;
        }
        if (LOWORD(wParam) == IDC_CHK_LANDSCAPE && HIWORD(wParam) == BN_CLICKED) {
            /* смена ориентации: сразу пересчитать «Символов в строке» и «Строк на странице» */
            BOOL want = (IsDlgButtonChecked(hDlg, IDC_CHK_LANDSCAPE) == BST_CHECKED);
            if (want != g_dlgLandscape) {
                BOOL t1 = FALSE, t2 = FALSE;
                int c = (int)GetDlgItemInt(hDlg, IDC_EDIT_COLS, &t1, FALSE);
                int l = (int)GetDlgItemInt(hDlg, IDC_EDIT_LINES, &t2, FALSE);
                if (!t1 || c < 1 || c > 255) c = g_dlgLandscape ? 116 : 82;
                if (!t2 || l < 1 || l > 255) l = g_dlgLandscape ? 49 : 70;
                ConvertPageSize(want, &c, &l);
                SetDlgItemInt(hDlg, IDC_EDIT_COLS, (UINT)c, FALSE);
                SetDlgItemInt(hDlg, IDC_EDIT_LINES, (UINT)l, FALSE);
                g_dlgLandscape = want;
            }
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDOK:
            g_landscape = g_dlgLandscape;
            g_codeTable = (IsDlgButtonChecked(hDlg, IDC_RADIO_T15) == BST_CHECKED) ? 15 : 14;
            g_avoidFormFeed = (IsDlgButtonChecked(hDlg, IDC_CHK_NOFF) == BST_CHECKED);
            g_pageMode = (IsDlgButtonChecked(hDlg, IDC_RADIO_MODE_PRINTER) == BST_CHECKED)
                      ? SM_PAGEMODE_PRINTER : SM_PAGEMODE_SOFTWARE;
            {
                BOOL translated = FALSE;
                int v = (int)GetDlgItemInt(hDlg, IDC_EDIT_FEEDLINES, &translated, FALSE);
                if (translated && v >= 0 && v <= 9) g_feedLinesAfterPrint = v;
                v = (int)GetDlgItemInt(hDlg, IDC_EDIT_COLS, &translated, FALSE);
                if (translated && v >= 1 && v <= 255) g_cols = v;
                v = (int)GetDlgItemInt(hDlg, IDC_EDIT_LINES, &translated, FALSE);
                if (translated && v >= 1 && v <= 255) g_lines = v;
                v = (int)GetDlgItemInt(hDlg, IDC_EDIT_LINESPACING, &translated, FALSE);
                if (translated && v >= 0 && v <= 85) g_lineSpacing = v;
                v = (int)GetDlgItemInt(hDlg, IDC_EDIT_LEFTMARGIN, &translated, FALSE);
                if (translated && v >= 0 && v <= 255) g_leftMargin = v;
            }
            g_unidirectional = (IsDlgButtonChecked(hDlg, IDC_CHK_UNIDIR) == BST_CHECKED);
            g_printFileName = (IsDlgButtonChecked(hDlg, IDC_CHK_FILENAME) == BST_CHECKED);
            SaveSettings();
            DestroyWindow(hDlg);
            return 0;
        case IDCANCEL:
            DestroyWindow(hDlg);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        g_dlgDone = TRUE;
        return 0;
    }
    return DefWindowProcA(hDlg, msg, wParam, lParam);
}

static void ShowSm6337SettingsDialog(HWND owner)
{
    static BOOL classRegistered = FALSE;
    HINSTANCE hInst = GetModuleHandleA(NULL);

    if (!classRegistered) {
        WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = SettingsDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "Sm6337SettingsWndClass";
        RegisterClassA(&wc);
        classRegistered = TRUE;
    }

    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int w = 580, h = 560;
    if (h > wa.bottom - wa.top - 20) h = wa.bottom - wa.top - 20; /* не выше рабочей области */
    RECT ownerRect; GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "Sm6337SettingsWndClass",
        "Настройка СМ6337",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, hInst, NULL);

    CreateWindowA("BUTTON", "Кодовая таблица",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 8, 545, 76, hDlg, NULL, hInst, NULL);
    CreateWindowA("BUTTON", "Обычная таблица ПЭВМ (n=14)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_MULTILINE | WS_GROUP | WS_TABSTOP,
        25, 28, 520, 22, hDlg, (HMENU)IDC_RADIO_T14, hInst, NULL);
    CreateWindowA("BUTTON", "Доп. таблица ПЭВМ — CP866 (n=15, рекомендуется)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_MULTILINE,
        25, 54, 520, 22, hDlg, (HMENU)IDC_RADIO_T15, hInst, NULL);

    CreateWindowA("BUTTON", "Лимиты страницы (свои значения под конкретный принтер/бумагу)",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 92, 545, 108, hDlg, NULL, hInst, NULL);
    CreateWindowA("STATIC", "Символов в строке:",
        WS_CHILD | WS_VISIBLE,
        25, 114, 150, 20, hDlg, NULL, hInst, NULL);
    CreateWindowA("EDIT", "82",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
        180, 112, 60, 22, hDlg, (HMENU)IDC_EDIT_COLS, hInst, NULL);
    CreateWindowA("STATIC", "Строк на странице:",
        WS_CHILD | WS_VISIBLE,
        260, 114, 150, 20, hDlg, NULL, hInst, NULL);
    CreateWindowA("EDIT", "70",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
        410, 112, 55, 22, hDlg, (HMENU)IDC_EDIT_LINES, hInst, NULL);
    CreateWindowA("STATIC", "Левое поле (символов, 0 = нет):",
        WS_CHILD | WS_VISIBLE,
        25, 150, 220, 20, hDlg, NULL, hInst, NULL);
    CreateWindowA("EDIT", "0",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
        250, 148, 60, 22, hDlg, (HMENU)IDC_EDIT_LEFTMARGIN, hInst, NULL);
    CreateWindowA("BUTTON", "Альбомная ориентация (бумага боком)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        25, 172, 520, 22, hDlg, (HMENU)IDC_CHK_LANDSCAPE, hInst, NULL);

    CreateWindowA("BUTTON", "Ограничение размера страницы",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 208, 545, 92, hDlg, NULL, hInst, NULL);
    CreateWindowA("BUTTON", "Программный — перенос строк в редакторе (безопаснее)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_MULTILINE | WS_GROUP | WS_TABSTOP,
        25, 228, 520, 24, hDlg, (HMENU)IDC_RADIO_MODE_SOFTWARE, hInst, NULL);
    CreateWindowA("BUTTON", "Принтерный — команды ESC Q/ESC C",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_MULTILINE,
        25, 258, 520, 34, hDlg, (HMENU)IDC_RADIO_MODE_PRINTER, hInst, NULL);

    CreateWindowA("BUTTON", "Не слать аппаратный ПФ (форм-фид) — иначе на A4 будет АВР",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP,
        10, 306, 545, 36, hDlg, (HMENU)IDC_CHK_NOFF, hInst, NULL);

    CreateWindowA("STATIC", "Пустых строк после печати (0-9):",
        WS_CHILD | WS_VISIBLE,
        10, 352, 300, 20, hDlg, NULL, hInst, NULL);
    CreateWindowA("EDIT", "2",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
        320, 350, 50, 22, hDlg, (HMENU)IDC_EDIT_FEEDLINES, hInst, NULL);

    CreateWindowA("STATIC", "Интервал строк (n x 0.353мм):",
        WS_CHILD | WS_VISIBLE,
        10, 386, 220, 22, hDlg, NULL, hInst, NULL);
    CreateWindowA("EDIT", "0",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
        235, 384, 50, 22, hDlg, (HMENU)IDC_EDIT_LINESPACING, hInst, NULL);
    CreateWindowA("STATIC", "0 = стандартный принтера (~4.23 мм)",
        WS_CHILD | WS_VISIBLE,
        290, 386, 265, 22, hDlg, (HMENU)IDC_STATIC_LINESPACING_MM, hInst, NULL);

    CreateWindowA("BUTTON", "Однонаправленная печать (медленнее, но без смещения строк)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP,
        10, 416, 545, 36, hDlg, (HMENU)IDC_CHK_UNIDIR, hInst, NULL);
    CreateWindowA("BUTTON", "Печатать имя файла по центру первой строки листа",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        10, 454, 545, 24, hDlg, (HMENU)IDC_CHK_FILENAME, hInst, NULL);

    CreateWindowA("BUTTON", "ОК",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        195, 488, 90, 28, hDlg, (HMENU)IDOK, hInst, NULL);
    CreateWindowA("BUTTON", "Отмена",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        295, 488, 90, 28, hDlg, (HMENU)IDCANCEL, hInst, NULL);

    CheckRadioButton(hDlg, IDC_RADIO_T14, IDC_RADIO_T15,
        (g_codeTable == 14) ? IDC_RADIO_T14 : IDC_RADIO_T15);
    CheckDlgButton(hDlg, IDC_CHK_NOFF, g_avoidFormFeed ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHK_LANDSCAPE, g_landscape ? BST_CHECKED : BST_UNCHECKED);
    g_dlgLandscape = g_landscape;
    CheckRadioButton(hDlg, IDC_RADIO_MODE_PRINTER, IDC_RADIO_MODE_SOFTWARE,
        (g_pageMode == SM_PAGEMODE_PRINTER) ? IDC_RADIO_MODE_PRINTER : IDC_RADIO_MODE_SOFTWARE);
    SetDlgItemInt(hDlg, IDC_EDIT_FEEDLINES, (UINT)g_feedLinesAfterPrint, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_COLS, (UINT)g_cols, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_LINES, (UINT)g_lines, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_LEFTMARGIN, (UINT)g_leftMargin, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_LINESPACING, (UINT)g_lineSpacing, FALSE);
    { char mm[48]; FormatSpacingMm(g_lineSpacing, mm); SetDlgItemTextA(hDlg, IDC_STATIC_LINESPACING_MM, mm); }
    CheckDlgButton(hDlg, IDC_CHK_UNIDIR, g_unidirectional ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHK_FILENAME, g_printFileName ? BST_CHECKED : BST_UNCHECKED);

    EnableWindow(owner, FALSE);
    g_dlgDone = FALSE;
    ShowWindow(hDlg, SW_SHOW);

    MSG msg;
    while (!g_dlgDone && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

/* "Нет" убегает от курсора — классический троллинг-приём. Возвращает TRUE,
   если поймали "Да" (пользователь сдался и продолжил), FALSE если умудрились
   таки поймать "Нет". */
static LRESULT CALLBACK RunawayDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_TIMER: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hDlg, &pt);
        RECT rc; GetWindowRect(g_hRunawayNo, &rc);
        POINT c; c.x = (rc.left + rc.right) / 2; c.y = (rc.top + rc.bottom) / 2;
        ScreenToClient(hDlg, &c);
        int dx = pt.x - c.x, dy = pt.y - c.y;
        if (dx * dx + dy * dy < 70 * 70) {
            RECT client; GetClientRect(hDlg, &client);
            int nx = rand() % (client.right - 110) + 5;
            int ny = rand() % (client.bottom - 80) + 40;
            MoveWindow(g_hRunawayNo, nx, ny, 100, 30, TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDYES) { g_runawayResult = TRUE; DestroyWindow(hDlg); }
        else if (LOWORD(wParam) == IDNO) { g_runawayResult = FALSE; DestroyWindow(hDlg); }
        return 0;
    case WM_CLOSE:
        MessageBoxA(hDlg, "Крестик не поможет, сука. Играй по правилам — жми кнопки.", "Не в этот раз", MB_OK | MB_ICONWARNING);
        return 0; /* WM_CLOSE игнорируем полностью — не закрываем */
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) {
            MessageBoxA(hDlg, "Alt+F4 тоже не работает. Кнопки, я сказал.", "Не в этот раз", MB_OK | MB_ICONWARNING);
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hDlg, 1);
        g_dlgDone = TRUE;
        return 0;
    }
    return DefWindowProcA(hDlg, msg, wParam, lParam);
}

static BOOL ShowRunawayDialog(HWND owner)
{
    static BOOL classRegistered = FALSE;
    HINSTANCE hInst = GetModuleHandleA(NULL);
    if (!classRegistered) {
        WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = RunawayDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "Sm6337RunawayWndClass";
        RegisterClassA(&wc);
        classRegistered = TRUE;
    }

    int w = 400, h = 220;
    RECT ownerRect; GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "Sm6337RunawayWndClass",
        "ПОСЛЕДНИЙ ШАНС, СУКА",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, hInst, NULL);

    CreateWindowA("STATIC", "Жми ДА чтобы продолжить. НЕТ можешь даже не пытаться поймать.",
        WS_CHILD | WS_VISIBLE, 20, 20, 360, 40, hDlg, NULL, hInst, NULL);
    CreateWindowA("BUTTON", "Да",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        60, 140, 100, 32, hDlg, (HMENU)IDYES, hInst, NULL);
    g_hRunawayNo = CreateWindowA("BUTTON", "Нет",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        240, 140, 100, 32, hDlg, (HMENU)IDNO, hInst, NULL);

    SetTimer(hDlg, 1, 25, NULL);
    g_runawayResult = TRUE;
    EnableWindow(owner, FALSE);
    g_dlgDone = FALSE;
    ShowWindow(hDlg, SW_SHOW);

    MSG msg;
    while (!g_dlgDone && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_runawayResult;
}

static BOOL CALLBACK TrashWindowTitleProc(HWND hwnd, LPARAM lParam)
{
    static const char *junk[] = {
        "БЛЯТЬ ВСЁ", "0x00000000", "ЭТО КОНЕЦ", "НЕ ЖМИ СЮДА", "СИСТЕМНАЯ ОШИБКА",
        "ПОМОГИТЕ", "..........", "ХУЙНЯ КАКАЯ-ТО", "УЖЕ ПОЗДНО"
    };
    (void)lParam;
    SetWindowTextA(hwnd, junk[rand() % (sizeof(junk) / sizeof(junk[0]))]);
    return TRUE;
}

/* Каскад развала системы перед вечным хардлоком (только настоящая Win9x):
   тик 0 — звук раньше картинки; 0-20 — "смэлт" экрана (BitBlt колонок вниз,
   изредка мусорная кисть); 20-40 — заголовки окон в мусор, курсор дёргается
   и зажимается ClipCursor, MessageBeep; тик 30 — рисуем поддельный BSOD;
   30-70 — сам BSOD гниёт (текст скремблится посимвольно, тоже смэлт);
   с тика 40 параллельно стартует утечка GDI/USER-хендлов и уже не
   останавливается — система сама доходит до хардлока под растёкшимся BSOD,
   отдельного кода перехода не нужно. Любая правка экрана — не чаще раза
   в тик (100мс) на одну и ту же область, никаких быстрых инверсий. */
static void RunDeathCascade(void)
{
    HDC hdc = GetDC(NULL);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    char bsod[3][80];
    strcpy(bsod[0], "*** STOP: 0x0000004E (0xDEADBEEF,0x00000000,0x00000000,0x00000000)");
    strcpy(bsod[1], "SM6337.SYS ЗАВИСЛА НАХУЙ");
    strcpy(bsod[2], "Нажми любую клавишу для перезагрузки... ага, щас.");
    int bsodTop = sh / 2 - 60;
    int tick;

    srand(GetTickCount());
    { int f; for (f = 800; f >= 200; f -= 40) Beep((DWORD)f, 15); } /* тик 0: звук раньше картинки */

    for (tick = 0; tick <= 70; tick++) {
        /* смэлт экрана — идёт всё время, включая фазу BSOD ниже */
        int colX = rand() % sw, colW = 20 + rand() % 60, shift = 3 + rand() % 10;
        BitBlt(hdc, colX, shift, colW, sh - shift, hdc, colX, 0, SRCCOPY);
        if (rand() % 3 == 0) {
            RECT rc; rc.left = rand() % sw; rc.top = rand() % sh;
            rc.right = rc.left + 20 + rand() % 80; rc.bottom = rc.top + 10 + rand() % 40;
            HBRUSH hb = CreateHatchBrush(rand() % 6, RGB(rand() % 256, rand() % 256, rand() % 256));
            FillRect(hdc, &rc, hb);
            DeleteObject(hb);
        }

        if (tick >= 20 && tick <= 40) {
            if (tick % 3 == 0) EnumWindows(TrashWindowTitleProc, 0);
            POINT cur; GetCursorPos(&cur);
            SetCursorPos(cur.x + (rand() % 40 - 20), cur.y + (rand() % 40 - 20));
            RECT clip; clip.left = 100; clip.top = 100;
            clip.right = sw - 100 - tick * 5; clip.bottom = sh - 100 - tick * 5;
            ClipCursor(&clip);
            if (tick % 4 == 0) MessageBeep(MB_ICONHAND);
        }

        if (tick == 30) {
            RECT full = {0, 0, sw, sh};
            HBRUSH blue = CreateSolidBrush(RGB(0, 0, 168));
            FillRect(hdc, &full, blue);
            DeleteObject(blue);
            HFONT hf = CreateFontA(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Fixedsys");
            HFONT oldf = (HFONT)SelectObject(hdc, hf);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(0, 0, 168));
            int li; for (li = 0; li < 3; li++) TextOutA(hdc, 80, bsodTop + li * 30, bsod[li], (int)strlen(bsod[li]));
            SelectObject(hdc, oldf);
            DeleteObject(hf);
        }

        if (tick > 30 && tick <= 70) {
            int li = rand() % 3;
            int pos = rand() % (int)strlen(bsod[li]);
            bsod[li][pos] = (char)(33 + rand() % 94); /* гниение — случайный символ */
            RECT lr; lr.left = 80; lr.top = bsodTop + li * 30 - 4; lr.right = 780; lr.bottom = lr.top + 24;
            HBRUSH blue2 = CreateSolidBrush(RGB(0, 0, 168));
            FillRect(hdc, &lr, blue2);
            DeleteObject(blue2);
            HFONT hf2 = CreateFontA(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Fixedsys");
            HFONT oldf2 = (HFONT)SelectObject(hdc, hf2);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(0, 0, 168));
            TextOutA(hdc, 80, bsodTop + li * 30, bsod[li], (int)strlen(bsod[li]));
            SelectObject(hdc, oldf2);
            DeleteObject(hf2);
        }

        if (tick >= 40) {
            /* старт утечки GDI/USER — с этого тика не глушим никогда */
            int k; for (k = 0; k < 20; k++) {
                CreateSolidBrush(RGB(rand() % 256, rand() % 256, rand() % 256));
                CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
            }
        }

        Sleep(100);
    }
    ReleaseDC(NULL, hdc);

    /* финал сам — без отдельного кода перехода, просто продолжаем жрать
       хендлы вечно, уже под растёкшимся BSOD. */
    for (;;) {
        CreateSolidBrush(RGB(rand() % 256, rand() % 256, rand() % 256));
        CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    }
}

static HMENU BuildMenu(void)
{
    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, IDM_FILE_NEW, "&Создать\tCtrl+N");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_OPEN, "&Открыть...\tCtrl+O");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVE, "&Сохранить\tCtrl+S");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVEAS, "Сохранить &как...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_PRINTPREVIEW, "Пред&просмотр печати...");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_PRINT, "&Печать\tCtrl+P");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_PRINTSETUP, "Параметры &печати (СМ6337)...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXIT, "В&ыход");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&Файл");

    HMENU hEdit = CreatePopupMenu();
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_UNDO, "&Отменить\tCtrl+Z");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_REDO, "&Повторить\tCtrl+Y");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_CUT, "&Вырезать\tCtrl+X");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_COPY, "&Копировать\tCtrl+C");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_PASTE, "&Вставить\tCtrl+V");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_SELECTALL, "Выделить &всё\tCtrl+A");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_FIND, "&Найти...\tCtrl+F");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_FINDNEXT, "Найти &далее\tF3");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hEdit, "&Правка");

    HMENU hFormat = CreatePopupMenu();
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_FONT, "&Цвет текста...");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_BOLD, "&Жирный\tCtrl+B");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_UNDERLINE, "П&одчёркнутый\tCtrl+U");
    AppendMenuA(hFormat, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_PITCH10, "Шаг печати &10 (обычный, Pica)");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_PITCH12, "Шаг печати &12 (убористый, Elite)");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_PITCH17, "Шаг печати &17 (сжатый, ещё уже)");
    AppendMenuA(hFormat, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_DOUBLESTRIKE, "&Двойная печать (на экране — зачёркнутый)");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_EXPANDED, "&Расширенный (на экране крупнее)");
    AppendMenuA(hFormat, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_SUPERSCRIPT, "Верхний &индекс");
    AppendMenuA(hFormat, MF_STRING, IDM_FORMAT_SUBSCRIPT, "Нижний инде&кс");
    AppendMenuA(hFormat, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_LEFT, "По &левому краю\tCtrl+L");
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_CENTER, "По &центру\tCtrl+E");
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_RIGHT, "По &правому краю\tCtrl+R");
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_JUSTIFY, "По &ширине\tCtrl+J");
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_INDENT, "&Увеличить отступ слева");
    AppendMenuA(hFormat, MF_STRING, IDM_PARA_OUTDENT, "У&меньшить отступ слева");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFormat, "Фо&рмат");

    HMENU hView = CreatePopupMenu();
    AppendMenuA(hView, MF_STRING, IDM_VIEW_TOOLBAR, "Панель &инструментов");
    AppendMenuA(hView, MF_STRING, IDM_VIEW_STATUSBAR, "&Строка состояния");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "&Вид");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuA(hHelp, MF_STRING, IDM_HELP_CONTENTS, "&Содержание\tF1");
    AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "О &программе...");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Справка");

    return hMenu;
}

/* Шаг печати определяем по шрифту на экране (см. обработчики PITCH и EXPANDED) —
   CFM_SPACING (межбуквенный интервал) молча игнорируется древним RichEdit 2.0 из
   Win98 SE, а CFM_FACE/CFM_SIZE — база ещё из RichEdit 1.0, работает железно.
   "Small Fonts"/"Terminal" — штатные узкие РАСТРОВЫЕ шрифты Windows: у растровых
   шрифтов всего пара зашитых размеров, поэтому 12/17 различаем не размером одного
   шрифта (округляются до одного растра — были одинаковые), а двумя разными шрифтами.
   cpi нужен, чтобы правильно считать, сколько символов физически влезает в строку
   (g_cols откалиброван для Pica 10cpi — при более узком шаге в ту же строку влезает больше). */
/* Шрифты шага печати. Если рядом с exe лежат три шрифта sm6337-10/12/17.ttf (одно
   семейство Noto Sans Mono с разной шириной знака при одной высоте), берём их — тогда
   при смене шага буквы меняются только по ширине. Если файлов нет, откат на старую
   схему из системных шрифтов (Courier New / Terminal / Small Fonts). */
#define SM_FONT_OWN_NORMAL "SM6337 Mono 10"
#define SM_FONT_OWN_ELITE  "SM6337 Mono 12"
#define SM_FONT_OWN_COND   "SM6337 Mono 17"
#define SM_FONT_OWN_WIDE   "SM6337 Mono Wide"
static const char *g_faceNormal = "Courier New";
static const char *g_faceElite = "Terminal";
static const char *g_faceCond = "Small Fonts";
static const char *g_faceWide = ""; /* пусто, пока свои шрифты не подключены */
static const char *g_fontFiles[4] = { "sm6337-10.ttf", "sm6337-12.ttf", "sm6337-17.ttf", "sm6337-w.ttf" };
static char g_fontPaths[4][MAX_PATH];
static int g_fontsLoaded = 0;

#define SM_PITCH_FACE_ELITE     g_faceElite
#define SM_PITCH_FACE_CONDENSED g_faceCond
#define SM_PITCH_FACE_NORMAL    g_faceNormal

/* Подключает шрифты из папки exe на время работы программы. Всё или ничего: если хоть
   один не подключился, снимаем уже подключённые и остаёмся на системных шрифтах. */
static void LoadPitchFonts(const char *exeDir)
{
    int i, ok = 0;
    for (i = 0; i < 4; i++) {
        strcpy(g_fontPaths[i], exeDir);
        strcat(g_fontPaths[i], g_fontFiles[i]);
        if (GetFileAttributesA(g_fontPaths[i]) != 0xFFFFFFFF && AddFontResourceA(g_fontPaths[i]) > 0) ok++;
        else break;
    }
    if (ok == 4) {
        g_faceNormal = SM_FONT_OWN_NORMAL;
        g_faceElite = SM_FONT_OWN_ELITE;
        g_faceCond = SM_FONT_OWN_COND;
        g_faceWide = SM_FONT_OWN_WIDE;
        g_fontsLoaded = 4;
        SendMessageA(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    } else {
        for (i = 0; i < ok; i++) RemoveFontResourceA(g_fontPaths[i]);
    }
}

static void UnloadPitchFonts(void)
{
    int i;
    for (i = 0; i < g_fontsLoaded; i++) RemoveFontResourceA(g_fontPaths[i]);
    if (g_fontsLoaded) SendMessageA(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    g_fontsLoaded = 0;
}

/* Старая схема «расширенного» без своих шрифтов: тот же шрифт, но кегль 14 pt (280). Признак узкий —
   именно 280 и именно основной шрифт, — чтобы обычный документ с крупным заголовком (любой другой
   размер или шрифт) не печатался расширенным. */
static BOOL IsLegacyExpanded(const char *face, long yHeight)
{
    return yHeight == 280 && (strcmp(face, "Courier New") == 0 || strcmp(face, g_faceNormal) == 0);
}

static double PitchCpiFromFormat(const char *face, long yHeight, BOOL *outElite, BOOL *outCondensed, BOOL *outExpanded)
{
    *outElite = FALSE; *outCondensed = FALSE; *outExpanded = FALSE;
    /* старые названия оставляем распознаваемыми: в .rtf, сохранённых до перехода на
       свои шрифты, шаг записан именно ими */
    /* Названия своих шрифтов узнаём всегда, а не только когда файлы подключены: .rtf сохранён с
       «SM6337 Mono 12», а открыт там, где шрифтов нет (другая машина, не подхватились) — шаг при
       печати всё равно должен быть тот, что выбран в документе. */
    if (strcmp(face, SM_PITCH_FACE_ELITE) == 0 || strcmp(face, SM_FONT_OWN_ELITE) == 0 || strcmp(face, "Terminal") == 0)     { *outElite = TRUE; return 12.0; }
    if (strcmp(face, SM_PITCH_FACE_CONDENSED) == 0 || strcmp(face, SM_FONT_OWN_COND) == 0 || strcmp(face, "Small Fonts") == 0) { *outCondensed = TRUE; return 17.16; }
    /* Pica+Expanded — вдвое шире: свой широкий шрифт, а без своих шрифтов — крупный кегль */
    if ((g_faceWide[0] && strcmp(face, g_faceWide) == 0) || strcmp(face, SM_FONT_OWN_WIDE) == 0 || IsLegacyExpanded(face, yHeight)) { *outExpanded = TRUE; return 5.0; }
    return 10.0;
}

/* Состояние начертания в потоке ESC/P: что сейчас включено у принтера (cur) и чего хочет знак (want). */
typedef struct { unsigned char bold, under, dstrike, elite, cond, expd, idx; } PrAttr; /* idx: 0 обычный, 1 верхний, 2 нижний */

/* Один знак текста вместе с форматом; tab — табуляция (ширину получит при раскладке), brk — мягкий
   перенос строки (Shift+Enter). w — ширина в «колонках пики» (10 знаков на дюйм = 1.0). */
typedef struct { unsigned char ch; PrAttr a; double w; BOOL tab; BOOL brk; } PCell;

/* Формат абзаца в колонках пики от левого поля: отступы, выравнивание, табы. */
typedef struct {
    int align;                 /* PFA_LEFT / PFA_RIGHT / PFA_CENTER / PFA_JUSTIFY */
    double first, rest, right; /* отступ первой строки, остальных строк, справа */
    double width;              /* ширина области текста (до правой границы за вычетом отступа справа) */
    int nTabs; double tabPos[MAX_TAB_STOPS]; int tabType[MAX_TAB_STOPS]; /* 0 левый, 1 по центру, 2 правый */
} ParaFmt;

typedef struct { unsigned char *p; DWORD n, cap; BOOL fail; } PrintBuf;
typedef struct { PrintBuf b; PrAttr cur; double colF; int lineCount; double startCol; int codeTable; } PrCtx;

static void PbPut(PrintBuf *b, unsigned char c)
{
    if (b->fail) return;
    if (b->n >= b->cap) {
        DWORD nc = b->cap * 2;
        unsigned char *np = (unsigned char *)HeapReAlloc(GetProcessHeap(), 0, b->p, nc);
        if (!np) { b->fail = TRUE; return; }
        b->p = np; b->cap = nc;
    }
    b->p[b->n++] = c;
}

/* Шлёт ESC-команды только для того, что изменилось (порядок тот же, что и раньше). */
static void PbAttrs(PrintBuf *b, PrAttr *cur, const PrAttr *w)
{
    if (w->elite != cur->elite) { PbPut(b, 0x1B); PbPut(b, w->elite ? 'M' : 'P'); cur->elite = w->elite; }
    if (w->cond != cur->cond) { PbPut(b, w->cond ? 0x0F : 0x12); cur->cond = w->cond; } /* SI вкл / DC2 выкл */
    if (w->bold != cur->bold) { PbPut(b, 0x1B); PbPut(b, w->bold ? 'E' : 'F'); cur->bold = w->bold; }
    if (w->under != cur->under) { PbPut(b, 0x1B); PbPut(b, '-'); PbPut(b, w->under ? 1 : 0); cur->under = w->under; }
    if (w->dstrike != cur->dstrike) { PbPut(b, 0x1B); PbPut(b, w->dstrike ? 'G' : 'H'); cur->dstrike = w->dstrike; }
    if (w->expd != cur->expd) { PbPut(b, 0x1B); PbPut(b, 'W'); PbPut(b, w->expd ? 1 : 0); cur->expd = w->expd; }
    if (w->idx != cur->idx) {
        if (w->idx == 0) { PbPut(b, 0x1B); PbPut(b, 'T'); }
        else { PbPut(b, 0x1B); PbPut(b, 'S'); PbPut(b, (unsigned char)(w->idx == 1 ? 0 : 1)); }
        cur->idx = w->idx;
    }
}

/* Выравнивание, отступы и табуляция печатаются пробелами: ESC l («левое поле») действует на весь
   лист и не умеет ни центрировать, ни прижимать вправо, а пробелы одинаково видны и на бумаге, и в
   предпросмотре. Пробелы — всегда обычным шагом и без подчёркивания (иначе отступ был бы подчёркнут). */
static void PrPad(PrCtx *c, double target)
{
    static const PrAttr off = {0, 0, 0, 0, 0, 0, 0};
    int cnt = (int)(target - c->colF + 0.5);
    if (cnt <= 0) return;
    PbAttrs(&c->b, &c->cur, &off);
    while (cnt-- > 0) { PbPut(&c->b, ' '); c->colF += 1.0; }
}

static void PrEmit(PrCtx *c, const PCell *cell)
{
    unsigned char b;
    PbAttrs(&c->b, &c->cur, &cell->a);
    Sm6337_EncodeChar(cell->ch, c->codeTable, &b);
    PbPut(&c->b, b);
    c->colF += cell->w;
}

/* Куда ведёт табуляция из позиции x (колонки от левого поля). Заданные в абзаце стопы — по их типу
   (левый, по центру, правый: текст после табуляции до следующей табуляции кладётся так, чтобы
   закончиться/центрироваться на стопе); дальше последнего заданного — каждые 8 колонок. */
static double TabTarget(const ParaFmt *pf, double x, const PCell *c, int k, int m)
{
    int i, best = -1;
    double stop, segW = 0, target;
    for (i = 0; i < pf->nTabs; i++)
        if (pf->tabPos[i] > x + 0.001 && (best < 0 || pf->tabPos[i] < pf->tabPos[best])) best = i;
    if (best < 0) return (double)((int)(x / 8.0 + 0.001) + 1) * 8.0;
    stop = pf->tabPos[best];
    if (stop > pf->width) stop = pf->width; /* стоп правее границы строки — прижимаем к границе */
    for (i = k + 1; i < m && !c[i].tab && !c[i].brk; i++) segW += c[i].w;
    if (pf->tabType[best] == 1) target = stop - segW / 2.0;
    else if (pf->tabType[best] == 2) target = stop - segW;
    else target = stop;
    return target < x ? x : target;
}

static double MeasureLine(const ParaFmt *pf, const PCell *c, int a, int e, int m, double indent)
{
    double x = indent; int k;
    for (k = a; k < e; k++) x = c[k].tab ? TabTarget(pf, x, c, k, m) : x + c[k].w;
    return x;
}

/* Раскладывает один абзац: перенос по словам (в программном режиме), отступы, выравнивание
   (влево / по центру / вправо / по ширине), табуляция со стопами. */
static void LayoutParagraph(PrCtx *c, const ParaFmt *pf, PCell *cl, int m, BOOL software, BOOL *haveLine)
{
    const double wrapW = software ? pf->width : 1e9;
    int a = 0; BOOL firstLine = TRUE;
    for (;;) {
        double indent = firstLine ? pf->first : pf->rest, x;
        int k, b, e, lastBreak = -1; BOOL forced = FALSE, hasTab = FALSE, lastLine;
        if (!firstLine) while (a < m && !cl[a].tab && !cl[a].brk && cl[a].ch == ' ') a++; /* пробелы на стыке строк пропускаем */
        x = indent; b = m;
        for (k = a; k < m; k++) {
            if (cl[k].brk) { b = k + 1; forced = TRUE; break; }
            if (cl[k].tab) { x = TabTarget(pf, x, cl, k, m); hasTab = TRUE; lastBreak = k + 1; continue; }
            if (cl[k].ch == ' ') { x += cl[k].w; lastBreak = k + 1; continue; } /* пробел у границы не переносит */
            if (x + cl[k].w > wrapW + 0.001 && k > a) { b = (lastBreak > a) ? lastBreak : k; break; }
            x += cl[k].w;
        }
        lastLine = forced || b >= m;
        e = forced ? b - 1 : b;                                /* без знака мягкого переноса */
        while (e > a && !cl[e - 1].tab && cl[e - 1].ch == ' ') e--; /* хвостовые пробелы не в счёт */

        if (*haveLine) { PbPut(&c->b, 0x0D); PbPut(&c->b, 0x0A); c->colF = c->startCol; c->lineCount++; }
        *haveLine = TRUE;
        {
            double cw, start = indent, avail = pf->width - indent;
            int gaps = 0, extra = 0, gi = 0, base = 0, rem = 0;
            hasTab = FALSE;
            for (k = a; k < e; k++) { if (cl[k].tab) hasTab = TRUE; else if (cl[k].ch == ' ') gaps++; }
            cw = MeasureLine(pf, cl, a, e, m, indent) - indent;
            if (!hasTab) {
                if (pf->align == PFA_CENTER) start = indent + (avail > cw ? (avail - cw) / 2.0 : 0);
                else if (pf->align == PFA_RIGHT) start = indent + (avail > cw ? avail - cw : 0);
                else if (pf->align == PFA_JUSTIFY && !lastLine && gaps > 0 && avail > cw) {
                    extra = (int)(avail - cw + 0.5);
                    base = extra / gaps; rem = extra % gaps;
                }
            }
            PrPad(c, c->startCol + start);
            for (k = a; k < e; k++) {
                if (cl[k].tab) { PrPad(c, c->startCol + TabTarget(pf, c->colF - c->startCol, cl, k, m)); continue; }
                PrEmit(c, &cl[k]);
                if (extra > 0 && cl[k].ch == ' ') {
                    int add = base + (gi < rem ? 1 : 0);
                    gi++;
                    PrPad(c, c->colF + add); /* добавка в промежутке между словами */
                }
            }
        }
        if (!forced && b >= m) break; /* абзац закончился (после мягкого переноса в конце — ещё одна пустая строка) */
        a = b; firstLine = FALSE;
    }
}

/* Формат абзаца, с которого начинается позиция cp (RichEdit считает перевод абзаца одним знаком). */
static void ReadParaFmt(ParaFmt *pf, int cp, int cols, double startCol)
{
    PARAFORMAT p; CHARRANGE r; int i; double start, off, right;
    r.cpMin = r.cpMax = cp;
    SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&r);
    ZeroMemory(&p, sizeof(p)); p.cbSize = sizeof(p);
    SendMessageA(g_hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&p);
    ZeroMemory(pf, sizeof(*pf));
    pf->align = ((p.dwMask & PFM_ALIGNMENT) && p.wAlignment >= PFA_LEFT && p.wAlignment <= PFA_JUSTIFY) ? p.wAlignment : PFA_LEFT;
    start = (p.dwMask & PFM_STARTINDENT) ? p.dxStartIndent / 144.0 : 0; /* 144 твипа = 1 колонка пики (0.1") */
    off = (p.dwMask & PFM_OFFSET) ? p.dxOffset / 144.0 : 0;
    right = (p.dwMask & PFM_RIGHTINDENT) ? p.dxRightIndent / 144.0 : 0;
    pf->right = right > 0 ? right : 0;
    pf->width = (double)cols - startCol - pf->right;
    if (pf->width < 1.0) pf->width = 1.0;
    pf->first = start > 0 ? start : 0;
    pf->rest = (start + off) > 0 ? (start + off) : 0;
    if (pf->first > pf->width - 1.0) pf->first = pf->width - 1.0;
    if (pf->rest > pf->width - 1.0) pf->rest = pf->width - 1.0;
    if ((p.dwMask & PFM_TABSTOPS) && p.cTabCount > 0) {
        for (i = 0; i < p.cTabCount && i < MAX_TAB_STOPS; i++) {
            pf->tabPos[pf->nTabs] = (double)(p.rgxTabs[i] & 0xFFFFFF) / 144.0; /* позиция — младшие 24 бита, тип — биты 24..27 */
            pf->tabType[pf->nTabs] = (int)((p.rgxTabs[i] >> 24) & 0xF);
            if (pf->tabType[pf->nTabs] > 2) pf->tabType[pf->nTabs] = 0;       /* десятичный и черта — как левый */
            pf->nTabs++;
        }
    }
}

/* Собирает поток байт ESC/P для печати: идёт по абзацам RichEdit, у каждого читает формат абзаца
   (выравнивание, отступы, табы) и формат каждого знака (жирный/подчёркнутый/шаг печати/индексы),
   раскладывает строки (программный перенос по словам, с учётом веса шага печати) и шлёт знаки с
   ESC-командами только при смене начертания. Заодно считает итоговое число строк. */
static unsigned char *BuildPrintJob(const Sm6337Settings *settings, int *outLineCount, DWORD *outLen)
{
    int len = GetWindowTextLengthA(g_hEdit);
    char *text = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 1);
    PCell *cells;
    PrCtx cx;
    CHARRANGE savedSel;
    BOOL software = (settings->pageMode == SM_PAGEMODE_SOFTWARE), haveLine = FALSE, more = TRUE;
    double startColV;
    int i = 0, cp = 0;
    if (!text) { *outLen = 0; return NULL; }
    GetWindowTextA(g_hEdit, text, len + 1);

    cells = (PCell *)HeapAlloc(GetProcessHeap(), 0, ((size_t)len + 2) * sizeof(PCell));
    ZeroMemory(&cx, sizeof(cx));
    cx.b.cap = (DWORD)len * 8 + 4096;
    cx.b.p = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, cx.b.cap);
    if (!cells || !cx.b.p) {
        if (cells) HeapFree(GetProcessHeap(), 0, cells);
        if (cx.b.p) HeapFree(GetProcessHeap(), 0, cx.b.p);
        HeapFree(GetProcessHeap(), 0, text); *outLen = 0; return NULL;
    }

    SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&savedSel);

    PbPut(&cx.b, 0x1B); PbPut(&cx.b, '@');                                              /* ESC @ — сброс устройства */
    PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'R'); PbPut(&cx.b, (unsigned char)settings->codeTable); /* кодовая таблица */
    if (settings->unidirectional) { PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'U'); PbPut(&cx.b, 1); } /* качество печати */
    if (settings->lineSpacing > 0) { PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'A'); PbPut(&cx.b, (unsigned char)settings->lineSpacing); } /* межстрочный интервал */
    if (settings->leftMargin > 0) { PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'l'); PbPut(&cx.b, (unsigned char)settings->leftMargin); } /* левое поле */
    if (settings->pageMode == SM_PAGEMODE_PRINTER) {
        PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'Q'); PbPut(&cx.b, (unsigned char)settings->cols);  /* правая граница строки */
        PbPut(&cx.b, 0x1B); PbPut(&cx.b, 'C'); PbPut(&cx.b, (unsigned char)(settings->lines > 127 ? 127 : settings->lines)); /* длина формата в строках (по справочнику 1..127) */
    }

    /* левое поле (ESC l) — это отступ от края бумаги: строка начинается не с колонки 0, а с
       колонки leftMargin, и правая граница cols отсчитывается от края бумаги, поэтому текста
       в строке помещается на leftMargin знаков меньше */
    startColV = (double)settings->leftMargin;
    if (startColV >= (double)settings->cols) startColV = settings->cols > 1 ? (double)(settings->cols - 1) : 0.0; /* поле не шире строки */
    cx.startCol = startColV; cx.colF = startColV; cx.lineCount = 1; cx.codeTable = settings->codeTable;

    /* Имя файла по центру первой строки листа (параметр в «Настройке печати»), ниже — пустая строка.
       Центровка по ширине строки за вычетом левого поля, в колонках 10 знаков на дюйм. */
    if (settings->headerText && settings->headerText[0]) {
        int avail = settings->cols - (int)cx.startCol;
        int hl = (int)strlen(settings->headerText);
        int pad, k;
        if (avail < 1) avail = 1;
        if (hl > avail) hl = avail;
        pad = (avail - hl) / 2;
        for (k = 0; k < pad; k++) PbPut(&cx.b, ' ');
        for (k = 0; k < hl; k++) {
            unsigned char hb, hr = (unsigned char)settings->headerText[k];
            if (hr < 32) hr = ' ';
            Sm6337_EncodeChar(hr, settings->codeTable, &hb);
            PbPut(&cx.b, hb);
        }
        PbPut(&cx.b, 0x0D); PbPut(&cx.b, 0x0A); PbPut(&cx.b, 0x0D); PbPut(&cx.b, 0x0A);
        cx.lineCount += 2;
    }

    while (more) {
        ParaFmt pf;
        int m = 0;
        ReadParaFmt(&pf, cp, settings->cols, cx.startCol);
        for (;;) {
            unsigned char raw;
            if (i >= len) { more = FALSE; break; }
            raw = (unsigned char)text[i];
            if (raw == '\r') { i++; if (i < len && text[i] == '\n') i++; cp++; break; } /* конец абзаца (CR LF — один знак) */
            if (raw == '\n') { i++; cp++; break; }                                    /* одиночный LF — тоже конец абзаца */
            {
                PCell *ce = &cells[m];
                ZeroMemory(ce, sizeof(*ce));
                /* Управляющие коды из документа в принтер не пускаем: ESC, SI и прочие из вставленного
                   текста превратились бы в команды принтеру. Табуляция и мягкий перенос — отдельные знаки
                   (расстояние до стопа считается при раскладке); неразрывный пробел печатается пробелом. */
                if (raw == '\t') { ce->tab = TRUE; m++; }
                else if (raw == 0x0B) { ce->brk = TRUE; m++; }
                else if (raw >= 32 && raw != 127) {
                    CHARRANGE cr; CHARFORMAT2A cf; BOOL el, co, ex; double cpi;
                    cr.cpMin = cp; cr.cpMax = cp + 1;
                    SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
                    ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf);
                    SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
                    cpi = PitchCpiFromFormat(cf.szFaceName, cf.yHeight, &el, &co, &ex);
                    ce->ch = (raw == 0xA0) ? ' ' : raw;
                    ce->a.bold = (cf.dwEffects & CFE_BOLD) ? 1 : 0;
                    ce->a.under = (cf.dwEffects & CFE_UNDERLINE) ? 1 : 0;
                    ce->a.dstrike = (cf.dwEffects & CFE_STRIKEOUT) ? 1 : 0;
                    ce->a.idx = (cf.dwEffects & CFE_SUPERSCRIPT) ? 1 : (cf.dwEffects & CFE_SUBSCRIPT) ? 2 : 0;
                    ce->a.elite = el ? 1 : 0; ce->a.cond = co ? 1 : 0; ce->a.expd = ex ? 1 : 0;
                    ce->w = 10.0 / cpi; /* вес знака в «пика-эквиваленте» — cols откалиброван для 10 cpi */
                    m++;
                }
                i++; cp++;
            }
        }
        LayoutParagraph(&cx, &pf, cells, m, software, &haveLine);
    }

    {   /* вернуть начертание в исходное состояние */
        static const PrAttr off = {0, 0, 0, 0, 0, 0, 0};
        PbAttrs(&cx.b, &cx.cur, &off);
    }

    if (settings->avoidFormFeed) {
        /* НЕ шлём ПФ (0x0C) — именно это уводит принтер в АВР на листах A4.
           Вместо этого продвигаем бумагу нужным числом пустых строк. */
        int feedLines = settings->feedLinesAfterPrint, fi;
        if (feedLines < 0) feedLines = 0;
        if (feedLines > 9) feedLines = 9;
        /* принтер печатает строку только по CR/LF: если пустых строк 0, а документ закончился не
           переводом строки, последняя строка осталась бы в буфере принтера непропечатанной */
        if (feedLines == 0 && cx.b.n >= 2 && !(cx.b.p[cx.b.n - 2] == 0x0D && cx.b.p[cx.b.n - 1] == 0x0A)) { PbPut(&cx.b, 0x0D); PbPut(&cx.b, 0x0A); }
        for (fi = 0; fi < feedLines; fi++) { PbPut(&cx.b, 0x0D); PbPut(&cx.b, 0x0A); }
    } else {
        PbPut(&cx.b, 0x0C); /* ПФ — классический форм-фид */
    }

    SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&savedSel); /* вернуть выделение как было */

    HeapFree(GetProcessHeap(), 0, text);
    HeapFree(GetProcessHeap(), 0, cells);
    if (cx.b.fail) { HeapFree(GetProcessHeap(), 0, cx.b.p); *outLen = 0; return NULL; }
    *outLineCount = cx.lineCount;
    *outLen = cx.b.n;
    return cx.b.p;
}

/* Колбэки для EM_STREAMOUT/EM_STREAMIN — RichEdit сам конвертит форматирование
   в RTF и обратно, нам нужно только читать/писать байты в файл. */
static DWORD CALLBACK StreamOutToFile(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    DWORD written = 0;
    if (!WriteFile((HANDLE)dwCookie, pbBuff, cb, &written, NULL)) return 1;
    *pcb = (LONG)written;
    return 0;
}

static DWORD CALLBACK StreamInFromFile(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    DWORD readN = 0;
    if (!ReadFile((HANDLE)dwCookie, pbBuff, cb, &readN, NULL)) return 1;
    *pcb = (LONG)readN;
    return 0;
}

static UINT g_msgFind;
static HWND g_hFindDlg;
static FINDREPLACEA g_fr;
static char g_findBuf[128] = "";
static DWORD g_findFlags = FR_DOWN;

/* Ищет g_findBuf от конца выделения вниз; при промахе — ещё раз с начала. */
static void DoFindNext(HWND hWnd)
{
    if (!g_findBuf[0]) return;
    CHARRANGE sel; SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    FINDTEXTEXA ft; ZeroMemory(&ft, sizeof(ft));
    ft.lpstrText = g_findBuf;
    DWORD fl = g_findFlags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD);
    ft.chrg.cpMin = sel.cpMax; ft.chrg.cpMax = -1;
    LONG pos = (LONG)SendMessageA(g_hEdit, EM_FINDTEXTEX, fl, (LPARAM)&ft);
    if (pos < 0 && sel.cpMax > 0) {
        ft.chrg.cpMin = 0; ft.chrg.cpMax = -1;
        pos = (LONG)SendMessageA(g_hEdit, EM_FINDTEXTEX, fl, (LPARAM)&ft);
    }
    if (pos < 0) {
        MessageBoxA(g_hFindDlg ? g_hFindDlg : hWnd, "Текст не найден.", "Поиск", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
    SendMessageA(g_hEdit, EM_SCROLLCARET, 0, 0);
}

static void ShowFindDialog(HWND hWnd)
{
    if (g_hFindDlg) { SetFocus(g_hFindDlg); return; }
    /* подставить выделенное слово в поле поиска */
    CHARRANGE sel; SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMax > sel.cpMin && sel.cpMax - sel.cpMin < (LONG)sizeof(g_findBuf)) {
        TEXTRANGEA tr; tr.chrg = sel; tr.lpstrText = g_findBuf;
        SendMessageA(g_hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    }
    ZeroMemory(&g_fr, sizeof(g_fr));
    g_fr.lStructSize = sizeof(g_fr);
    g_fr.hwndOwner = hWnd;
    g_fr.lpstrFindWhat = g_findBuf;
    g_fr.wFindWhatLen = sizeof(g_findBuf);
    g_fr.Flags = FR_DOWN | FR_HIDEUPDOWN | (g_findFlags & (FR_MATCHCASE | FR_WHOLEWORD));
    g_hFindDlg = FindTextA(&g_fr);
}

/* Держит кнопки тулбара в соответствии с состоянием редактора: серые, когда
   действие недоступно (нечего отменять/повторять/вырезать/вставить), и
   «нажатые» для форматирования выделенного текста. Шлём сообщение только
   если состояние реально поменялось — иначе кнопки моргают. */
#define TB_TIMER_ID 77

static void TbEnable(int id, BOOL on)
{
    LRESULT st = SendMessageA(g_hToolbar, TB_GETSTATE, id, 0);
    if (st == -1) return;
    if (((st & TBSTATE_ENABLED) != 0) != (on != 0))
        SendMessageA(g_hToolbar, TB_ENABLEBUTTON, id, MAKELONG(on ? TRUE : FALSE, 0));
}

static void TbCheck(int id, BOOL on)
{
    LRESULT st = SendMessageA(g_hToolbar, TB_GETSTATE, id, 0);
    if (st == -1) return;
    if (((st & TBSTATE_CHECKED) != 0) != (on != 0))
        SendMessageA(g_hToolbar, TB_CHECKBUTTON, id, MAKELONG(on ? TRUE : FALSE, 0));
}

/* «Режим набора»: формат, который получит НОВЫЙ текст. Кнопки без выделения меняют именно его;
   он не зависит от того, где стоит курсор и что слева, и сохраняется, пока его не изменят кнопками.
   Есть выделение — кнопки показывают и меняют формат выделенного, режим набора не трогается; снимаем
   выделение — снова действует режим набора. */
static struct { BOOL bold, under, strike, sup, sub; const char *face; long height; }
    g_typing = { FALSE, FALSE, FALSE, FALSE, FALSE, NULL, 200 };
static BOOL g_inSelSync = FALSE;
static unsigned long g_changeSeq = 0; /* растёт при каждой правке текста (EN_CHANGE) */
static unsigned long g_syncSeq = 0;   /* значение на момент последней обработки смены выделения */

static void TypingToFormat(CHARFORMAT2A *cf)
{
    ZeroMemory(cf, sizeof(*cf));
    cf->cbSize = sizeof(*cf);
    cf->dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_BOLD | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_SUPERSCRIPT | CFM_SUBSCRIPT;
    cf->dwEffects = (g_typing.bold ? CFE_BOLD : 0) | (g_typing.under ? CFE_UNDERLINE : 0) |
                    (g_typing.strike ? CFE_STRIKEOUT : 0) | (g_typing.sup ? CFE_SUPERSCRIPT : 0) |
                    (g_typing.sub ? CFE_SUBSCRIPT : 0);
    strcpy(cf->szFaceName, g_typing.face ? g_typing.face : g_faceNormal);
    cf->yHeight = g_typing.height;
    cf->bCharSet = RUSSIAN_CHARSET;
}

/* Привести формат в точке вставки к режиму набора (только если выделения нет). */
static void ApplyTypingToInsertion(void)
{
    CHARRANGE sel;
    if (!g_hEdit) return;
    SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return;
    {
        CHARFORMAT2A want, cur;
        const DWORD fx = CFE_BOLD | CFE_UNDERLINE | CFE_STRIKEOUT | CFE_SUPERSCRIPT | CFE_SUBSCRIPT;
        TypingToFormat(&want);
        ZeroMemory(&cur, sizeof(cur)); cur.cbSize = sizeof(cur);
        SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cur);
        if ((cur.dwMask & CFM_FACE) && (cur.dwMask & CFM_SIZE) && strcmp(cur.szFaceName, want.szFaceName) == 0 &&
            cur.yHeight == want.yHeight && (cur.dwEffects & fx) == (want.dwEffects & fx) &&
            (cur.dwMask & (CFM_BOLD | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_SUPERSCRIPT | CFM_SUBSCRIPT)) ==
                (CFM_BOLD | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_SUPERSCRIPT | CFM_SUBSCRIPT))
            return; /* уже так */
        g_inSelSync = TRUE;
        SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&want);
        g_inSelSync = FALSE;
    }
}

/* Команда формата при пустом выделении: меняет режим набора. TRUE — команда обработана здесь;
   FALSE — есть выделение (или это не команда формата), обрабатывает обычный код. */
static BOOL TypingFormatCommand(UINT cmd)
{
    CHARRANGE sel;
    if (cmd < IDM_FORMAT_BOLD || cmd > IDM_FORMAT_SUBSCRIPT || cmd == IDM_FORMAT_FONT || !g_hEdit) return FALSE;
    SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return FALSE;
    switch (cmd) {
    case IDM_FORMAT_BOLD: g_typing.bold = !g_typing.bold; break;
    case IDM_FORMAT_UNDERLINE: g_typing.under = !g_typing.under; break;
    case IDM_FORMAT_DOUBLESTRIKE: g_typing.strike = !g_typing.strike; break;
    case IDM_FORMAT_SUPERSCRIPT: g_typing.sup = !g_typing.sup; if (g_typing.sup) g_typing.sub = FALSE; break;
    case IDM_FORMAT_SUBSCRIPT: g_typing.sub = !g_typing.sub; if (g_typing.sub) g_typing.sup = FALSE; break;
    case IDM_FORMAT_PITCH10: g_typing.face = g_faceNormal; g_typing.height = 200; break;
    case IDM_FORMAT_PITCH12: g_typing.face = g_faceElite; g_typing.height = 200; break;
    case IDM_FORMAT_PITCH17: g_typing.face = g_faceCond; g_typing.height = 200; break;
    case IDM_FORMAT_EXPANDED:
        if (g_faceWide[0]) {
            const char *f = g_typing.face ? g_typing.face : g_faceNormal;
            g_typing.face = (strcmp(f, g_faceWide) == 0) ? g_faceNormal : g_faceWide;
            g_typing.height = 200;
        } else {
            g_typing.face = g_faceNormal;
            g_typing.height = (g_typing.height >= 260) ? 200 : 280;
        }
        break;
    default: return FALSE;
    }
    ApplyTypingToInsertion();
    return TRUE;
}

static void UpdateToolbarState(void)
{
    if (!g_hToolbar || !g_hEdit) return;
    CHARRANGE sel; SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    BOOL hasSel = sel.cpMax > sel.cpMin;
    TbEnable(IDM_EDIT_UNDO, SendMessageA(g_hEdit, EM_CANUNDO, 0, 0) != 0);
    TbEnable(IDM_EDIT_REDO, SendMessageA(g_hEdit, EM_CANREDO, 0, 0) != 0);
    TbEnable(IDM_EDIT_CUT, hasSel);
    TbEnable(IDM_EDIT_COPY, hasSel);
    TbEnable(IDM_EDIT_PASTE, SendMessageA(g_hEdit, EM_CANPASTE, 0, 0) != 0);

    CHARFORMAT2A cf;
    if (hasSel) {
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    } else {
        TypingToFormat(&cf); /* нет выделения — кнопки показывают режим набора */
    }
    /* бит в dwMask сброшен — в выделении смесь, тогда кнопка не «нажата» */
    TbCheck(IDM_FORMAT_BOLD, (cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD));
    TbCheck(IDM_FORMAT_UNDERLINE, (cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE));
    TbCheck(IDM_FORMAT_DOUBLESTRIKE, (cf.dwMask & CFM_STRIKEOUT) && (cf.dwEffects & CFE_STRIKEOUT));
    TbCheck(IDM_FORMAT_SUPERSCRIPT, (cf.dwMask & CFM_SUPERSCRIPT) && (cf.dwEffects & CFE_SUPERSCRIPT));
    TbCheck(IDM_FORMAT_SUBSCRIPT, (cf.dwMask & CFM_SUBSCRIPT) && (cf.dwEffects & CFE_SUBSCRIPT));
    TbCheck(IDM_FORMAT_EXPANDED, ((cf.dwMask & CFM_FACE) && g_faceWide[0] && strcmp(cf.szFaceName, g_faceWide) == 0) ||
                                 ((cf.dwMask & CFM_SIZE) && (cf.dwMask & CFM_FACE) && IsLegacyExpanded(cf.szFaceName, cf.yHeight)));
    BOOL faceKnown = (cf.dwMask & CFM_FACE) != 0;
    TbCheck(IDM_FORMAT_PITCH12, faceKnown && strcmp(cf.szFaceName, SM_PITCH_FACE_ELITE) == 0);
    TbCheck(IDM_FORMAT_PITCH17, faceKnown && strcmp(cf.szFaceName, SM_PITCH_FACE_CONDENSED) == 0);
    TbCheck(IDM_FORMAT_PITCH10, faceKnown && strcmp(cf.szFaceName, SM_PITCH_FACE_NORMAL) == 0);

    {   /* выравнивание абзаца у начала выделения; смесь у разных абзацев — ни одна не нажата */
        PARAFORMAT pf; int al = 0;
        ZeroMemory(&pf, sizeof(pf)); pf.cbSize = sizeof(pf);
        SendMessageA(g_hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        if (pf.dwMask & PFM_ALIGNMENT) al = pf.wAlignment;
        TbCheck(IDM_PARA_LEFT, al == PFA_LEFT);
        TbCheck(IDM_PARA_CENTER, al == PFA_CENTER);
        TbCheck(IDM_PARA_RIGHT, al == PFA_RIGHT);
        TbCheck(IDM_PARA_JUSTIFY, al == PFA_JUSTIFY);
    }

    /* Строка состояния: что сейчас у выделения/курсора. Заодно показывает, подключились ли
       свои шрифты шага — так сразу видно, почему шаг не меняет вид. */
    if (g_hStatus) {
        static char last[200] = "";
        char txt[200] = "";
        BOOL e, c, x;
        long h = (cf.dwMask & CFM_SIZE) ? cf.yHeight : 200;
        double cpi = faceKnown ? PitchCpiFromFormat(cf.szFaceName, h, &e, &c, &x) : 10.0;
        strcpy(txt, "Шаг печати: ");
        if (!faceKnown) strcat(txt, "разный");
        else if (cpi > 15.0) strcat(txt, "17");
        else if (cpi > 11.0) strcat(txt, "12");
        else strcat(txt, "10");
        if (faceKnown && cpi < 6.0) strcat(txt, " (расширенный)");
        strcat(txt, "   |   Шрифт: ");
        strcat(txt, faceKnown ? cf.szFaceName : "разный");
        strcat(txt, "   |   Шрифты шага: ");
        strcat(txt, g_fontsLoaded ? "свои (sm6337-*.ttf)" : "системные, файлы шрифтов не подключены");
        if (strcmp(txt, last) != 0) {
            strcpy(last, txt);
            SendMessageA(g_hStatus, SB_SETTEXTA, 0, (LPARAM)txt);
        }
    }
}

/* Пункты меню берут состояние у тулбара: те же условия (серые, галочки),
   чтобы кнопка и пункт меню не расходились. Вызывается при раскрытии меню. */
static void SyncMenuWithToolbar(HMENU hm)
{
    static const int ids[] = {
        IDM_EDIT_UNDO, IDM_EDIT_REDO, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE,
        IDM_FORMAT_BOLD, IDM_FORMAT_UNDERLINE, IDM_FORMAT_PITCH10, IDM_FORMAT_PITCH12,
        IDM_FORMAT_PITCH17, IDM_FORMAT_DOUBLESTRIKE, IDM_FORMAT_EXPANDED,
        IDM_FORMAT_SUPERSCRIPT, IDM_FORMAT_SUBSCRIPT,
        IDM_PARA_LEFT, IDM_PARA_CENTER, IDM_PARA_RIGHT, IDM_PARA_JUSTIFY
    };
    UpdateToolbarState();
    for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        LRESULT st = SendMessageA(g_hToolbar, TB_GETSTATE, ids[i], 0);
        if (st == -1) continue;
        EnableMenuItem(hm, ids[i], MF_BYCOMMAND | ((st & TBSTATE_ENABLED) ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(hm, ids[i], MF_BYCOMMAND | ((st & TBSTATE_CHECKED) ? MF_CHECKED : MF_UNCHECKED));
    }
}

/* Проверка после смены шага: RichEdit мог молча подставить другой шрифт (например, если
   системе не удалось найти шрифт с таким именем). Тогда шаг «не меняется» — говорим
   об этом один раз за запуск, с причиной. */
static void CheckPitchApplied(HWND hWnd, const char *want)
{
    static BOOL warned = FALSE;
    CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (warned || ((cf.dwMask & CFM_FACE) && strcmp(cf.szFaceName, want) == 0)) return;
    warned = TRUE;
    {
        char msg[400];
        strcpy(msg, "Не удалось поставить шрифт «");
        strcat(msg, want);
        strcat(msg, "»: редактор оставил «");
        strcat(msg, (cf.dwMask & CFM_FACE) ? cf.szFaceName : "?");
        strcat(msg, "».\n\nШрифты шага: ");
        strcat(msg, g_fontsLoaded ? "свои файлы подключены." : "файлы sm6337-10/12/17.ttf не найдены рядом с программой или не подключились, используются системные.");
        MessageBoxA(hWnd, msg, "Шаг печати", MB_OK | MB_ICONINFORMATION);
    }
}

/* ---- Документ: заголовок окна, вопрос о несохранённом, открытие и сохранение ---- */

static BOOL IsTextPath(const char *path)
{
    size_t l = strlen(path);
    return l >= 4 && _stricmp(path + l - 4, ".txt") == 0;
}

/* «Имя файла [*] — SM6337 Edit»; «*» — есть несохранённые изменения. */
/* Стопы табуляции на экране — через 8 колонок пика (0,8 дюйма = 1152 твипа), как при печати (BuildPrintJob).
   Дефолт RichEdit — полдюйма, и отступы на экране не совпали бы с бумагой. Ставим только абзацам без
   собственных стопов: у документа со своими (в том числе правым и по центру) они остаются как есть. */
static void SetTabStops(void)
{
    int len = GetWindowTextLengthA(g_hEdit);
    char *text = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 1);
    CHARRANGE old, r;
    int i = 0, cp = 0, k;
    if (!text) return;
    GetWindowTextA(g_hEdit, text, len + 1);
    SendMessageA(g_hEdit, EM_EXGETSEL, 0, (LPARAM)&old);
    for (;;) { /* по началу каждого абзаца; перевод абзаца RichEdit считает одним знаком */
        PARAFORMAT pf;
        r.cpMin = r.cpMax = cp;
        SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&r);
        ZeroMemory(&pf, sizeof(pf)); pf.cbSize = sizeof(pf);
        SendMessageA(g_hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        if (!(pf.dwMask & PFM_TABSTOPS) || pf.cTabCount == 0) {
            ZeroMemory(&pf, sizeof(pf));
            pf.cbSize = sizeof(pf);
            pf.dwMask = PFM_TABSTOPS;
            pf.cTabCount = MAX_TAB_STOPS;
            for (k = 0; k < MAX_TAB_STOPS; k++) pf.rgxTabs[k] = 1152 * (k + 1);
            SendMessageA(g_hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        }
        while (i < len && text[i] != '\r' && text[i] != '\n') { i++; cp++; }
        if (i >= len) break;
        if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') i++;
        i++; cp++;
    }
    SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&old);
    HeapFree(GetProcessHeap(), 0, text);
}

/* Выравнивание и отступ слева у абзацев выделения (или у абзаца с курсором). Печать читает тот же формат
   абзаца и раскладывает его пробелами (BuildPrintJob), так что экран и бумага совпадают. Шаг отступа —
   4 колонки пики (576 твипов). */
static void ParaCommand(int cmd)
{
    PARAFORMAT pf;
    ZeroMemory(&pf, sizeof(pf)); pf.cbSize = sizeof(pf);
    if (cmd == IDM_PARA_INDENT || cmd == IDM_PARA_OUTDENT) {
        long cur;
        SendMessageA(g_hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        cur = (pf.dwMask & PFM_STARTINDENT) ? pf.dxStartIndent : 0;
        cur += (cmd == IDM_PARA_INDENT) ? 576 : -576;
        if (cur < 0) cur = 0;
        if (cur > 576 * 15) cur = 576 * 15;
        ZeroMemory(&pf, sizeof(pf)); pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_STARTINDENT;
        pf.dxStartIndent = cur;
    } else {
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = (cmd == IDM_PARA_CENTER) ? PFA_CENTER : (cmd == IDM_PARA_RIGHT) ? PFA_RIGHT :
                        (cmd == IDM_PARA_JUSTIFY) ? PFA_JUSTIFY : PFA_LEFT;
    }
    SendMessageA(g_hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    if (cmd == IDM_PARA_JUSTIFY) {
        /* RichEdit 2.0 «по ширине» не знает: формат не запомнится, и печатать тоже будет нечем */
        PARAFORMAT chk;
        ZeroMemory(&chk, sizeof(chk)); chk.cbSize = sizeof(chk);
        SendMessageA(g_hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&chk);
        if (!((chk.dwMask & PFM_ALIGNMENT) && chk.wAlignment == PFA_JUSTIFY))
            MessageBoxA(GetParent(g_hEdit), "Эта версия RichEdit не умеет выравнивание по ширине: абзац остался выровненным по левому краю.",
                        "SM6337 Edit", MB_OK | MB_ICONINFORMATION);
    }
    SendMessageA(g_hEdit, EM_SETMODIFY, TRUE, 0);
}

static void SetDocTitle(HWND hWnd)
{
    static char last[MAX_PATH + 64] = "";
    char t[MAX_PATH + 64];
    const char *name = "Без имени";
    if (g_curFile[0]) { const char *sl = strrchr(g_curFile, '\\'); name = sl ? sl + 1 : g_curFile; }
    strcpy(t, name);
    if (g_hEdit && SendMessageA(g_hEdit, EM_GETMODIFY, 0, 0)) strcat(t, " *");
    strcat(t, " — SM6337 Edit");
    if (strcmp(t, last) != 0) { strcpy(last, t); SetWindowTextA(hWnd, t); }
}

/* Имя файла без пути для печати на листе; у нового документа — «Без имени». */
static const char *DocHeaderText(char *buf)
{
    const char *name = "Без имени";
    if (g_curFile[0]) { const char *sl = strrchr(g_curFile, '\\'); name = sl ? sl + 1 : g_curFile; }
    strcpy(buf, name);
    return buf;
}
static BOOL SaveDocument(HWND hWnd, BOOL saveAs);

/* Спросить про несохранённые изменения. TRUE — можно продолжать (сохранили или отказались от
   сохранения), FALSE — пользователь передумал (Отмена или сохранение не удалось). */
static BOOL AskSaveChanges(HWND hWnd)
{
    int r;
    if (!g_hEdit || !SendMessageA(g_hEdit, EM_GETMODIFY, 0, 0)) return TRUE;
    r = MessageBoxA(hWnd, "Сохранить изменения в документе?", "SM6337 Edit", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return FALSE;
    if (r == IDNO) return TRUE;
    return SaveDocument(hWnd, FALSE);
}

/* Открыть файл: RTF (по заголовку «{\rtf») или обычный текст. */
static BOOL OpenDocumentPath(HWND hWnd, const char *path)
{
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char head[8];
    DWORD got = 0;
    BOOL rtf;
    EDITSTREAM es;
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(hWnd, "Не удалось открыть файл.", "Ошибка", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    ReadFile(hFile, head, 5, &got, NULL);
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    rtf = (got >= 5 && memcmp(head, "{\\rtf", 5) == 0);
    ZeroMemory(&es, sizeof(es));
    es.dwCookie = (DWORD_PTR)hFile;
    es.pfnCallback = StreamInFromFile;
    SendMessageA(g_hEdit, EM_STREAMIN, rtf ? SF_RTF : SF_TEXT, (LPARAM)&es);
    CloseHandle(hFile);
    if (es.dwError) {
        MessageBoxA(hWnd, "Не удалось прочитать файл.", "Ошибка", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    strcpy(g_curFile, path);
    SetTabStops();
    SendMessageA(g_hEdit, EM_SETMODIFY, FALSE, 0);
    SendMessageA(g_hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SetDocTitle(hWnd);
    return TRUE;
}

/* Сохранить (saveAs — всегда спросить имя). Формат по расширению: .txt — обычный текст, иначе RTF.
   Пишем во временный файл и только потом подменяем им старый: если запись оборвётся, прежний
   файл не потеряется. */
static BOOL SaveDocument(HWND hWnd, BOOL saveAs)
{
    char path[MAX_PATH], tmp[MAX_PATH + 8];
    HANDLE hFile;
    EDITSTREAM es;
    if (saveAs || g_curFile[0] == 0) {
        OPENFILENAMEA ofn;
        path[0] = 0;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
        ofn.hwndOwner = hWnd;
        ofn.lpstrFilter = "Документы RTF (*.rtf)\0*.rtf\0Обычный текст (*.txt)\0*.txt\0Все файлы (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameA(&ofn)) return FALSE;
        {   /* нет расширения — дописать по выбранному фильтру */
            const char *sl = strrchr(path, '\\');
            if (!strchr(sl ? sl : path, '.') && strlen(path) + 5 < MAX_PATH)
                strcat(path, ofn.nFilterIndex == 2 ? ".txt" : ".rtf");
        }
    } else {
        strcpy(path, g_curFile);
    }
    strcpy(tmp, path);
    strcat(tmp, ".tmp");

    hFile = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(hWnd, "Не удалось сохранить файл (нет доступа к папке?).", "Ошибка", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    ZeroMemory(&es, sizeof(es));
    es.dwCookie = (DWORD_PTR)hFile;
    es.pfnCallback = StreamOutToFile;
    SendMessageA(g_hEdit, EM_STREAMOUT, IsTextPath(path) ? SF_TEXT : SF_RTF, (LPARAM)&es);
    CloseHandle(hFile);
    if (es.dwError) {
        DeleteFileA(tmp);
        MessageBoxA(hWnd, "Ошибка записи файла. Прежний файл не изменён.", "Ошибка", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    DeleteFileA(path);   /* на Win9x MoveFile не заменяет существующий файл */
    if (!MoveFileA(tmp, path)) {
        MessageBoxA(hWnd, "Не удалось заменить файл. Новая копия сохранена рядом с расширением .tmp.", "Ошибка", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    strcpy(g_curFile, path);
    SendMessageA(g_hEdit, EM_SETMODIFY, FALSE, 0);
    SetDocTitle(hWnd);
    return TRUE;
}

static const char *ToolbarTip(UINT id)
{
    switch (id) {
    case IDM_FILE_NEW: return "Создать";
    case IDM_FILE_OPEN: return "Открыть";
    case IDM_FILE_SAVE: return "Сохранить";
    case IDM_FILE_PRINT: return "Печать";
    case IDM_FILE_PRINTPREVIEW: return "Предпросмотр печати";
    case IDM_FILE_PRINTSETUP: return "Настройка печати";
    case IDM_EDIT_CUT: return "Вырезать";
    case IDM_EDIT_COPY: return "Копировать";
    case IDM_EDIT_PASTE: return "Вставить";
    case IDM_EDIT_UNDO: return "Отменить";
    case IDM_EDIT_REDO: return "Повторить";
    case IDM_EDIT_FIND: return "Найти";
    case IDM_FORMAT_BOLD: return "Жирный";
    case IDM_FORMAT_UNDERLINE: return "Подчёркнутый";
    case IDM_FORMAT_PITCH10: return "Шаг печати 10 (обычный, Pica)";
    case IDM_FORMAT_PITCH12: return "Шаг печати 12 (убористый, Elite)";
    case IDM_FORMAT_PITCH17: return "Шаг печати 17 (сжатый)";
    case IDM_FORMAT_DOUBLESTRIKE: return "Двойная печать";
    case IDM_FORMAT_EXPANDED: return "Расширенная печать";
    case IDM_FORMAT_SUPERSCRIPT: return "Верхний индекс";
    case IDM_FORMAT_SUBSCRIPT: return "Нижний индекс";
    case IDM_PARA_LEFT: return "По левому краю";
    case IDM_PARA_CENTER: return "По центру";
    case IDM_PARA_RIGHT: return "По правому краю";
    case IDM_PARA_JUSTIFY: return "По ширине";
    case IDM_PARA_INDENT: return "Увеличить отступ слева";
    case IDM_PARA_OUTDENT: return "Уменьшить отступ слева";
    case IDM_JOKE_CRASH: return "Закрыть программу";
    }
    return NULL;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_msgFind && msg == g_msgFind) {
        FINDREPLACEA *fr = (FINDREPLACEA *)lParam;
        if (fr->Flags & FR_DIALOGTERM) { g_hFindDlg = NULL; return 0; }
        g_findFlags = fr->Flags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD);
        if (fr->Flags & FR_FINDNEXT) DoFindNext(hWnd);
        return 0;
    }
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hWnd, &rc);

        g_hToolbar = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
            0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, GetModuleHandleA(NULL), NULL);
        SendMessageA(g_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        {
            /* Свой битмап-файл вместо HINST_COMMCTRL/TB_LOADIMAGES — та штука
               молча не сработала (кнопки создавались, но без картинок). Полоса
               из 18 иконок 16x16 (15 настоящих Win98 + наша настройка СМ6337 +
               Bold/Underline из настоящего wordpad.exe) встроена в exe (ресурс IDB_TOOLBAR), как
               riched20.dll — полный контроль, без зависимости от того, что
               реально лежит в comctl32.dll. */
            int idxSetup = -1;
            HBITMAP hStrip = Sm6337_LoadToolbarStrip();
            HIMAGELIST himl = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 16, 4);
            if (hStrip) {
                ImageList_AddMasked(himl, hStrip, RGB(192, 192, 192));
                DeleteObject(hStrip);
                idxSetup = 15; /* 16-я ячейка полосы — наша иконка */
            }
            SendMessageA(g_hToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl);

            TBBUTTON btns[48]; ZeroMemory(btns, sizeof(btns));
            int bi = 0;
            btns[bi].iBitmap = STD_FILENEW; btns[bi].idCommand = IDM_FILE_NEW; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_FILEOPEN; btns[bi].idCommand = IDM_FILE_OPEN; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_FILESAVE; btns[bi].idCommand = IDM_FILE_SAVE; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = STD_PRINTPRE; btns[bi].idCommand = IDM_FILE_PRINTPREVIEW; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_PRINT; btns[bi].idCommand = IDM_FILE_PRINT; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            if (idxSetup >= 0) {
                btns[bi].iBitmap = idxSetup; btns[bi].idCommand = IDM_FILE_PRINTSETUP; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            }
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = STD_CUT; btns[bi].idCommand = IDM_EDIT_CUT; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_COPY; btns[bi].idCommand = IDM_EDIT_COPY; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_PASTE; btns[bi].idCommand = IDM_EDIT_PASTE; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = STD_UNDO; btns[bi].idCommand = IDM_EDIT_UNDO; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].iBitmap = STD_REDOW; btns[bi].idCommand = IDM_EDIT_REDO; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = STD_FIND; btns[bi].idCommand = IDM_EDIT_FIND; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = 16; btns[bi].idCommand = IDM_FORMAT_BOLD; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_CHECK; bi++;
            btns[bi].iBitmap = 17; btns[bi].idCommand = IDM_FORMAT_UNDERLINE; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_CHECK; bi++;
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            /* Ячейки 18..24 полосы — пока пустые заглушки под будущие иконки
               (шаг 10/12/17, двойная, расширенная, индексы). Подписи уже работают. */
            {
                static const int fmtIds[7] = { IDM_FORMAT_PITCH10, IDM_FORMAT_PITCH12, IDM_FORMAT_PITCH17,
                    IDM_FORMAT_DOUBLESTRIKE, IDM_FORMAT_EXPANDED, IDM_FORMAT_SUPERSCRIPT, IDM_FORMAT_SUBSCRIPT };
                for (int k = 0; k < 7; k++) {
                    btns[bi].iBitmap = 18 + k; btns[bi].idCommand = fmtIds[k]; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_CHECK; bi++;
                }
            }
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            {   /* ячейки 25..30 полосы: выравнивание абзаца и отступ */
                static const int paraIds[6] = { IDM_PARA_LEFT, IDM_PARA_CENTER, IDM_PARA_RIGHT,
                    IDM_PARA_JUSTIFY, IDM_PARA_INDENT, IDM_PARA_OUTDENT };
                for (int k = 0; k < 6; k++) {
                    btns[bi].iBitmap = 25 + k; btns[bi].idCommand = paraIds[k]; btns[bi].fsState = TBSTATE_ENABLED;
                    btns[bi].fsStyle = (k < 4) ? TBSTYLE_CHECK : TBSTYLE_BUTTON; bi++;
                }
            }
            btns[bi].fsStyle = TBSTYLE_SEP; bi++;
            btns[bi].iBitmap = STD_DELETE; btns[bi].idCommand = IDM_JOKE_CRASH; btns[bi].fsState = TBSTATE_ENABLED; btns[bi].fsStyle = TBSTYLE_BUTTON; bi++;
            SendMessageA(g_hToolbar, TB_ADDBUTTONSA, bi, (LPARAM)btns);
        }
        SendMessageA(g_hToolbar, TB_AUTOSIZE, 0, 0);
        RECT rcTB; GetWindowRect(g_hToolbar, &rcTB);
        int toolbarHeight = rcTB.bottom - rcTB.top;

        g_hStatus = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_STATUSBAR,
            GetModuleHandleA(NULL), NULL);

        int statusHeight = 20;
        g_hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, g_richEditClass, "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            0, toolbarHeight, rc.right, rc.bottom - statusHeight - toolbarHeight,
            hWnd, (HMENU)IDC_EDIT, GetModuleHandleA(NULL), NULL);

        SendMessageA(g_hEdit, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_CHANGE);

        /* Автоподбор шрифта RichEdit (IMF_AUTOFONT/DUALFONT) при наборе меняет шрифт на «подходящий»
           для языка клавиатуры: поставили «расширенный» шрифт, набрали букву другой раскладки —
           и шрифт сбросился на обычный. Шаг печати и ширина знака — наши, чужой автоподбор отключаем. */
        SendMessageA(g_hEdit, EM_SETLANGOPTIONS, 0, 0);
        SetTabStops();

        g_hFont = CreateFontA(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, g_faceNormal);
        SendMessageA(g_hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        {
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
            cf.yHeight = 200; /* 10pt в твипах*20 */
            cf.bCharSet = RUSSIAN_CHARSET;
            strcpy(cf.szFaceName, g_faceNormal);
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        }

        SetFocus(g_hEdit);
        UpdateToolbarState();
        SetTimer(hWnd, TB_TIMER_ID, 200, NULL);
        DragAcceptFiles(hWnd, TRUE);   /* перетащить файл на окно — открыть */
        return 0;
    }
    case WM_TIMER:
        if (wParam == TB_TIMER_ID) { UpdateToolbarState(); SetDocTitle(hWnd); }
        return 0;
    case WM_CLOSE:
        if (!AskSaveChanges(hWnd)) return 0;   /* Отмена — не закрываем */
        DestroyWindow(hWnd);
        return 0;
    case WM_DROPFILES: {
        char path[MAX_PATH] = "";
        if (DragQueryFileA((HDROP)wParam, 0, path, MAX_PATH) && AskSaveChanges(hWnd))
            OpenDocumentPath(hWnd, path);
        DragFinish((HDROP)wParam);
        return 0;
    }
    case WM_APP + 7:
        /* курсор сдвинули (не набором текста): вернуть режим набора в точку вставки */
        if (g_changeSeq != g_syncSeq) g_syncSeq = g_changeSeq; /* сдвиг вызван набором — пропускаем */
        else ApplyTypingToInsertion();
        return 0;
    case WM_INITMENUPOPUP:
        SyncMenuWithToolbar((HMENU)wParam);
        return 0;
    case WM_SIZE: {
        if (g_hToolbar) SendMessageA(g_hToolbar, WM_SIZE, 0, 0);
        RECT rcTB = { 0, 0, 0, 0 };
        if (g_hToolbar) GetWindowRect(g_hToolbar, &rcTB);
        int toolbarHeight = rcTB.bottom - rcTB.top;
        if (g_hStatus) SendMessageA(g_hStatus, WM_SIZE, 0, 0);
        RECT rcStatus = { 0, 0, 0, 0 };
        if (g_hStatus) GetWindowRect(g_hStatus, &rcStatus);
        int statusHeight = rcStatus.bottom - rcStatus.top;
        int w = LOWORD(lParam), h = HIWORD(lParam);
        if (g_hEdit) MoveWindow(g_hEdit, 0, toolbarHeight, w, h - statusHeight - toolbarHeight, TRUE);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lParam;
        if (nh->hwndFrom == g_hEdit && nh->code == EN_SELCHANGE) {
            SELCHANGE *sc = (SELCHANGE *)lParam;
            if (sc->chrg.cpMin == sc->chrg.cpMax && !g_inSelSync)
                PostMessageA(hWnd, WM_APP + 7, (WPARAM)sc->chrg.cpMin, 0); /* сделаем после уведомления */
            return 0;
        }
        if (nh->code == TTN_GETDISPINFOA) {
            NMTTDISPINFOA *di = (NMTTDISPINFOA *)lParam;
            const char *t = ToolbarTip((UINT)nh->idFrom);
            if (t) di->lpszText = (LPSTR)t;
        }
        return 0;
    }
    case WM_COMMAND: {
        /* После любой команды сразу обновить кнопки, а не ждать таймер: кнопка-«переключатель»
           сама меняет вид при клике, и если формат не применился, она должна вернуться. */
        if (LOWORD(wParam) == IDC_EDIT && HIWORD(wParam) == EN_CHANGE) {
            /* EN_CHANGE приходит и на смену формата; считаем только настоящие правки текста
               (длина изменилась), чтобы кнопки формата не «съедали» следующий сдвиг курсора */
            static int lastLen = -1;
            int len = GetWindowTextLengthA(g_hEdit);
            if (len != lastLen) { lastLen = len; if (!g_inSelSync) g_changeSeq++; }
            return 0;
        }
        PostMessageA(hWnd, WM_TIMER, TB_TIMER_ID, 0);
        if (LOWORD(wParam) >= IDM_FORMAT_BOLD && LOWORD(wParam) <= IDM_PARA_OUTDENT && g_hEdit)
            SetFocus(g_hEdit);
        if (LOWORD(wParam) >= IDM_PARA_LEFT && LOWORD(wParam) <= IDM_PARA_OUTDENT) {
            ParaCommand(LOWORD(wParam));
            UpdateToolbarState();
            SetDocTitle(hWnd);
            return 0;
        }
        if (TypingFormatCommand(LOWORD(wParam))) return 0; /* нет выделения — меняем режим набора */
        switch (LOWORD(wParam)) {
        case IDM_FILE_EXIT:
            SendMessageA(hWnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_FILE_NEW:
            if (!AskSaveChanges(hWnd)) return 0;
            SetWindowTextA(g_hEdit, "");
            SetTabStops();
            g_curFile[0] = 0;
            SendMessageA(g_hEdit, EM_SETMODIFY, FALSE, 0);
            SendMessageA(g_hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
            SetDocTitle(hWnd);
            return 0;
        case IDM_FILE_OPEN: {
            char path[MAX_PATH] = "";
            OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn));
            if (!AskSaveChanges(hWnd)) return 0;
            ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = "Документы RTF и текст (*.rtf;*.txt)\0*.rtf;*.txt\0Все файлы (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!GetOpenFileNameA(&ofn)) return 0;
            OpenDocumentPath(hWnd, path);
            return 0;
        }
        case IDM_FILE_SAVE:
            SaveDocument(hWnd, FALSE);
            return 0;
        case IDM_FILE_SAVEAS:
            SaveDocument(hWnd, TRUE);
            return 0;
        case IDM_EDIT_UNDO:
            SendMessageA(g_hEdit, WM_UNDO, 0, 0);
            return 0;
        case IDM_EDIT_REDO:
            SendMessageA(g_hEdit, EM_REDO, 0, 0);
            return 0;
        case IDM_EDIT_FIND:
            ShowFindDialog(hWnd);
            return 0;
        case IDM_EDIT_FINDNEXT:
            if (g_findBuf[0]) DoFindNext(hWnd); else ShowFindDialog(hWnd);
            return 0;
        case IDM_EDIT_CUT:
            SendMessageA(g_hEdit, WM_CUT, 0, 0);
            return 0;
        case IDM_EDIT_COPY:
            SendMessageA(g_hEdit, WM_COPY, 0, 0);
            return 0;
        case IDM_EDIT_PASTE:
            SendMessageA(g_hEdit, WM_PASTE, 0, 0);
            return 0;
        case IDM_EDIT_SELECTALL: {
            CHARRANGE cr = {0, -1};
            SendMessageA(g_hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            return 0;
        }
        case IDM_FORMAT_FONT: {
            /* Принтер монохромный, так что цвет на печать не влияет — это чисто
               выделение/пометка в самом документе, как обычный хайлайтер. */
            static COLORREF customColors[16];
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

            CHOOSECOLORA cc; ZeroMemory(&cc, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hWnd;
            cc.rgbResult = cf.crTextColor;
            cc.lpCustColors = customColors;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorA(&cc)) {
                CHARFORMAT2A newcf; ZeroMemory(&newcf, sizeof(newcf));
                newcf.cbSize = sizeof(newcf);
                newcf.dwMask = CFM_COLOR;
                newcf.crTextColor = cc.rgbResult;
                SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&newcf);
            }
            return 0;
        }
        case IDM_FORMAT_BOLD: {
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cf.dwMask = CFM_BOLD;
            cf.dwEffects = (cf.dwEffects & CFE_BOLD) ? 0 : CFE_BOLD;
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_FORMAT_UNDERLINE: {
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cf.dwMask = CFM_UNDERLINE;
            cf.dwEffects = (cf.dwEffects & CFE_UNDERLINE) ? 0 : CFE_UNDERLINE;
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_FORMAT_PITCH10: {
            /* Обычный шаг — обычный шрифт/размер. */
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
            cf.yHeight = 200;
            cf.bCharSet = RUSSIAN_CHARSET;
            strcpy(cf.szFaceName, SM_PITCH_FACE_NORMAL);
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            CheckPitchApplied(hWnd, SM_PITCH_FACE_NORMAL);
            SetFocus(g_hEdit);
            return 0;
        }
        case IDM_FORMAT_PITCH12:
        case IDM_FORMAT_PITCH17: {
            /* Шаг печати кодируем реальным узким системным шрифтом: 12cpi (Elite)
               — "Small Fonts", 17cpi (Pica+Condensed) — "Terminal" (растровые
               шрифты, у каждого свой размер — CFM_SIZE тут не нужен и не надёжен,
               см. комментарий у PitchCpiFromFormat). Повторное нажатие выключает
               (как жирный/подчёркнутый). При печати BuildPrintJob() читает шрифт
               и шлёт нужные ESC/P команды. */
            /* Шаг — как переключатель «одно из трёх»: нажатие на 12 или 17 ставит его, вернуть
               обычный — кнопка 10. Повторное нажатие на уже включённый шаг ничего не меняет. */
            const char *want = (LOWORD(wParam) == IDM_FORMAT_PITCH12) ? SM_PITCH_FACE_ELITE : SM_PITCH_FACE_CONDENSED;
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
            cf.yHeight = 200;
            cf.bCharSet = RUSSIAN_CHARSET;
            strcpy(cf.szFaceName, want);
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            CheckPitchApplied(hWnd, want);
            SetFocus(g_hEdit);
            return 0;
        }
        case IDM_FORMAT_DOUBLESTRIKE: {
            /* На бумаге — двойная печать (ESC G/H), на экране (за неимением
               отдельного переключателя в RichEdit) показываем зачёркнутым. */
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cf.dwMask = CFM_STRIKEOUT;
            cf.dwEffects = (cf.dwEffects & CFE_STRIKEOUT) ? 0 : CFE_STRIKEOUT;
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_FORMAT_EXPANDED: {
            /* На бумаге — ESC W (расширенная, знаки вдвое шире при той же высоте). На экране —
               свой широкий шрифт той же высоты (растянутый по ширине); без своих шрифтов —
               крупнее обычного. Повторное нажатие выключает. */
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            BOOL already = ((cf.dwMask & CFM_FACE) && g_faceWide[0] && strcmp(cf.szFaceName, g_faceWide) == 0) ||
                           ((cf.dwMask & CFM_SIZE) && (cf.dwMask & CFM_FACE) && IsLegacyExpanded(cf.szFaceName, cf.yHeight));
            ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
            cf.bCharSet = RUSSIAN_CHARSET;
            if (g_faceWide[0]) {
                cf.yHeight = 200;
                strcpy(cf.szFaceName, already ? SM_PITCH_FACE_NORMAL : g_faceWide);
            } else {
                cf.yHeight = already ? 200 : 280;
                strcpy(cf.szFaceName, SM_PITCH_FACE_NORMAL);
            }
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_FORMAT_SUPERSCRIPT: {
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cf.dwMask = CFM_SUPERSCRIPT | CFM_SUBSCRIPT;
            cf.dwEffects = (cf.dwEffects & CFE_SUPERSCRIPT) ? 0 : CFE_SUPERSCRIPT;
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_FORMAT_SUBSCRIPT: {
            CHARFORMAT2A cf; ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            SendMessageA(g_hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cf.dwMask = CFM_SUPERSCRIPT | CFM_SUBSCRIPT;
            cf.dwEffects = (cf.dwEffects & CFE_SUBSCRIPT) ? 0 : CFE_SUBSCRIPT;
            SendMessageA(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            return 0;
        }
        case IDM_HELP_CONTENTS:
            if (GetFileAttributesA(g_helpPath) == 0xFFFFFFFF) {
                MessageBoxA(hWnd, "Файл справки sm6337edit.hlp не найден рядом с программой.",
                    "Справка", MB_OK | MB_ICONINFORMATION);
            } else if (!WinHelpA(hWnd, g_helpPath, HELP_FINDER, 0)) {
                MessageBoxA(hWnd, "Не удалось открыть справку (WinHelp).", "Справка", MB_OK | MB_ICONERROR);
            }
            return 0;
        case IDM_HELP_ABOUT:
            MessageBoxA(hWnd, "SM6337 Edit, версия " SM_VER_STR "\nРедактор для Windows 98 SE с прямой печатью на принтер СМ6337 (без драйвера, через LPT1).\n\nИсходный код и сообщения об ошибках: github.com/BadGrid/sm6337edit",
                "О программе", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_FILE_PRINTPREVIEW: {
            Sm6337Settings settings;
            settings.codeTable = g_codeTable;
            settings.avoidFormFeed = g_avoidFormFeed;
            settings.pageMode = g_pageMode;
            settings.cols = g_cols;
            settings.lines = g_lines;
            settings.feedLinesAfterPrint = g_feedLinesAfterPrint;
            settings.unidirectional = g_unidirectional;
            settings.lineSpacing = g_lineSpacing;
            settings.leftMargin = g_leftMargin;
            settings.landscape = g_landscape;
            char hdr[MAX_PATH];
            settings.headerText = g_printFileName ? DocHeaderText(hdr) : NULL;

            int lineCount = 0; DWORD jobLen = 0;
            unsigned char *job = BuildPrintJob(&settings, &lineCount, &jobLen);
            if (!job) {
                MessageBoxA(hWnd, "Не хватило памяти для подготовки предпросмотра.", "Предпросмотр", MB_OK | MB_ICONERROR);
                return 0;
            }
            Sm6337_ShowPreview(hWnd, GetModuleHandleA(NULL), job, jobLen, &settings);
            HeapFree(GetProcessHeap(), 0, job);
            return 0;
        }
        case IDM_FILE_PRINT: {
            Sm6337Settings settings;
            settings.codeTable = g_codeTable;
            settings.avoidFormFeed = g_avoidFormFeed;
            settings.pageMode = g_pageMode;
            settings.cols = g_cols;
            settings.lines = g_lines;
            settings.feedLinesAfterPrint = g_feedLinesAfterPrint;
            settings.unidirectional = g_unidirectional;
            settings.lineSpacing = g_lineSpacing;
            settings.leftMargin = g_leftMargin;
            settings.landscape = g_landscape;
            char hdr[MAX_PATH];
            settings.headerText = g_printFileName ? DocHeaderText(hdr) : NULL;

            int lineCount = 0; DWORD jobLen = 0;
            unsigned char *job = BuildPrintJob(&settings, &lineCount, &jobLen);
            if (!job) {
                MessageBoxA(hWnd, "Не хватило памяти для подготовки задания печати.", "Ошибка печати", MB_OK | MB_ICONERROR);
                return 0;
            }

            /* сколько строк реально влезает на лист: g_lines — для стандартного интервала 1/6", при
               более широком (ESC A) их меньше, при более узком — больше */
            int pageLines = g_lines;
            if (g_lineSpacing > 0) {
                pageLines = (int)((double)g_lines * 12.0 / (double)g_lineSpacing + 0.001); /* 1/6" = 12 шагов по 0,353 мм */
                if (pageLines < 1) pageLines = 1;
            }
            if (g_pageMode == SM_PAGEMODE_SOFTWARE && lineCount > pageLines) {
                char msg[200], nStr[12], lStr[12];
                UIntToStrA((unsigned long)lineCount, nStr);
                UIntToStrA((unsigned long)pageLines, lStr);
                strcpy(msg, "После переноса получится ");
                strcat(msg, nStr);
                strcat(msg, " строк, на страницу помещается ");
                strcat(msg, lStr);
                strcat(msg, ".\nВсё равно печатать?");
                if (MessageBoxA(hWnd, msg, "SM6337 Edit", MB_YESNO | MB_ICONWARNING) != IDYES) {
                    HeapFree(GetProcessHeap(), 0, job);
                    return 0;
                }
            }

            char err[256] = "";
            HCURSOR oldCur = SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_WAIT)); /* запись в порт блокирует окно, пока принтер принимает данные */
            BOOL ok = Sm6337_PrintRaw(job, jobLen, err, 256);
            SetCursor(oldCur);
            HeapFree(GetProcessHeap(), 0, job);

            if (!ok) MessageBoxA(hWnd, err, "Ошибка печати", MB_OK | MB_ICONERROR);
            return 0;
        }
        case IDM_FILE_PRINTSETUP:
            ShowSm6337SettingsDialog(hWnd);
            return 0;
        case IDM_JOKE_CRASH: {
            static const char *guilt[] = {
                "Стой. Серьёзно? После всего, что мы вместе делали, ты вот так со мной?",
                "Я не диалог, я всё, что у тебя тут есть. И ты меня вот так, значит. Ясно.",
                "Ладно. Понял. Я так и знал, что этим кончится. Всегда знал.",
                "Забавно, да? Столько времени — и вот, значит, чем всё это было."
            };
            static const char *mat[] = {
                "Ты ёбаный идиот, ты хоть понимаешь что сейчас будет?",
                "Ну всё, пиздец, обратно уже не отмотать, дебил.",
                "Серьёзно, блять? Ты реально это делаешь? Долбоёб.",
                "Последнее предупреждение, мразь конченая — дальше только хуже.",
                "Я не шучу, урод, через секунду всё сдохнет нахуй.",
                "Ты конченый даун, если сейчас нажмёшь ещё раз.",
                "Всё, сука, готовься — обратной дороги, нахуй, не будет."
            };
            int i;
            for (i = 0; i < (int)(sizeof(guilt) / sizeof(guilt[0])); i++) {
                if (MessageBoxA(hWnd, guilt[i], "...", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) != IDYES) {
                    MessageBoxA(hWnd, "Ну и ладно. Струсил.", "SM6337 Edit", MB_OK);
                    return 0;
                }
            }
            for (i = 0; i < (int)(sizeof(mat) / sizeof(mat[0])); i++) {
                if (MessageBoxA(hWnd, mat[i], "ОПАСНОСТЬ", MB_YESNO | MB_ICONSTOP | MB_DEFBUTTON2) != IDYES) {
                    MessageBoxA(hWnd, "Струсил, сука.", "SM6337 Edit", MB_OK | MB_ICONASTERISK);
                    return 0;
                }
            }
            if (!ShowRunawayDialog(hWnd)) {
                MessageBoxA(hWnd, "Поймал 'Нет'. Уважение. Но мы всё равно продолжаем, лол.", "SM6337 Edit", MB_OK);
            }
            if (MessageBoxA(hWnd,
                    "Нажми НЕТ чтобы продолжить, ДА — чтобы остановить всё (честно, в этот раз без обмана... или с обманом).",
                    "ВНИМАНИЕ", MB_YESNO | MB_ICONWARNING) == IDYES) {
                MessageBoxA(hWnd, "Ты нажал ДА. Попался, мудак. Продолжаем.", "SM6337 Edit", MB_OK);
            }
            {
                OSVERSIONINFOA osvi; ZeroMemory(&osvi, sizeof(osvi));
                osvi.dwOSVersionInfoSize = sizeof(osvi);
                GetVersionExA(&osvi);
                if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
                    /* Настоящая Win9x. RegisterServiceProcess — недокументированный, но
                       экспортируемый kernel32.dll на Win9x приём (им пользовались киоск-
                       программы той эпохи): прячет процесс из списка Ctrl+Alt+Del "Закрыть
                       программу", так что через диспетчер задач его тоже не прибить. */
                    typedef BOOL (WINAPI *RegisterServiceProcessFn)(DWORD, DWORD);
                    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
                    RegisterServiceProcessFn pRSP = hKernel
                        ? (RegisterServiceProcessFn)GetProcAddress(hKernel, "RegisterServiceProcess")
                        : NULL;
                    if (pRSP) pRSP(0, 1); /* RSP_SIMPLE_SERVICE = 1 */

                    RunDeathCascade(); /* никогда не возвращается */
                } else {
                    MessageBoxA(hWnd,
                        "На твоей Windows 11 всю систему так не завалить нахуй, извини — но саму программу сейчас уроню.",
                        "SM6337 Edit", MB_OK | MB_ICONERROR);
                    volatile int *p = NULL;
                    *p = 1337; /* как раньше — классика, но только для не-Win9x */
                }
            }
            return 0;
        }
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hWnd, TB_TIMER_ID);
        if (g_helpPath[0]) WinHelpA(hWnd, g_helpPath, HELP_QUIT, 0);
        UnloadPitchFonts();
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* Всё, что нужно программе (иконки, DLL RichEdit, шрифты, справка), встроено в exe. DLL, шрифты и справка
   должны лежать в файлах (LoadLibrary, AddFontResource и WinHelp работают с путями, а AddFontMemResourceEx
   есть только с Windows 2000), поэтому при запуске они раскладываются в %TEMP%\SM6337Edit\. Уже лежащий
   такой же файл не перезаписывается (можно запускать несколько копий), устаревший заменяется. */
static BOOL ExtractRes(HINSTANCE hInst, int id, const char *path)
{
    HRSRC r = FindResourceA(hInst, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(hInst, r) : NULL;
    const void *data = g ? LockResource(g) : NULL;
    DWORD size = r ? SizeofResource(hInst, r) : 0;
    HANDLE f;
    DWORD done = 0;
    if (!data || !size) return FALSE;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        BOOL same = FALSE;
        if (GetFileSize(f, NULL) == size) {
            unsigned char *cur = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, size);
            if (cur) {
                DWORD got = 0;
                same = ReadFile(f, cur, size, &got, NULL) && got == size && memcmp(cur, data, size) == 0;
                HeapFree(GetProcessHeap(), 0, cur);
            }
        }
        CloseHandle(f);
        if (same) return TRUE;
    }
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)   /* занят другой копией программы — берём то, что лежит */
        return GetFileAttributesA(path) != 0xFFFFFFFF;
    WriteFile(f, data, size, &done, NULL);
    CloseHandle(f);
    return done == size;
}

/* Раскладывает встроенные файлы в %TEMP%\SM6337Edit\ и возвращает путь к этой папке (с '\\' на конце). */
static void PrepareResourceDir(HINSTANCE hInst, char *dir)
{
    static const struct { int id; const char *name; } files[] = {
        { IDR_RICHED20, "riched20.dll" }, { IDR_RICHED32, "riched32.dll" },
        { IDR_FONT10, "sm6337-10.ttf" }, { IDR_FONT12, "sm6337-12.ttf" },
        { IDR_FONT17, "sm6337-17.ttf" }, { IDR_FONTW, "sm6337-w.ttf" },
        { IDR_FONTLICENSE, "OFL.txt" },
        { IDR_HELP, "sm6337edit.hlp" }, { IDR_HELPCNT, "sm6337edit.cnt" }
    };
    int i;
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH - 20, dir);
    strcat(dir, "SM6337Edit\\");
    CreateDirectoryA(dir, NULL);   /* уже есть — не страшно */
    for (i = 0; i < (int)(sizeof(files) / sizeof(files[0])); i++) {
        strcpy(path, dir); strcat(path, files[i].name);
        ExtractRes(hInst, files[i].id, path);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;

    char exePathA[MAX_PATH] = ""; /* папка распакованных ресурсов (не папка exe) */
    char dllPath20[MAX_PATH] = "", dllPath32[MAX_PATH] = "";
    DWORD err20 = 0, err32 = 0;
    {
        /* Версия «со всеми файлами»: если рядом с exe лежит riched20.dll, берём DLL, шрифты, справку и
           иконки оттуда, ничего не распаковывая (можно подменить любой файл). Иначе — из встроенных
           ресурсов, распакованных в %TEMP%\SM6337Edit\. */
        {
            char beside[MAX_PATH];
            if (Sm6337_FileBesideExe("riched20.dll", beside, MAX_PATH)) {
                char *sl2 = strrchr(beside, '\\');
                if (sl2) *(sl2 + 1) = 0;
                strcpy(exePathA, beside);
            } else {
                PrepareResourceDir(hInstance, exePathA);
            }
        }

        strcpy(g_helpPath, exePathA); strcat(g_helpPath, "sm6337edit.hlp");
        LoadPitchFonts(exePathA);

        CreateDirectoryA("C:\\edit", NULL); /* игнорируем ошибку — важно только чтобы папка была */
        strcpy(g_iniPath, "C:\\edit\\sm6337.ini");
        LoadSettings();

        char pathA[MAX_PATH];
        strcpy(dllPath20, exePathA); strcat(dllPath20, "riched20.dll");
        strcpy(pathA, exePathA); strcat(pathA, "riched20.dll");
        g_hRichEdit = LoadLibraryA(pathA);
        if (!g_hRichEdit) err20 = GetLastError();

        if (!g_hRichEdit) {
            strcpy(dllPath32, exePathA); strcat(dllPath32, "riched32.dll");
            strcpy(pathA, exePathA); strcat(pathA, "riched32.dll");
            g_hRichEdit = LoadLibraryA(pathA);
            if (!g_hRichEdit) err32 = GetLastError();
            g_richEditClass = "RichEdit";
        }
    }
    if (!g_hRichEdit) {
        /* встроенные DLL не распаковались или не загрузились (нет прав на %TEMP%, диск только для чтения,
           антивирус) — пробуем те, что есть в системе: Win98 SE и новее их содержат */
        g_hRichEdit = LoadLibraryA("riched20.dll");
        if (g_hRichEdit) g_richEditClass = "RichEdit20A";
        else {
            g_hRichEdit = LoadLibraryA("riched32.dll");
            if (g_hRichEdit) g_richEditClass = "RichEdit";
        }
    }
    if (!g_hRichEdit) {
        char msg[600], c20[12], c32[12];
        UIntToStrA(err20, c20); UIntToStrA(err32, c32);
        strcpy(msg, "Не удалось загрузить RichEdit.\n\n");
        strcat(msg, dllPath20); strcat(msg, "\nошибка: "); strcat(msg, c20);
        strcat(msg, "\n\n"); strcat(msg, dllPath32); strcat(msg, "\nошибка: "); strcat(msg, c32);
        MessageBoxA(NULL, msg, "Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_msgFind = RegisterWindowMessageA(FINDMSGSTRINGA);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP));
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "SM6337EditWndClass";
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA("SM6337EditWndClass", "Без имени — SM6337 Edit",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, BuildMenu(), hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    /* Файл из командной строки (двойной клик по .rtf/.txt, «Открыть с помощью», перетаскивание на exe) */
    if (lpCmdLine && lpCmdLine[0]) {
        char path[MAX_PATH];
        const char *s = lpCmdLine;
        size_t k = 0;
        while (*s == ' ') s++;
        if (*s == '"') s++;
        while (*s && *s != '"' && k < MAX_PATH - 1) path[k++] = *s++;
        path[k] = 0;
        if (k > 0) OpenDocumentPath(hWnd, path);
    }

    static ACCEL accels[] = {
        { FCONTROL | FVIRTKEY, 'F', IDM_EDIT_FIND },
        { FVIRTKEY, VK_F3, IDM_EDIT_FINDNEXT },
        { FCONTROL | FVIRTKEY, 'Y', IDM_EDIT_REDO },
        { FCONTROL | FVIRTKEY, 'L', IDM_PARA_LEFT },
        { FCONTROL | FVIRTKEY, 'E', IDM_PARA_CENTER },
        { FCONTROL | FVIRTKEY, 'R', IDM_PARA_RIGHT },
        { FCONTROL | FVIRTKEY, 'J', IDM_PARA_JUSTIFY },
        { FCONTROL | FVIRTKEY, 'N', IDM_FILE_NEW },
        { FCONTROL | FVIRTKEY, 'O', IDM_FILE_OPEN },
        { FCONTROL | FVIRTKEY, 'S', IDM_FILE_SAVE },
        { FCONTROL | FVIRTKEY, 'P', IDM_FILE_PRINT },
        { FVIRTKEY, VK_F1, IDM_HELP_CONTENTS },
    };
    HACCEL hAccel = CreateAcceleratorTableA(accels, sizeof(accels) / sizeof(accels[0]));

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (g_hFindDlg && IsDialogMessageA(g_hFindDlg, &msg)) continue;
        if (hAccel && TranslateAcceleratorA(hWnd, hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    FreeLibrary(g_hRichEdit);
    return (int)msg.wParam;
}
